#include "editor.h"

#include "about.h"
#include "help.h"
#include "utf8.h"
#include "symbols.h"
#include "workspace.h"
#include "path.h"
#include "settings.h"

#include <cstdio>
#include <cstdlib>
#include <system_error>

namespace editor {

namespace {

const int kTreeWidth = 22;   // enough for a name and two levels of nesting
const int kPanelRows = 7;    // the command, and a few lines of what it said

// The lines the screen is partitioned by, from the box-drawing block. Written
// as their bytes rather than as characters, so that no compiler has to be told
// what encoding this file is in and no source file has to carry a mark saying
// so - MSVC reads a plain file in the machine's own code page.
//
//   horizontal U+2500, vertical U+2502, then the four corners, the four tees
//   and the cross.
// The same frame in characters every font has. A console that draws the plain
// line from one face and the junctions from another - which is what a missing
// glyph makes it do - puts their crossbars at different heights, and the line
// appears to break at every tee. Nothing in the program can mend that; this is
// the way round it, chosen with --plain.
const char* const kPlainAcross = "-";
const char* const kPlainDown   = "|";
const char* const kPlainJoint  = "+";

const char* const kAcross    = "\xe2\x94\x80";
const char* const kDown      = "\xe2\x94\x82";
const char* const kTopLeft   = "\xe2\x94\x8c";
const char* const kTopRight  = "\xe2\x94\x90";
const char* const kFootLeft  = "\xe2\x94\x94";
const char* const kFootRight = "\xe2\x94\x98";
const char* const kTeeDown   = "\xe2\x94\xac";
const char* const kTeeUp     = "\xe2\x94\xb4";
const char* const kTeeRight  = "\xe2\x94\x9c";
const char* const kTeeLeft   = "\xe2\x94\xa4";

// Which of the two the screen is drawn with. Every line and corner is asked
// for through one of these, so the choice is made once and nothing below has
// to know which was made.
}  // namespace

const Frame kBoxFrame = {kAcross, kDown, kTopLeft, kTopRight, kFootLeft, kFootRight,
                         kTeeDown, kTeeUp, kTeeRight, kTeeLeft, "\xe2\x80\xa2"};
const Frame kPlainFrame = {kPlainAcross, kPlainDown, kPlainJoint, kPlainJoint,
                           kPlainJoint, kPlainJoint, kPlainJoint, kPlainJoint,
                           kPlainJoint, kPlainJoint, "*"};

namespace {

// One character repeated. The box characters are three bytes each and one
// column each, which is the whole reason this is not std::string(n, c).
std::string repeated(const char* one, int times) {
    std::string out;
    for (int i = 0; i < times; ++i) out += one;
    return out;
}

// Puts a paragraph into the console a line at a time. The console holds lines
// and a refusal is a sentence or three; without this the end of it is off the
// side of the screen, which reads as the editor having nothing more to say.
void wrapInto(std::vector<std::string>& lines, const std::string& text, size_t width) {
    if (width < 20) width = 20;
    std::string line;
    size_t at = 0;
    while (at <= text.size()) {
        size_t space = text.find(' ', at);
        std::string word = text.substr(at, space == std::string::npos ? std::string::npos
                                                                      : space - at);
        if (!line.empty() && line.size() + 1 + word.size() > width) {
            lines.push_back(line);
            line.clear();
        }
        if (!line.empty()) line += ' ';
        line += word;
        if (space == std::string::npos) break;
        at = space + 1;
    }
    if (!line.empty()) lines.push_back(line);
}

// Takes `width` columns of `s` starting at `from`, padded with spaces if the
// line runs out first. Every region is padded to its full width, so the one
// beside it starts in the same column on every row.
std::string window(const std::string& s, size_t from, size_t width) {
    std::string out;
    if (from < s.size()) out = s.substr(from, width);
    out.resize(width, ' ');
    return out;
}

std::string number(size_t n) { return std::to_string(n); }

std::string lineCountText(size_t n) {
    return std::to_string(n) + (n == 1 ? " line" : " lines");
}

size_t digitsIn(size_t n) {
    size_t d = 1;
    while (n >= 10) { n /= 10; ++d; }
    return d;
}

// Tabs and their colours expand together, so that a mark still sits under the
// character it was worked out for once the line has been widened.
//
// A tab stop is counted in screen columns rather than in bytes, or a line with
// anything but ASCII in front of a tab would line up wrongly.
void expandWithKinds(const std::string& s, const std::vector<unsigned char>& kinds,
                     std::string& text, std::vector<unsigned char>& out) {
    size_t column = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned char k = i < kinds.size() ? kinds[i] : KindNormal;

        if (s[i] == '\t') {
            do {
                text += ' ';
                out.push_back(k);
                ++column;
            } while (column % kTabStop != 0);
            ++i;
            continue;
        }

        size_t step = utf8::next(s, i);
        if (step <= i) step = i + 1;
        for (size_t b = i; b < step && b < s.size(); ++b) {
            text += s[b];
            out.push_back(k);
        }
        column += utf8::widthOf(utf8::codePointAt(s, i));
        i = step;
    }
}

// A window of the line, written as runs of one colour, with whatever is
// selected shown in reverse. One escape per run rather than one per character -
// a screen's worth of the latter is enough to be seen redrawing on a slow
// console. selFrom and selTo are screen columns, and equal means nothing is
// selected on this line.
std::string colouredWindow(const std::string& text,
                           const std::vector<unsigned char>& kinds,
                           size_t from, size_t width,
                           size_t selFrom, size_t selTo) {
    std::string out;
    size_t column = 0;   // where the next character will be drawn
    size_t drawn = 0;
    int current = -1;
    bool inverted = false;

    // Walked a character at a time and counted in screen columns, not bytes:
    // one Urdu letter is two bytes and one column, and one Chinese character
    // is three bytes and two columns.
    for (size_t i = 0; i < text.size();) {
        size_t step = utf8::next(text, i);
        if (step <= i) step = i + 1;
        size_t wide = utf8::widthOf(utf8::codePointAt(text, i));

        if (column + wide <= from) {   // still left of what is being shown
            column += wide;
            i = step;
            continue;
        }
        if (drawn + wide > width) break;

        bool wanted = (column >= selFrom && column < selTo);
        if (wanted != inverted) {
            out += wanted ? "\x1b[7m" : "\x1b[27m";
            inverted = wanted;
        }

        unsigned char k = i < kinds.size() ? kinds[i] : KindNormal;
        if (static_cast<int>(k) != current) {
            out += "\x1b[";
            out += colourFor(k);
            out += "m";
            current = k;
        }
        for (size_t b = i; b < step && b < text.size(); ++b) out += text[b];

        column += wide;
        drawn += wide;
        i = step;
    }

    if (inverted) out += "\x1b[27m";
    if (current != -1 && current != KindNormal) out += "\x1b[39m";
    if (drawn < width) out.append(width - drawn, ' ');
    return out;
}

size_t leadingSpace(const std::string& line) {
    size_t n = 0;
    while (n < line.size() && (line[n] == ' ' || line[n] == '\t')) ++n;
    return n;
}

// Whether a colon just typed ends a label, and so is worth re-laying the line
// for. The colon in `x = a ? b : c` is not, and a line moving under the caret
// while an expression is half written would be worse than no rule at all.
bool endsALabel(const std::string& before) {
    std::string t = before.substr(leadingSpace(before));
    if (t.compare(0, 5, "case ") == 0 || t == "default") return true;
    if (t.empty() || (t[0] >= '0' && t[0] <= '9')) return false;
    for (size_t i = 0; i < t.size(); ++i) {
        char ch = t[i];
        bool ident = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                     (ch >= '0' && ch <= '9') || ch == '_';
        if (!ident) return false;
    }
    return true;
}

std::string baseName(const std::string& path) {
    size_t at = path.find_last_of("/\\");
    return at == std::string::npos ? path : path.substr(at + 1);
}

void consoleSink(void* context, const std::string& line) {
    static_cast<Editor*>(context)->console(line);
}

}  // namespace

Editor::Editor()
    : docs_(1), doc_(0),
      cx_(0), cy_(0), rx_(0), rowoff_(0), coloff_(0),
      treeSel_(0), treeOff_(0), treeOpen_(true),
      panelOff_(0), panelOpen_(true), tab_(TabConsole),
      focus_(FocusText), lang_(LangPlain), config_(ConfigDebug), arch_(0), numbers_(true), needsDraw_(true), debugTemporary_(true),
      stopLine_(0),
      looking_(0),
      marked_(false), markRow_(0), markCol_(0),
      quitConfirm_(0), running_(true),
      screenRows_(24), screenCols_(80),
      bodyRows_(14), panelRows_(kPanelRows),
      treeCols_(kTreeWidth), sourceCols_(80), gutterCols_(4), paintedCols_(0) {
    frame_ = &kBoxFrame;
    // The host's own architecture first, since that is the one cc1 will carry
    // past -S on this machine - and so the only one Run can do anything with.
    for (size_t i = 0; i < 3; ++i)
        if (std::string(kArches[i]) == hostArch()) arch_ = i;
    // $CC1 first, then a cc1 installed beside this editor, then the bare name
    // for PATH to answer - the same three the window asks, in the same order.
    // The middle one is what `make product` and `build.bat product` build:
    // one directory holding the editor and the compiler it drives, which used
    // to be found only when the editor happened to be started in it. Beside
    // the editor is asked before PATH deliberately - a compiler shipped with
    // this copy is the one this copy is meant to run. --cc1 overrides all of
    // it, later, in main.
    //
    // cl is not looked for here: nothing installs it beside the editor, and
    // toolchain.cpp already finds it through Visual Studio itself, which is a
    // better answer than any directory search.
    const char* fromEnv = std::getenv("CC1");
    if (fromEnv && *fromEnv) {
        tool_.cc1 = fromEnv;
    } else {
        // cc1.exe first, which is what Compiler-C builds on every machine
        // now; then bare cc1, because installs made before that rename still
        // have one and an editor that stopped finding them would be the
        // rename breaking somebody's setup for nothing.
        std::string beside = path::besideProgram("cc1.exe");
        if (beside.empty()) beside = path::besideProgram("cc1");
        if (!beside.empty()) tool_.cc1 = beside;
    }

    // shc is found the same way, and for the same reason.
    const char* shcFromEnv = std::getenv("SHC");
    if (shcFromEnv && *shcFromEnv) {
        tool_.shc = shcFromEnv;
    } else {
        std::string beside = path::besideProgram("shc.exe");
        if (beside.empty()) beside = path::besideProgram("shc");
        if (!beside.empty()) tool_.shc = beside;
    }

    console_.push_back("Compiler output appears here.  Ctrl-B builds.");
    resetDebug();
}

void Editor::stash() {
    docs_[doc_].buf = buf_;
    docs_[doc_].cx = cx_;
    docs_[doc_].cy = cy_;
    docs_[doc_].rowoff = rowoff_;
    docs_[doc_].coloff = coloff_;
    docs_[doc_].lang = lang_;
}

void Editor::restore() {
    buf_ = docs_[doc_].buf;
    cx_ = docs_[doc_].cx;
    cy_ = docs_[doc_].cy;
    rowoff_ = docs_[doc_].rowoff;
    coloff_ = docs_[doc_].coloff;
    lang_ = docs_[doc_].lang;
}

// One file has more than one spelling, and comparing the text opened it twice:
// "src/one.c" from the command line and the same file from the project pane,
// which hands out the path it counts from the root. The second copy is read
// from the disk, so unsaved changes in the first look as though they have been
// thrown away - and saving the second would write over them.
//
// Compared by what they resolve to, not by how they were written. On Windows
// the case is not part of the answer either, which path::absolute leaves alone
// deliberately - the name is shown to people as they wrote it.
size_t Editor::findDocument(const std::string& path) const {
    std::string want = path::oneName(path);

    for (size_t i = 0; i < docs_.size(); ++i) {
        const std::string& have = (i == doc_) ? buf_.path() : docs_[i].buf.path();
        if (have.empty()) continue;
        if (path::oneName(have) == want) return i;
    }
    return docs_.size();
}

void Editor::switchTo(size_t index) {
    if (index >= docs_.size() || index == doc_) return;
    stash();
    doc_ = index;
    restore();
    focus_ = FocusText;
}

void Editor::nextDocument(int by) {
    if (docs_.size() < 2) { say("only one file is open"); return; }
    size_t at = (doc_ + docs_.size() + static_cast<size_t>(by)) % docs_.size();
    switchTo(at);
    say(buf_.path().empty() ? std::string("[no name]") : buf_.path());
}

// Every open file, not the one in front. The tab you are looking at is rarely
// the one you forgot to save - which is exactly how this was found: two files
// open, changes in the one behind, and Ctrl-Q left without a word.
size_t Editor::unsaved(std::string& named) const {
    size_t count = 0;
    named.clear();
    for (size_t i = 0; i < docs_.size(); ++i) {
        // The one in front lives in buf_; docs_[doc_] is only written back
        // when the tab changes, so it is a copy of what it was.
        const Buffer& b = (i == doc_) ? buf_ : docs_[i].buf;
        if (!b.dirty()) continue;
        if (named.empty())
            named = b.path().empty() ? std::string("[no name]") : baseName(b.path());
        ++count;
    }
    return count;
}

// Asked by both ways out - Ctrl-Q and the Quit on the File menu, which used to
// leave outright and check nothing at all. The first press says what would be
// lost and the second leaves anyway; anything else typed in between clears
// that, which processKey does.
bool Editor::mayLeave() {
    std::string named;
    size_t count = unsaved(named);
    if (count == 0 || quitConfirm_ != 0) return true;

    quitConfirm_ = 1;
    if (count == 1)
        say("unsaved changes in " + named + " - press Ctrl-Q again to leave them behind");
    else
        say(named + " and " + number(count - 1) + " other" + (count == 2 ? "" : "s") +
            " have unsaved changes - press Ctrl-Q again to leave them behind");
    return false;
}

void Editor::closeDocument() {
    if (buf_.dirty()) {
        say("unsaved changes - save it, or Ctrl-Q twice to leave");
        return;
    }
    stash();
    docs_.erase(docs_.begin() + static_cast<long>(doc_));
    // There is always a document, even if it is an empty one. Every other rule
    // in this file assumes there is a buffer to put the caret in.
    if (docs_.empty()) docs_.push_back(Document());
    if (doc_ >= docs_.size()) doc_ = docs_.size() - 1;
    restore();
    say("closed");
}

void Editor::open(const std::string& path) {
    size_t already = findDocument(path);
    if (already < docs_.size()) {
        switchTo(already);
        say(path + " - already open");
        return;
    }

    // An untouched, unnamed document is a spare tab rather than a file anyone
    // is working on, so opening into it replaces it instead of adding another.
    bool reuse = buf_.path().empty() && !buf_.dirty() && buf_.lineCount() == 1 &&
                 buf_.line(0).empty();
    if (!reuse) {
        stash();
        docs_.push_back(Document());
        doc_ = docs_.size() - 1;
        restore();
    }

    std::string error;
    switch (buf_.load(path, error)) {
        case Buffer::Opened:  say(path + "  " + lineCountText(buf_.lineCount())); break;
        case Buffer::NewFile: say(path + "  new file"); break;
        case Buffer::Failed:  say(error); break;
    }
    cx_ = cy_ = rowoff_ = coloff_ = 0;
    applyLanguage();

    size_t at = tree_.find(path);
    if (at < tree_.size()) treeSel_ = at;
}

void Editor::applyProject() {
    if (!project_.loaded()) return;

    // The project's settings are the project's. Anything named on the command
    // line is applied after this and wins, which is the order someone expects:
    // the file is what the project always does, the flag is what today needs.
    style_ = project_.indent();
    tool_.kind = project_.toolchain();
    config_ = project_.config();
    for (size_t i = 0; i < 3; ++i)
        if (project_.arch() == kArches[i]) arch_ = i;
}

void Editor::openFirstFile() {
    if (!buf_.path().empty()) return;   // something was named, and it wins

    const std::vector<Group>& groups = project_.groups();
    for (size_t i = 0; i < groups.size(); ++i)
        for (size_t j = 0; j < groups[i].files.size(); ++j) {
            std::string where = project_.absolute(groups[i].files[j]);
            if (!path::exists(where)) continue;   // listed but not there any more

            // Whatever opening the project had to say is the news on a first
            // run - that one was made, or which one it is. Which file happens
            // to be in front of you is on the status bar either way, so it
            // does not need the line as well.
            std::string already = message_;
            open(where);
            if (!already.empty()) say(already);
            return;
        }
}

void Editor::refreshTree() {
    if (project_.loaded()) {
        tree_.showProject(project_);
    } else {
        tree_.reread();
    }
    if (treeSel_ >= tree_.size()) treeSel_ = tree_.size() ? tree_.size() - 1 : 0;
}

void Editor::openProject(const std::string& path) {
    projectDir_ = path;

    std::string error;
    if (project_.load(path, error)) {
        applyProject();
        refreshTree();
        // Remembered so that the next run opens here without being told. It is
        // the editor's own configuration and not the project's - see
        // settings.h for why it cannot live in an ed1.json.
        settings::rememberProject(project_.root());
        // "ready" rather than a bare count: it is the first thing on the line
        // when the editor comes up on a project it was told about or one it
        // remembered, and what it means is that there is nothing to do first.
        size_t howMany = project_.groups().size();
        say("ready - " + project_.name() + ", " + number(howMany) +
            (howMany == 1 ? " group" : " groups"));
        sayIfSettingsWereBad();
    } else if (error.empty()) {
        // Nothing to read, so one is written from what is in the directory
        // rather than opening without a project at all. An editor that needs a
        // file it can perfectly well make is an editor that stops for no
        // reason.
        Outcome made = beginFromWhatIsThere(project_, path);
        if (made.ok) {
            applyProject();
            refreshTree();
            settings::rememberProject(project_.root());
            say(made.message);
        } else {
            tree_.setRoot(path);
            project_.setRoot(tree_.root());
            say(made.message);
        }
    } else {
        // A project file that will not parse is a different matter: it is
        // somebody's, and writing over it to save an error message would be
        // the editor destroying work. The pane shows the directory instead,
        // which is what it did before projects existed.
        tree_.setRoot(path);
        project_.setRoot(tree_.root());
        say(error);
    }
    treeSel_ = 0;
    treeOff_ = 0;
}

void Editor::console(const std::string& line) {
    console_.push_back(line);
    // Keep the newest line in view while the build is running, which is the
    // whole point of showing it as it arrives.
    if (panelOpen_ && tab_ == TabConsole && console_.size() > static_cast<size_t>(panelRows_))
        panelOff_ = console_.size() - static_cast<size_t>(panelRows_);
    refresh();
}

const std::vector<std::string>& Editor::panelLines() const {
    if (tab_ == TabConsole) return console_;
    if (tab_ == TabDebug) return debug_;
    return assembly_;
}

void Editor::layout() {
    term_.size(screenRows_, screenCols_);
    if (screenRows_ < 8) screenRows_ = 8;
    if (screenCols_ < 40) screenCols_ = 40;

    // One row each for the menu, the line the panes are drawn under, the line
    // they are closed off with, the status bar and the message line. The panel
    // takes the line above it - which carries its tabs, so the line and the
    // names are one row and not two - plus its own.
    int taken = 5;
    panelRows_ = kPanelRows;
    if (panelOpen_) {
        // On a short window the panel gives ground rather than squeezing the
        // text out of existence.
        int room = screenRows_ - taken - 4;
        if (panelRows_ > room) panelRows_ = room;
        if (panelRows_ < 2) panelRows_ = 2;
        taken += panelRows_ + 1;
    }
    bodyRows_ = screenRows_ - taken;
    // On a window short enough for the arithmetic to run out, the panel gives
    // up its last rows rather than the frame giving up a line: a screen drawn
    // one row taller than the terminal scrolls, and then none of the lines
    // meet anything.
    while (bodyRows_ < 1 && panelOpen_ && panelRows_ > 1) {
        --panelRows_;
        ++bodyRows_;
    }
    if (bodyRows_ < 1) bodyRows_ = 1;

    // Three columns of the width are lines when the project pane is open - the
    // two edges and the one between the panes - and two when it is closed.
    if (treeOpen_) {
        treeCols_ = kTreeWidth;
        if (treeCols_ > screenCols_ / 3) treeCols_ = screenCols_ / 3;
        sourceCols_ = screenCols_ - treeCols_ - 3;
    } else {
        treeCols_ = 0;
        sourceCols_ = screenCols_ - 2;
    }

    // Wide enough for the last line in the file, and it does not shrink back
    // when you scroll up - a gutter that changed width as you moved would take
    // the text with it.
    gutterCols_ = 0;
    if (numbers_) {
        gutterCols_ = static_cast<int>(digitsIn(buf_.lineCount())) + 1;
        if (gutterCols_ < 4) gutterCols_ = 4;
        sourceCols_ -= gutterCols_;
    }
    if (sourceCols_ < 10) sourceCols_ = 10;
}

size_t Editor::renderCol(const std::string& line, size_t col) const {
    size_t r = 0;
    for (size_t i = 0; i < col && i < line.size();) {
        if (line[i] == '\t') {
            r += kTabStop - (r % kTabStop);
            ++i;
            continue;
        }
        r += utf8::widthOf(utf8::codePointAt(line, i));
        size_t step = utf8::next(line, i);
        i = (step > i) ? step : i + 1;
    }
    return r;
}

void Editor::clampCursor() {
    if (cy_ >= buf_.lineCount()) cy_ = buf_.lineCount() - 1;
    if (cx_ > buf_.line(cy_).size()) cx_ = buf_.line(cy_).size();
    // Never inside a character. Everything else here can then assume the caret
    // is somewhere a character begins.
    cx_ = utf8::startOf(buf_.line(cy_), cx_);
}

void Editor::scroll() {
    rx_ = renderCol(buf_.line(cy_), cx_);

    if (cy_ < rowoff_) rowoff_ = cy_;
    if (cy_ >= rowoff_ + static_cast<size_t>(bodyRows_))
        rowoff_ = cy_ - static_cast<size_t>(bodyRows_) + 1;

    if (rx_ < coloff_) coloff_ = rx_;
    if (rx_ >= coloff_ + static_cast<size_t>(sourceCols_))
        coloff_ = rx_ - static_cast<size_t>(sourceCols_) + 1;

    if (treeSel_ < treeOff_) treeOff_ = treeSel_;
    if (treeSel_ >= treeOff_ + static_cast<size_t>(bodyRows_))
        treeOff_ = treeSel_ - static_cast<size_t>(bodyRows_) + 1;
}

void Editor::drawMenuBar(std::string& out) const {
    std::string bar(static_cast<size_t>(screenCols_), ' ');
    for (size_t i = 0; i < menu_.columns().size(); ++i) {
        const std::string& title = menu_.columns()[i].title;
        size_t at = menu_.titleAt(i);
        for (size_t j = 0; j < title.size() && at + j < bar.size(); ++j)
            bar[at + j] = title[j];
    }

    out += "\x1b[7m";
    // The active title is drawn back in normal video, which on an inverted bar
    // is what makes it look pressed.
    if (menu_.active()) {
        size_t at = menu_.titleAt(menu_.column());
        size_t len = menu_.columns()[menu_.column()].title.size();
        out += bar.substr(0, at);
        out += "\x1b[m";
        out += bar.substr(at, len);
        out += "\x1b[7m";
        out += bar.substr(at + len);
    } else {
        out += bar;
    }
    out += "\x1b[m\r\n";
}

// A line across the whole screen. The junction is where the pane divider meets
// it, when there is one; the labels are laid into the line after it and the
// tail is put at its right-hand end. A line with the names in it is one row
// where a line and a row of names would be two, and rows are what a terminal
// has least of.
std::string Editor::rule(const char* left, const char* right, const char* junction,
                         const std::string& labels, int labelColumns,
                         const std::string& tail, int tailColumns) const {
    std::string out = left;
    int used = 1;
    if (treeOpen_ && junction != 0) {
        out += repeated(frame_->across, treeCols_);
        out += junction;
        used += treeCols_ + 1;
    }

    int room = screenCols_ - used - 1;   // what is left, less the right-hand end
    if (room < 0) room = 0;

    // Never flush against a corner: a space of line either side is what makes
    // a name look set into the line rather than dropped on top of it.
    if (labelColumns > 0 && labelColumns + 1 <= room) {
        out += frame_->across;
        out += labels;
        room -= labelColumns + 1;
    }
    if (tailColumns > 0 && tailColumns + 2 <= room) {
        out += repeated(frame_->across, room - tailColumns - 1);
        out += tail;
        out += frame_->across;
        room = 0;
    }
    out += repeated(frame_->across, room);
    out += right;
    return out;
}

void Editor::drawFrameTop(std::string& out) const {
    // The files that are open are the top edge of the pane they are shown in,
    // which is where a tab belongs: the one in front is the one lit up.
    int room = screenCols_ - 2 - (treeOpen_ ? treeCols_ + 1 : 0);
    std::string tabs;
    int wide = 0;

    for (size_t i = 0; i < docs_.size(); ++i) {
        const Buffer& b = (i == doc_) ? buf_ : docs_[i].buf;
        std::string name = b.path().empty() ? std::string("[no name]") : baseName(b.path());
        if (b.dirty()) name += "*";
        std::string cell = " " + name + " ";
        int cellWide = static_cast<int>(utf8::columns(cell, cell.size()));

        // One column for the line that closes this tab off from the next.
        if (wide + cellWide + 2 > room) break;
        if (i == doc_) {
            tabs += "\x1b[7m";
            tabs += cell;
            tabs += "\x1b[m";
        } else {
            tabs += "\x1b[90m";
            tabs += cell;
            tabs += "\x1b[39m";
        }
        tabs += frame_->across;
        wide += cellWide + 1;
    }

    out += rule(frame_->topLeft, frame_->topRight, frame_->teeDown, tabs, wide, std::string(), 0);
    out += "\x1b[K\r\n";
}

// The line the panes are closed off with. The divider between them runs down to
// it when the panel is shut, and stops at the panel's own line when it is open.
void Editor::drawFrameFoot(std::string& out) const {
    out += rule(frame_->footLeft, frame_->footRight, panelOpen_ ? 0 : frame_->teeUp,
                std::string(), 0, std::string(), 0);
    out += "\x1b[K\r\n";
}

void Editor::drawBody(std::string& out) const {
    // Drawing starts at rowoff_, so the lines above it are walked first - a
    // block comment opened off the top of the screen still colours what is on
    // it. Only the state is carried; no colours are worked out for them.
    SyntaxState state;
    for (size_t i = 0; i < rowoff_ && i < buf_.lineCount(); ++i)
        advanceState(buf_.line(i), lang_, state);

    for (int y = 0; y < bodyRows_; ++y) {
        out += frame_->down;                       // the left edge of the window
        if (treeOpen_) {
            size_t row = treeOff_ + static_cast<size_t>(y);
            std::string cell;
            if (row < tree_.size()) {
                const TreeEntry& e = tree_.entries()[row];
                cell = std::string(static_cast<size_t>(e.depth) * 2, ' ');
                cell += e.directory ? (e.open ? "-" : "+") : " ";
                cell += " ";
                cell += e.name;
            }
            bool picked = (row < tree_.size() && row == treeSel_);
            if (picked) out += (focus_ == FocusTree) ? "\x1b[7m" : "\x1b[4m";
            out += window(cell, 0, static_cast<size_t>(treeCols_));
            if (picked) out += "\x1b[m";
            out += frame_->down;                   // and the one between the panes
        }

        size_t row = rowoff_ + static_cast<size_t>(y);

        if (numbers_) {
            std::string cell(static_cast<size_t>(gutterCols_), ' ');
            bool hasBreak = false, isStopped = false, isLooked = false;
            if (row < buf_.lineCount()) {
                std::string num = number(row + 1);
                // Right-aligned, with the last column left as a gap so the
                // digits never touch the text.
                size_t at = cell.size() - 1 - num.size();
                for (size_t i = 0; i < num.size(); ++i) cell[at + i] = num[i];

                // The first column of the gutter is the debugger's: a
                // breakpoint waiting, and the line the program is standing on.
                // Both in the space the numbers are already right-aligned away
                // from, so nothing moves when a breakpoint is set.
                hasBreak = breakpointOn(row + 1);
                isStopped = stopLine_ == row + 1 && !stopFile_.empty() &&
                            path::filename(stopFile_) == path::filename(buf_.path());

                // And the frame being looked at, when it is not the one the
                // program stopped in - the line that is waiting for the call to
                // come back. A different mark rather than the same one: the
                // program is not standing there, you are only looking at it.
                isLooked = looking_ > 0 && looking_ < stack_.size() &&
                           stack_[looking_].line == row + 1 &&
                           !stack_[looking_].file.empty() &&
                           path::filename(stack_[looking_].file) ==
                               path::filename(buf_.path());

                // Where the program is outranks where you are looking, which
                // outranks a breakpoint: the first two are about now and the
                // third is about every run of the program.
                //
                // A colon rather than the dash tried first, which against a
                // two-digit number reads as a minus sign - "-11" - and is
                // already what the project pane puts in front of an open
                // group. A colon is nothing else here and cannot be read as
                // part of the number.
                if (isStopped) cell[0] = '>';
                else if (isLooked) cell[0] = ':';
                else if (hasBreak) cell[0] = '*';
            }

            // The line the caret is on is picked out, which is the whole reason
            // for having the numbers where you can see them. Where the program
            // stopped outranks it: that is the one line being looked at.
            if (isStopped) out += "\x1b[92m";
            else if (isLooked) out += "\x1b[96m";
            else if (hasBreak) out += "\x1b[91m";
            else out += (row == cy_) ? "\x1b[93m" : "\x1b[90m";
            out += cell;
            out += "\x1b[39m";
        }

        if (row < buf_.lineCount()) {
            std::vector<unsigned char> kinds = highlight(buf_.line(row), lang_, state);
            std::string text;
            std::vector<unsigned char> spread;
            expandWithKinds(buf_.line(row), kinds, text, spread);

            // The selection is measured in the line's own columns and drawn in
            // the screen's, so a tab inside it highlights its whole width.
            size_t selFrom = 0, selTo = 0;
            size_t rawFrom = 0, rawTo = 0;
            if (selectionOn(row, rawFrom, rawTo)) {
                selFrom = renderCol(buf_.line(row), rawFrom);
                selTo = renderCol(buf_.line(row), rawTo);
            }
            out += colouredWindow(text, spread, coloff_,
                                  static_cast<size_t>(sourceCols_), selFrom, selTo);
        } else {
            std::string empty = numbers_ ? std::string() : std::string("~");
            empty.resize(static_cast<size_t>(sourceCols_), ' ');
            out += empty;
        }
        // Plainly, whatever the last thing drawn was coloured with.
        out += "\x1b[m";
        out += frame_->down;
        out += "\x1b[K\r\n";
    }
}

void Editor::drawPanel(std::string& out) const {
    if (!panelOpen_) return;

    std::string header;
    const char* names[TabCount] = {" Console ", " Debug ", " Assembly "};
    int visible = 0;
    for (int i = 0; i < TabCount; ++i) {
        bool on = (tab_ == static_cast<Tab>(i));
        if (on) {
            header += "\x1b[7m";
            header += names[i];
            header += "\x1b[m";
        } else {
            header += "\x1b[90m";
            header += names[i];
            header += "\x1b[39m";
        }
        visible += static_cast<int>(std::string(names[i]).size());

        // A tick of the line between one tab and the next, so they read as
        // three names on a line rather than as one long one.
        if (i + 1 < TabCount) {
            header += frame_->across;
            visible += 1;
        }
    }

    std::string right;
    if (tab_ == TabConsole)
        right = number(console_.size()) + (console_.size() == 1 ? " line" : " lines");
    else if (tab_ == TabDebug)
        right = assembly_.empty() ? "nothing built yet" : "read from the assembly";
    else
        right = assembly_.empty() ? std::string("nothing built yet")
                                  : number(assembly_.size()) + " lines";
    // The line between the text and the panel, carrying the panel's own tabs
    // and, at its far end, how much there is to read. The divider between the
    // panes stops here, which is what the tee pointing up says.
    out += rule(frame_->teeRight, frame_->teeLeft, frame_->teeUp, header, visible, " " + right + " ",
                static_cast<int>(right.size()) + 2);
    out += "\x1b[K\r\n";

    const std::vector<std::string>& lines = panelLines();
    Language panelLang = (tab_ == TabAssembly) ? LangAsm : LangPlain;
    size_t panelCols = static_cast<size_t>(screenCols_ > 2 ? screenCols_ - 2 : 1);
    for (int y = 0; y < panelRows_; ++y) {
        size_t row = panelOff_ + static_cast<size_t>(y);
        out += frame_->down;
        if (row >= lines.size()) {
            out += std::string(panelCols, ' ');
        } else {
            SyntaxState panelState;
            std::vector<unsigned char> kinds = highlight(lines[row], panelLang, panelState);
            std::string text;
            std::vector<unsigned char> spread;
            expandWithKinds(lines[row], kinds, text, spread);
            out += colouredWindow(text, spread, 0, panelCols, 0, 0);
        }
        out += "\x1b[m";
        out += frame_->down;
        out += "\x1b[K\r\n";
    }
}

void Editor::drawStatus(std::string& out) const {
    std::string name = buf_.path().empty() ? std::string("[no name]") : baseName(buf_.path());
    std::string left = " " + name;
    if (buf_.dirty()) left += " *";
    left += "  " + lineCountText(buf_.lineCount());

    // What will actually run, not what was picked - with a mark when the file
    // is what picked it.
    ToolchainKind kind = resolve(tool_, lang_);
    std::string right = languageName(lang_);
    right += "  ";
    right += configName(config_);
    right += "  ";
    // toolchainShown, not toolchainName: the build console says clang++ or
    // g++ because which C++ compiler this machine has is the whole content of
    // that answer, and a status bar saying "c++" while the console two lines
    // below said "clang++" was the same fact written two ways.
    right += toolchainShown(tool_, kind);
    if (tool_.kind == ToolAuto) right += "*";
    // The target is only shown when it means something. cl generates for the
    // host it was installed as, and offering a choice that does nothing would
    // be the status bar telling a lie.
    if (usesArch(kind)) {
        right += " ";
        right += kArches[arch_];
    }
    right += "  " + number(cy_ + 1) + "/" + number(buf_.lineCount());
    right += "  col " + number(rx_ + 1);
    right += (focus_ == FocusText) ? "  [text]" : (focus_ == FocusTree ? "  [tree]" : "  [panel]");
    right += " ";

    std::string bar = left;
    size_t width = static_cast<size_t>(screenCols_);
    if (bar.size() + right.size() <= width) {
        bar.resize(width - right.size(), ' ');
        bar += right;
    }
    bar.resize(width, ' ');

    out += "\x1b[7m";
    out += bar;
    out += "\x1b[m\r\n";
}

void Editor::drawMessage(std::string& out) const {
    // No timer on this line. A diagnostic has to stay put while you fix the
    // line it points at, and a message that faded after five seconds would be
    // gone by the time you had read the code.
    std::string text = message_;
    if (text.size() > static_cast<size_t>(screenCols_))
        text.resize(static_cast<size_t>(screenCols_));
    out += text;
    out += "\x1b[K";
}

// Whether a menu item names what the editor is already doing.
//
// Only the items that stand for a *state* answer yes. "Save" and "Build
// project" are things you ask for and are never current; "C++", "cc1",
// "Release" and "Line numbers" are places you already are, and a menu that
// showed you five compilers without saying which one you were on was a menu
// that made you go and look at the status bar.
//
// Asked of the editor's own fields rather than kept as a flag on the item.
// A flag would have to be set from four or five places and would go stale in
// exactly the ones nobody remembered.
// The menu, with what cannot be chosen just now already said.
//
// Told to it here rather than worked out inside it, because the menu knows
// about labels and keys and nothing about debuggers - which is what keeps one
// menu usable from a terminal and a window both.
void Editor::openMenu() {
    std::vector<Action> unavailable;

    // A Shalimar program says where it is and how deep, and nothing else. The
    // compiler emits no table of a function's names against its frame slots,
    // so there is no stack to walk and nothing to watch - see
    // ../Compiler-S/docs/DEBUGGING.md. These three refuse with a sentence when
    // they are chosen; greying them says the same thing before the choice is
    // made, which is the better place to say it.
    if (debuggingShalimar()) {
        unavailable.push_back(ActionFrameUp);
        unavailable.push_back(ActionFrameDown);
        unavailable.push_back(ActionWatch);
    }

    menu_.disable(unavailable);
    menu_.open();
}

bool Editor::menuItemIsCurrent(Action action) const {
    switch (action) {
        // Which language the file is treated as. "By extension" is current
        // when nothing was chosen by hand, which is not the same as the
        // language it happens to be - that is what the status bar says.
        case ActionLangAuto:     return langChoice_ == LangCount;
        case ActionLangC:        return langChoice_ == LangC;
        case ActionLangCpp:      return langChoice_ == LangCpp;
        case ActionLangShalimar: return langChoice_ == LangShalimar;
        case ActionLangText:     return langChoice_ == LangPlain;

        // Which compiler runs. Same shape: "By language" is current when
        // nobody overrode it.
        case ActionToolAuto: return tool_.kind == ToolAuto;
        case ActionToolCc1:  return tool_.kind == ToolCc1;
        case ActionToolShc:  return tool_.kind == ToolShc;
        case ActionToolMsvc: return tool_.kind == ToolMsvc;
        case ActionToolCxx:  return tool_.kind == ToolCxx;

        case ActionArchWindows: return kArches[arch_] == std::string("x86_64-windows");
        case ActionArchLinux:   return kArches[arch_] == std::string("x86_64-linux");
        case ActionArchDarwin:  return kArches[arch_] == std::string("arm64-darwin");

        case ActionConfigDebug:   return config_ == ConfigDebug;
        case ActionConfigRelease: return config_ == ConfigRelease;

        // The switches on the Edit menu, which are on or off rather than one
        // of several - but they are still a state you are in, and leaving them
        // unmarked while everything else was marked would read as an oversight.
        case ActionToggleTree:    return treeOpen_;
        case ActionTogglePanel:   return panelOpen_;
        case ActionToggleNumbers: return numbers_;
        case ActionTogglePlain:   return frame_ != &kBoxFrame;

        default: return false;
    }
}

void Editor::drawDropdown(std::string& out, std::vector<size_t>& covered) const {
    if (!menu_.dropped()) return;

    const MenuColumn& col = menu_.columns()[menu_.column()];

    size_t width = 0;
    for (size_t i = 0; i < col.items.size(); ++i) {
        if (col.items[i].rule) continue;
        // One column for the mark, one for the gap after it, one between the
        // label and the key, and one at each edge.
        size_t w = col.items[i].label.size() + col.items[i].key.size() + 5;
        if (w > width) width = w;
    }

    // The box hangs from the title it belongs to, with its left-hand line one
    // column left of the words so that they sit under the title rather than
    // beside it - and, at the far left, over the window's own corner instead
    // of beside that.
    size_t at = menu_.titleAt(menu_.column());
    if (at > 0) --at;
    if (at + width + 3 > static_cast<size_t>(screenCols_))
        at = static_cast<size_t>(screenCols_) - width - 3;

    // A box of its own, hanging from the title it belongs to. Drawn last and
    // placed by hand, so it lies over the text rather than pushing it aside.
    out += "\x1b[2;" + number(at + 1) + "H\x1b[m";
    out += frame_->topLeft;
    out += repeated(frame_->across, static_cast<int>(width));
    out += frame_->topRight;
    covered.push_back(1);

    for (size_t i = 0; i < col.items.size(); ++i) {
        out += "\x1b[" + number(i + 3) + ";" + number(at + 1) + "H\x1b[m";

        // A rule is drawn across the box rather than written in it, joining
        // the sides the way the panel's own rules do.
        if (col.items[i].rule) {
            out += frame_->teeRight;
            out += repeated(frame_->across, static_cast<int>(width));
            out += frame_->teeLeft;
            covered.push_back(i + 2);
            continue;
        }

        out += frame_->down;

        // The marker sits in the column the leading space used to be, so
        // nothing moves when it appears and the labels stay in one line
        // whether they are marked or not.
        //
        // The dot is three bytes and one column wide, and resize() counts
        // bytes - so the target grows by what the marker costs over the space
        // it replaced. Without that the marked rows come out two columns
        // short and the keys on the right go ragged.
        const std::string mark = menuItemIsCurrent(col.items[i].action)
                                     ? std::string(frame_->chosen)
                                     : std::string(" ");
        std::string row = mark + " " + col.items[i].label;
        row.resize(width - col.items[i].key.size() - 1 + (mark.size() - 1), ' ');
        row += col.items[i].key;
        row += " ";
        // Greyed when it cannot be chosen just now. Faint rather than a
        // different colour, because the reason is "not available" and not "a
        // different kind of thing" - and because a colour would have to work
        // against both a dark and a light terminal.
        if (menu_.disabled(col.items[i].action)) out += "\x1b[2m";
        if (i == menu_.item()) out += "\x1b[7m";
        out += row;
        out += "\x1b[m";
        out += frame_->down;
        covered.push_back(i + 2);
    }

    out += "\x1b[" + number(col.items.size() + 3) + ";" + number(at + 1) + "H\x1b[m";
    out += frame_->footLeft;
    out += repeated(frame_->across, static_cast<int>(width));
    out += frame_->footRight;
    covered.push_back(col.items.size() + 2);
}

// A question in a box of its own, in the middle of the text. It used to be
// asked on the message line, which is also where the editor answers back - so
// the question and the answer to the last one shared a row and neither looked
// like it was waiting for anything.
void Editor::drawDialog(std::string& out, std::vector<size_t>& covered) const {
    if (askTitle_.empty()) return;

    int wide = static_cast<int>(askTitle_.size());
    int answer = static_cast<int>(utf8::columns(askAnswer_, askAnswer_.size()));
    if (answer + 2 > wide) wide = answer + 2;
    if (wide < 40) wide = 40;
    if (wide > screenCols_ - 6) wide = screenCols_ - 6;
    if (wide < 8) wide = 8;

    int at = (screenCols_ - wide - 2) / 2;
    if (at < 0) at = 0;
    int top = 3 + bodyRows_ / 3;
    if (top < 3) top = 3;

    // The title sits in the top line of the box, as the tabs sit in the line
    // above the text: a name in a line, not a row given up to a name.
    std::string title = " " + askTitle_ + " ";
    int titleWide = static_cast<int>(utf8::columns(title, title.size()));
    if (titleWide > wide - 2) {
        title = " ";
        titleWide = 1;
    }

    std::string head = frame_->topLeft;
    head += frame_->across;
    head += title;
    head += repeated(frame_->across, wide - titleWide - 1);
    head += frame_->topRight;

    std::string shown = askAnswer_;
    while (static_cast<int>(utf8::columns(shown, shown.size())) > wide - 2)
        shown.erase(0, utf8::next(shown, 0));
    std::string middle = frame_->down;
    middle += " " + shown;
    middle += std::string(
        static_cast<size_t>(wide - 1 - static_cast<int>(utf8::columns(shown, shown.size()))), ' ');
    middle += frame_->down;

    std::string foot = frame_->footLeft;
    foot += repeated(frame_->across, wide);
    foot += frame_->footRight;

    const std::string* rows[3] = {&head, &middle, &foot};
    for (int i = 0; i < 3; ++i) {
        out += "\x1b[" + number(static_cast<size_t>(top + i)) + ";" +
               number(static_cast<size_t>(at + 1)) + "H";
        out += "\x1b[m";
        out += *rows[i];
        covered.push_back(static_cast<size_t>(top + i - 1));
    }
}

void Editor::placeCursor(std::string& out) const {
    size_t row = 1, col = 1;
    if (!askTitle_.empty()) {
        // In the box, after what has been typed into it - which is the only
        // place a caret means anything while a question is being asked.
        int wide = static_cast<int>(askTitle_.size());
        int answer = static_cast<int>(utf8::columns(askAnswer_, askAnswer_.size()));
        if (answer + 2 > wide) wide = answer + 2;
        if (wide < 40) wide = 40;
        if (wide > screenCols_ - 6) wide = screenCols_ - 6;
        if (wide < 8) wide = 8;
        int at = (screenCols_ - wide - 2) / 2;
        if (at < 0) at = 0;
        int top = 3 + bodyRows_ / 3;
        if (top < 3) top = 3;
        if (answer > wide - 2) answer = wide - 2;
        row = static_cast<size_t>(top + 1);
        col = static_cast<size_t>(at + 3 + answer);
    } else if (menu_.dropped()) {
        // On the item it is standing on, inside the box's own left-hand line.
        row = menu_.item() + 3;
        col = menu_.titleAt(menu_.column()) + 2;
    } else if (focus_ == FocusTree) {
        row = 3 + (treeSel_ - treeOff_);
        col = 2;
    } else if (focus_ == FocusPanel) {
        row = static_cast<size_t>(3 + bodyRows_ + 1);
        col = 2;
    } else {
        row = 3 + (cy_ - rowoff_);
        // The left edge, then the project pane and the line beside it when
        // they are there, then the numbers, then how far along the line.
        col = (treeOpen_ ? static_cast<size_t>(treeCols_) + 3 : 2) +
              static_cast<size_t>(gutterCols_) + (rx_ - coloff_);
    }
    out += "\x1b[" + number(row) + ";" + number(col) + "H";
}

void Editor::refresh() {
    layout();
    clampCursor();
    scroll();

    std::string screen;
    drawMenuBar(screen);
    drawFrameTop(screen);
    drawBody(screen);
    drawPanel(screen);
    drawFrameFoot(screen);
    drawStatus(screen);
    drawMessage(screen);

    // Every part above writes whole rows and ends each one the same way, so the
    // screen can be taken apart again here rather than every part being asked
    // to hand back a list.
    std::vector<std::string> rows;
    size_t from = 0;
    for (;;) {
        size_t end = screen.find("\r\n", from);
        if (end == std::string::npos) {
            rows.push_back(screen.substr(from));
            break;
        }
        rows.push_back(screen.substr(from, end - from));
        from = end + 2;
    }
    present(rows);
}

// What is on the screen already is left alone. A keystroke changes one row and
// the status line, so those are what gets written - not the whole screen, which
// is what made the whole screen flicker.
void Editor::present(const std::vector<std::string>& rows) {
    std::string out;

    // "Show me this all at once", for the terminals that understand it. The
    // ones that do not skip it, as they skip anything else they do not know.
    out += "\x1b[?2026h\x1b[?25l";

    bool everything = painted_.size() != rows.size() || paintedCols_ != screenCols_;
    if (everything) {
        out += "\x1b[2J";
        painted_.assign(rows.size(), std::string());
        paintedCols_ = screenCols_;
    }

    for (size_t i = 0; i < rows.size(); ++i) {
        if (!everything && painted_[i] == rows[i]) continue;
        out += "\x1b[" + number(i + 1) + ";1H";
        out += rows[i];
        out += "\x1b[m\x1b[K";
    }
    painted_ = rows;

    // The menu and the question box lie over the screen rather than in it, so
    // what they covered is remembered as unwritten and comes back when they go.
    std::vector<size_t> covered;
    drawDropdown(out, covered);
    drawDialog(out, covered);
    for (size_t i = 0; i < covered.size(); ++i)
        if (covered[i] < painted_.size()) painted_[covered[i]].clear();

    placeCursor(out);
    out += "\x1b[?25h\x1b[?2026l";
    Terminal::write(out);
}

void Editor::moveCursor(int key) {
    const std::string& line = buf_.line(cy_);
    switch (key) {
        case KEY_ARROW_LEFT:
            // A whole character at a time, so the caret never lands inside one.
            if (cx_ > 0) cx_ = utf8::previous(line, cx_);
            else if (cy_ > 0) { --cy_; cx_ = buf_.line(cy_).size(); }
            break;
        case KEY_ARROW_RIGHT:
            if (cx_ < line.size()) cx_ = utf8::next(line, cx_);
            else if (cy_ + 1 < buf_.lineCount()) { ++cy_; cx_ = 0; }
            break;
        case KEY_ARROW_UP:   if (cy_ > 0) --cy_; break;
        case KEY_ARROW_DOWN: if (cy_ + 1 < buf_.lineCount()) ++cy_; break;
        case KEY_HOME:       cx_ = 0; break;
        case KEY_END:        cx_ = line.size(); break;
        case KEY_PAGE_UP:
            cy_ = (cy_ > static_cast<size_t>(bodyRows_)) ? cy_ - static_cast<size_t>(bodyRows_) : 0;
            break;
        case KEY_PAGE_DOWN:
            cy_ += static_cast<size_t>(bodyRows_);
            if (cy_ >= buf_.lineCount()) cy_ = buf_.lineCount() - 1;
            break;
        default: break;
    }
    clampCursor();
    buf_.breakRun();
}

void Editor::moveTree(int key) {
    if (tree_.size() == 0) return;
    size_t step = static_cast<size_t>(bodyRows_);
    switch (key) {
        case KEY_ARROW_UP:   if (treeSel_ > 0) --treeSel_; break;
        case KEY_ARROW_DOWN: if (treeSel_ + 1 < tree_.size()) ++treeSel_; break;
        case KEY_PAGE_UP:    treeSel_ = (treeSel_ > step) ? treeSel_ - step : 0; break;
        case KEY_PAGE_DOWN:
            treeSel_ += step;
            if (treeSel_ >= tree_.size()) treeSel_ = tree_.size() - 1;
            break;
        case KEY_HOME:       treeSel_ = 0; break;
        case KEY_END:        treeSel_ = tree_.size() - 1; break;
        case KEY_ARROW_LEFT:
        case KEY_ARROW_RIGHT:
            tree_.toggle(treeSel_);
            refreshTree();
            break;
        default: break;
    }
}

void Editor::movePanel(int key) {
    const std::vector<std::string>& lines = panelLines();
    size_t step = (key == KEY_PAGE_UP || key == KEY_PAGE_DOWN)
                      ? static_cast<size_t>(panelRows_) : 1;
    switch (key) {
        case KEY_ARROW_UP:
        case KEY_PAGE_UP:   panelOff_ = (panelOff_ > step) ? panelOff_ - step : 0; break;
        case KEY_ARROW_DOWN:
        case KEY_PAGE_DOWN: if (panelOff_ + step < lines.size()) panelOff_ += step; break;
        case KEY_HOME:      panelOff_ = 0; break;
        case KEY_END:       panelOff_ = lines.empty() ? 0 : lines.size() - 1; break;
        case KEY_ARROW_RIGHT:
            tab_ = static_cast<Tab>((tab_ + 1) % TabCount);
            panelOff_ = 0;
            break;
        case KEY_ARROW_LEFT:
            tab_ = static_cast<Tab>((tab_ + TabCount - 1) % TabCount);
            panelOff_ = 0;
            break;
        default: break;
    }
}

void Editor::cycleFocus() {
    for (int i = 0; i < 3; ++i) {
        focus_ = (focus_ == FocusText) ? FocusTree
                                       : (focus_ == FocusTree ? FocusPanel : FocusText);
        if (focus_ == FocusTree && !treeOpen_) continue;
        if (focus_ == FocusPanel && !panelOpen_) continue;
        break;
    }
}

void Editor::insertChar(char c) {
    buf_.beginEdit(EditTyping, cx_, cy_);
    buf_.insertChar(cy_, cx_, c);
    ++cx_;
}

bool Editor::selection(Range& range) const {
    if (!marked_) return false;
    if (markRow_ == cy_ && markCol_ == cx_) return false;
    range = ordered(markRow_, markCol_, cy_, cx_);
    return true;
}

bool Editor::selectionOn(size_t row, size_t& from, size_t& to) const {
    Range range;
    if (!selection(range)) return false;
    if (row < range.fromRow || row > range.toRow) return false;

    from = (row == range.fromRow) ? range.fromCol : 0;
    to = (row == range.toRow) ? range.toCol : buf_.line(row).size();
    return to > from;
}

void Editor::extendTo(int key) {
    // The first shifted movement puts the mark down where the caret was; the
    // rest just move, and the stretch between the two is what is selected.
    if (!marked_) {
        marked_ = true;
        markRow_ = cy_;
        markCol_ = cx_;
    }
    moveCursor(unshifted(key));
}

bool Editor::eraseSelection() {
    Range range;
    if (!selection(range)) return false;

    buf_.beginEdit(EditOther, cx_, cy_);
    buf_.eraseRange(range);
    cy_ = range.fromRow;
    cx_ = range.fromCol;
    marked_ = false;
    clampCursor();
    return true;
}

void Editor::copySelection(bool cut) {
    Range range;
    if (selection(range)) {
        clipboard_ = buf_.textIn(range);
        if (cut) eraseSelection();
        else marked_ = false;
        say(number(clipboard_.size()) + " characters " + (cut ? "cut" : "copied"));
        return;
    }

    // Nothing selected means the line the caret is on, which is what is nearly
    // always wanted and saves selecting it first.
    clipboard_ = buf_.line(cy_) + "\n";
    if (cut) {
        buf_.beginEdit(EditOther, cx_, cy_);
        if (buf_.lineCount() == 1) {
            buf_.replaceLine(0, std::string());
        } else {
            Range whole;
            whole.fromRow = cy_;
            whole.fromCol = 0;
            whole.toRow = cy_ + 1;
            whole.toCol = 0;
            buf_.eraseRange(whole);
        }
        cx_ = 0;
        clampCursor();
    }
    say(cut ? "line cut" : "line copied");
}

void Editor::pasteClipboard() {
    if (clipboard_.empty()) { say("there is nothing to paste"); return; }

    buf_.beginEdit(EditOther, cx_, cy_);
    eraseSelection();

    size_t endRow = cy_, endCol = cx_;
    buf_.insertText(cy_, cx_, clipboard_, endRow, endCol);
    cy_ = endRow;
    cx_ = endCol;
    marked_ = false;
    clampCursor();
    say(number(clipboard_.size()) + " characters pasted");
}

void Editor::selectAll() {
    marked_ = true;
    markRow_ = 0;
    markCol_ = 0;
    cy_ = buf_.lineCount() - 1;
    cx_ = buf_.line(cy_).size();
    focus_ = FocusText;
    say("all of it selected");
}

void Editor::undoEdit() {
    marked_ = false;
    if (!buf_.undo(cx_, cy_)) { say("nothing to undo"); return; }
    clampCursor();
    focus_ = FocusText;
    say("undone" + std::string(buf_.canUndo() ? "" : " - that was the last of it"));
}

void Editor::redoEdit() {
    if (!buf_.redo(cx_, cy_)) { say("nothing to redo"); return; }
    clampCursor();
    focus_ = FocusText;
    say("redone");
}

void Editor::insertNewline() {
    // A newline stands alone, so undo gives back a line at a time rather than
    // everything typed since the file was opened.
    buf_.beginEdit(EditOther, cx_, cy_);

    std::string lead = indentAfterNewline(buf_.lines(), cy_, cx_, style_);
    buf_.splitLine(cy_, cx_);

    // A line left holding nothing but spaces keeps none of them. Pressing enter
    // on an empty indented line should not write trailing whitespace into the
    // file: cc1 will never see the difference, but a diff will.
    if (buf_.line(cy_).find_first_not_of(" \t") == std::string::npos)
        buf_.replaceLine(cy_, std::string());

    ++cy_;
    // What followed the caret brings its own leading space with it; the level
    // is decided here, so that space is dropped rather than added to.
    buf_.replaceLine(cy_, lead + withoutLeadingSpace(buf_.line(cy_)));
    cx_ = lead.size();
}

void Editor::backspace() {
    buf_.beginEdit(EditErasing, cx_, cy_);
    if (cx_ > 0) {
        // The whole character, not its last byte - deleting half of one would
        // leave the file holding something that is not text.
        size_t start = utf8::previous(buf_.line(cy_), cx_);
        Range range;
        range.fromRow = range.toRow = cy_;
        range.fromCol = start;
        range.toCol = cx_;
        buf_.eraseRange(range);
        cx_ = start;
    } else if (cy_ > 0) {
        cx_ = buf_.line(cy_ - 1).size();
        buf_.joinLine(cy_ - 1);
        --cy_;
    }
}

void Editor::deleteForward() {
    buf_.beginEdit(EditErasing, cx_, cy_);
    if (cx_ < buf_.line(cy_).size()) {
        Range range;
        range.fromRow = range.toRow = cy_;
        range.fromCol = cx_;
        range.toCol = utf8::next(buf_.line(cy_), cx_);
        buf_.eraseRange(range);
    } else if (cy_ + 1 < buf_.lineCount()) {
        buf_.joinLine(cy_);
    }
}

void Editor::realign() {
    const std::string& line = buf_.line(cy_);
    size_t had = leadingSpace(line);
    std::string want = indentFor(buf_.lines(), cy_, style_);
    if (want == line.substr(0, had)) return;

    // Part of the keystroke that caused it, so undoing a typed brace takes the
    // line's indentation back with it in one go.
    buf_.beginEdit(EditTyping, cx_, cy_);
    buf_.replaceLine(cy_, want + line.substr(had));
    cx_ = (cx_ >= had) ? cx_ - had + want.size() : want.size();
}

void Editor::tabKey() {
    const std::string& line = buf_.line(cy_);
    size_t lead = leadingSpace(line);

    // In the leading space, tab means 'put this line where it belongs' rather
    // than 'add a step'. Anywhere else it is an ordinary indent.
    if (cx_ <= lead) {
        std::string want = indentFor(buf_.lines(), cy_, style_);
        buf_.beginEdit(EditOther, cx_, cy_);
        buf_.replaceLine(cy_, want + line.substr(lead));
        cx_ = want.size();
        return;
    }
    if (style_.tabs) { insertChar('\t'); return; }
    for (size_t i = 0; i < style_.width; ++i) insertChar(' ');
}

void Editor::findAgain(bool forwards) {
    if (needle_.empty()) { say("nothing to look for yet - Ctrl-F asks"); return; }

    // From one past the caret, so pressing it again moves on instead of
    // finding the same place for ever.
    Match match = forwards ? findNext(buf_.lines(), needle_, cy_, cx_ + 1)
                           : findPrevious(buf_.lines(), needle_, cy_, cx_);
    if (!match.found) { say(needle_ + " is not in this file"); return; }

    cy_ = match.row;
    cx_ = match.col;
    focus_ = FocusText;
    say(needle_ + " - line " + number(match.row + 1));
}

void Editor::findPrompt() {
    bool cancelled = false;
    std::string want = prompt("find: ", cancelled);
    // An empty answer is not a search for nothing, and it is not the last
    // search again either - looking on is what Ctrl-G is for, in both front
    // ends. It used to re-find here and do nothing at all in the window, which
    // is the sort of difference nobody discovers on purpose.
    if (cancelled || want.empty()) { say("nothing looked for"); return; }

    needle_ = want;
    // From the caret itself this time, so a word already under it is found.
    Match match = findNext(buf_.lines(), needle_, cy_, cx_);
    if (!match.found) { say(needle_ + " is not in this file"); return; }

    cy_ = match.row;
    cx_ = match.col;
    focus_ = FocusText;
    say(needle_ + " - line " + number(match.row + 1) + ", Ctrl-G for the next");
}

void Editor::replacePrompt() {
    bool cancelled = false;
    std::string want = prompt("replace: ", cancelled);
    if (cancelled || want.empty()) { say("nothing replaced"); return; }

    std::string with = prompt("replace " + want + " with: ", cancelled);
    if (cancelled) { say("nothing replaced"); return; }

    std::vector<std::string> lines = buf_.lines();
    size_t count = replaceAll(lines, want, with);
    if (count == 0) { say(want + " is not in this file"); return; }

    buf_.beginEdit(EditOther, cx_, cy_);
    buf_.replaceAll(lines);
    needle_ = with;
    clampCursor();

    say(number(count) + (count == 1 ? " change" : " changes") + " - Ctrl-Z puts them back");
}

// Lay out what is selected, or the whole file when nothing is.
//
// Even for a selection the whole file is laid out first, and only the selected
// lines are taken from the result. Indentation is not a property of a line but
// of everything above it - how deep the braces are, whether a comment is open -
// so laying out a fragment on its own would start at column zero and be wrong
// from the first line. What the selection decides is which lines are written
// back, not what is measured.
void Editor::reindentAll() {
    const std::vector<std::string> laid = reindent(buf_.lines(), style_);

    Range range;
    bool some = selection(range) && !range.empty();

    // A run that adds or removes lines is not one this can take a slice of, so
    // the selection is passed over and the whole file is laid out. The
    // indenter does not do that today; if it ever does, this stays honest.
    if (some && laid.size() == buf_.lineCount()) {
        size_t first = range.fromRow, last = range.toRow;
        if (last >= buf_.lineCount()) last = buf_.lineCount() - 1;

        // The selected lines, laid out; every other line exactly as it was.
        std::vector<std::string> kept = buf_.lines();
        size_t moved = 0;
        for (size_t row = first; row <= last; ++row)
            if (kept[row] != laid[row]) {
                kept[row] = laid[row];
                ++moved;
            }

        buf_.beginEdit(EditOther, cx_, cy_);
        buf_.replaceAll(kept);
        clampCursor();
        cx_ = leadingSpace(buf_.line(cy_));
        size_t howMany = last - first + 1;
        say("laid out " + number(howMany) + (howMany == 1 ? " line" : " lines") +
            " of the selection - " + number(moved) + " moved");
        return;
    }

    buf_.beginEdit(EditOther, cx_, cy_);
    buf_.replaceAll(laid);
    clampCursor();
    cx_ = leadingSpace(buf_.line(cy_));
    say("laid out - " + lineCountText(buf_.lineCount()));
}

bool Editor::save() {
    if (buf_.path().empty()) {
        bool cancelled = false;
        std::string name = prompt("save as: ", cancelled);
        if (cancelled || name.empty()) { say("not saved"); return false; }
        buf_.setPath(name);
    }

    std::string error;
    if (!buf_.save(error)) { say(error); return false; }
    buf_.breakRun();
    say(buf_.path() + " written");
    refreshTree();
    return true;
}

void Editor::saveAs() {
    bool cancelled = false;
    std::string name = prompt("save as: ", cancelled);
    if (cancelled || name.empty()) { say("not saved"); return; }
    buf_.setPath(name);
    save();
}

void Editor::openPrompt() {
    bool cancelled = false;
    std::string name = prompt("open: ", cancelled);
    if (cancelled || name.empty()) { say("not opened"); return; }
    open(name);
}

void Editor::newFile() {
    stash();
    docs_.push_back(Document());
    doc_ = docs_.size() - 1;
    restore();
    lang_ = LangPlain;
    say("new file - Ctrl-S names it");
}

void Editor::openSelected() {
    if (treeSel_ >= tree_.size()) return;
    const TreeEntry& e = tree_.entries()[treeSel_];
    if (e.directory) {
        tree_.toggle(treeSel_);
        refreshTree();
        return;
    }
    open(e.path);
    focus_ = FocusText;
}

// What the file commands act on: whatever the project pane is standing on when
// it has the keyboard, and the file being edited otherwise.
std::string Editor::targetFile() const {
    if (focus_ == FocusTree && treeSel_ < tree_.size()) {
        const TreeEntry& e = tree_.entries()[treeSel_];
        if (!e.directory) return e.path;
    }
    return buf_.path();
}

std::string Editor::groupUnderCursor() const {
    if (!project_.loaded()) return std::string();
    for (size_t i = treeSel_ + 1; i-- > 0;)
        if (i < tree_.size() && tree_.entries()[i].group) return tree_.entries()[i].name;
    return std::string();
}

void Editor::createFile() {
    bool cancelled = false;
    std::string name = prompt("new file: ", cancelled);
    if (cancelled || name.empty()) { say("nothing made"); return; }

    Outcome done = editor::createFile(project_, name, groupUnderCursor());
    say(done.message);
    if (!done.ok) return;

    refreshTree();
    open(done.path);
}

void Editor::renameFile() {
    std::string path = targetFile();
    if (path.empty()) { say("no file to rename"); return; }

    std::string was = project_.relative(path);
    bool cancelled = false;
    std::string name = prompt("rename " + was + " to: ", cancelled);
    if (cancelled || name.empty()) { say("not renamed"); return; }

    Outcome done = editor::renameFile(project_, path, name);
    say(done.message);
    if (!done.ok) return;

    // A file open in a tab has to follow its own name, or saving would write
    // the old one back. Compared by what the paths resolve to rather than by
    // how they were written: the pane hands out one spelling and the edit view
    // holds another, and a tab that misses here goes on wearing the old name
    // with nothing said.
    for (size_t i = 0; i < docs_.size(); ++i) {
        Buffer& b = (i == doc_) ? buf_ : docs_[i].buf;
        if (!b.path().empty() && path::same(b.path(), path)) b.setPath(done.path);
    }

    // So do its breakpoints. They are filed under the file's name, so a rename
    // without this leaves them under a name nothing asks for again: the marks
    // vanish from the gutter, and a debugger started afterwards is told to stop
    // in a file that is no longer there.
    std::map<std::string, FileBreaks>::iterator had = breaks_.find(path::oneName(path));
    if (had != breaks_.end()) {
        std::set<size_t> lines = had->second.lines;
        breaks_.erase(had);
        FileBreaks& now = breaks_[path::oneName(done.path)];
        now.path = done.path;
        now.lines = lines;
    }

    applyLanguage();
    refreshTree();
}

void Editor::deleteFile() {
    std::string path = targetFile();
    if (path.empty()) { say("no file to delete"); return; }

    std::string shown = project_.relative(path);

    // Typed in full, on purpose. This is the one command here that cannot be
    // undone, and a single keypress is not enough to ask for it.
    bool cancelled = false;
    std::string answer = prompt("delete " + shown + " from disk? type yes: ", cancelled);
    if (cancelled || answer != "yes") { say("not deleted"); return; }

    Outcome done = editor::deleteFile(project_, path);
    say(done.message);
    if (!done.ok) return;

    for (size_t i = 0; i < docs_.size(); ++i) {
        Buffer& b = (i == doc_) ? buf_ : docs_[i].buf;
        if (!b.path().empty() && path::same(b.path(), path)) b.setPath(std::string());
    }

    // Its breakpoints go with it. A file that is not there cannot be stopped
    // in - and a name can come back, so leaving them would hand lines somebody
    // set in this file to whatever is written under the name next.
    breaks_.erase(path::oneName(path));

    refreshTree();
}

void Editor::regroupFile() {
    if (!project_.loaded()) { say("there is no project to regroup in"); return; }

    std::string path = targetFile();
    if (path.empty()) { say("no file to move"); return; }

    bool cancelled = false;
    std::string group = prompt("move " + project_.relative(path) + " to group: ", cancelled);
    if (cancelled || group.empty()) { say("not moved"); return; }

    Outcome done = editor::moveToGroup(project_, path, group);
    say(done.message);
    if (done.ok) refreshTree();
}

void Editor::addToProject() {
    if (!project_.loaded()) { say("there is no project - make one first"); return; }
    if (buf_.path().empty()) { say("save the file first, so it has a name"); return; }

    bool cancelled = false;
    std::string group =
        prompt("add " + project_.relative(buf_.path()) + " to group [Sources]: ", cancelled);
    if (cancelled) { say("not added"); return; }

    Outcome done = editor::addExisting(project_, buf_.path(), group);
    say(done.message);
    if (done.ok) refreshTree();
}

void Editor::newProject() {
    bool cancelled = false;
    std::string name = prompt("project name: ", cancelled);
    if (cancelled || name.empty()) { say("no project made"); return; }

    Outcome done = editor::beginProject(project_, projectDir_.empty() ? "." : projectDir_,
                                        name, buf_.path());
    say(done.message);
    if (done.ok) refreshTree();
}

void Editor::saveProject() {
    Outcome done = editor::saveProject(project_);
    say(done.message);
}

void Editor::resetDebug() {
    // Not a debugger, and it does not pretend to be one - but the reason has
    // changed. cc1 writes DWARF for two of the three targets now, so on those
    // there is something for a debugger to read; what there is not is a program
    // to read it against, since this builds to assembly and stops there. The
    // words come from the core so that the window says the same ones.
    debug_.clear();
    ToolchainKind kind = resolve(tool_, lang_);

    std::vector<std::string> note = debugNote(kind, kArches[arch_]);
    for (size_t i = 0; i < note.size(); ++i) debug_.push_back(note[i]);
    debug_.push_back("");

    std::vector<std::string> said = describe(symbolsIn(assembly_));
    for (size_t i = 0; i < said.size(); ++i) debug_.push_back(said[i]);

    debug_.push_back("");
    debug_.push_back("target: " + std::string(kArches[arch_]));
    debug_.push_back("build:  " + std::string(configName(config_)) + " (" +
                     configFlags(kind, config_, kArches[arch_]) + " )");
}

// The frame the cursor is on, which is the panel's top line: with the panel
// focused the terminal leaves the cursor at the start of that row, so the line
// it is standing on is the line to act on, and moving down the panel moves it.
// Nothing new is drawn to say which line is picked, because the cursor is
// already sitting on it.
//
// This goes to where the call came from and no further: the program is still
// standing where it stopped, the arrow in the gutter still marks that line, and
// the variables are still that frame's. Going to a line is not stepping.
void Editor::goToFrame() {
    const std::vector<std::string>& lines = panelLines();
    if (panelOff_ >= lines.size()) { say("there is nothing on that line"); return; }

    size_t which = dbg_frameOnLine(stack_, lines[panelOff_]);

    // The top line names the frame the program stopped in, which is the way
    // back from a caller: enter on it is enter on frame 0, and it is where the
    // cursor is already standing once a frame has been looked at.
    if (which >= stack_.size() && !stack_.empty() &&
        lines[panelOff_] == dbg_stopLine(stopFile_, stopLine_, stopFunction_))
        which = 0;

    if (which >= stack_.size()) {
        // Not a frame, but the tab's other kind of line is a variable, and
        // pressing enter on one of those is how it is set.
        size_t variable = dbg_variableOnLine(locals_, lines[panelOff_]);
        if (variable < locals_.size()) { editVariable(variable); return; }

        size_t watch = dbg_watchOnLine(debugger_.watches(), lines[panelOff_]);
        if (watch < debugger_.watches().size()) { editWatch(watch); return; }

        say("that line is neither a frame nor a variable nor a watch");
        return;
    }

    lookAt(which);
}

// An expression to keep asking about, read again wherever the program gets to
// next. Asked for in the same box as everything else, and answered straight
// away when there is something to answer it against.
void Editor::watchExpression() {
    bool cancelled = false;
    std::string what = prompt("watch: ", cancelled);
    if (cancelled || what.empty()) { say("nothing to watch"); return; }

    // A watch is an expression handed to a debugger to work out, and a
    // Shalimar program has nothing to hand it to: it reports where it is, not
    // what is in it. Refusing plainly beats accepting one and showing it blank
    // for the rest of the session.
    if (shm_.running()) {
        say("a Shalimar program says where it is, not what is in it - nothing to watch with");
        return;
    }

    debugger_.addWatch(what);
    panelOpen_ = true;
    tab_ = TabDebug;
    if (stopLine_ > 0) writeDebugTab();
    say(debugger_.running() ? "watching " + what
                            : "watching " + what + " - it is read when the program stops");
}

// Changing one, or taking it away: the box comes up with nothing in it, and an
// empty answer is how a watch is removed. There is no other list here that
// needs a key for taking something out of it, so it does not get one.
void Editor::editWatch(size_t which) {
    if (which >= debugger_.watches().size()) return;
    const std::string was = debugger_.watches()[which].expression;

    bool cancelled = false;
    std::string what = prompt("watch " + was + ", or empty to drop it: ", cancelled);
    if (cancelled) { say(was + " is still watched"); return; }

    debugger_.setWatch(which, what);
    writeDebugTab();
    say(what.empty() ? "stopped watching " + was : "watching " + what);
}

// Writing a variable back, into whichever frame is being looked at. The value
// is asked for in the same box that asks for a filename, and what the debugger
// says about a value it will not take is what the message line says: its
// complaint names the mistake better than anything invented here.
void Editor::editVariable(size_t which) {
    if (which >= locals_.size()) return;

    const std::string name = locals_[which].name;
    const std::string was = locals_[which].value;

    bool cancelled = false;
    std::string value = prompt("set " + name + " (" + was + ") to: ", cancelled);
    if (cancelled || value.empty()) { say(name + " is still " + was); return; }

    std::string said;
    if (!debugger_.setVariable(name, value, &said)) {
        say(said.empty() ? "the debugger would not set " + name : said);
        return;
    }

    locals_ = debugger_.locals();
    writeDebugTab();

    // Read back rather than assumed: a debugger may take "3.7" for an int and
    // store 3, and the tab should say what is in there rather than what was
    // typed at it.
    std::string now = value;
    for (size_t i = 0; i < locals_.size(); ++i)
        if (locals_[i].name == name) now = locals_[i].value;
    say(name + " is " + now + " now");
}

// One frame along, without going near the panel: Ctrl-Up towards what called
// this, Ctrl-Down back towards where the program stopped. The same act as
// pressing enter on the frame, reached from the text where the caret already
// is - which is where a person is when the question occurs to them.
void Editor::lookAlongStack(int by) {
    if (!debugging() || stack_.empty()) {
        say("nothing is stopped, so there is no stack to walk");
        return;
    }
    // Shalimar knows how deep it is and not what it is standing in - the
    // compiler emits no table of callers - so frames() gives back one frame
    // that says the depth. There is nothing to walk to, and saying so is
    // better than the "nothing called ..." below, which would name that
    // depth as if it were a function.
    if (shm_.running()) {
        say("a Shalimar program reports how deep it is, not what called it");
        return;
    }
    if (by > 0) {
        if (looking_ + 1 >= stack_.size()) {
            say("nothing called " + stack_[stack_.size() - 1].function + ", which is the top");
            return;
        }
        lookAt(looking_ + 1);
        return;
    }
    if (looking_ == 0) {
        say("this is where the program stopped - there is nothing below it");
        return;
    }
    lookAt(looking_ - 1);
}

// Looking at a frame: its variables are read, the tab is written again with it
// marked, and the caret goes to the line that is waiting for the call.
void Editor::lookAt(size_t which) {
    if (which >= stack_.size()) return;

    if (!shm_.running() && !debugger_.selectFrame(which)) {
        say("the debugger would not go to that frame");
        return;
    }
    looking_ = which;
    if (!shm_.running()) locals_ = debugger_.locals();
    writeDebugTab();
    panelOff_ = 0;

    const StackFrame& frame = stack_[which];
    if (!frame.file.empty() && path::filename(frame.file) != path::filename(buf_.path())) {
        std::string full = whereThatFileIs(frame.file);
        if (full.empty()) {
            say(path::filename(frame.file) + " is not in this project");
            return;
        }
        open(full);
    }

    if (frame.line > 0) {
        cy_ = frame.line - 1;
        if (cy_ >= buf_.lineCount()) cy_ = buf_.lineCount() - 1;
        cx_ = 0;
        clampCursor();
    }
    focus_ = FocusText;
    say(which == 0 ? path::filename(frame.file) + ":" + number(frame.line) + " in " +
                         frame.function + " - back where it stopped"
                   : path::filename(frame.file) + ":" + number(frame.line) + " in " +
                         frame.function + " - where the call came from");
}

void Editor::goToProblem() {
    if (!lastDiag_.present) { say("no error to go to"); return; }
    cy_ = lastDiag_.line - 1;
    if (cy_ >= buf_.lineCount()) cy_ = buf_.lineCount() - 1;
    cx_ = lastDiag_.col - 1;
    clampCursor();
    focus_ = FocusText;
    say(number(lastDiag_.line) + ":" + number(lastDiag_.col) + ": " + lastDiag_.message);
}

void Editor::compile() {
    // Which compiler runs is decided here, from the language, unless the
    // choice was taken by hand. Said before anything else so that a file that
    // cannot be compiled at all is turned away with a reason rather than with
    // a wall of somebody else's parse errors.
    ToolchainKind kind = resolve(tool_, lang_);
    if (!canCompile(kind, lang_)) {
        say(refusal(kind, lang_));
        return;
    }

    // cc1 reads a file, not a screen. Anything unsaved would not be in the
    // build, so saving is part of building rather than something to remember.
    if (buf_.dirty() || buf_.path().empty()) {
        if (!save()) return;
    }

    panelOpen_ = true;
    tab_ = TabConsole;
    console_.clear();
    // Shown with the compiler's own name and the file as the project knows it,
    // rather than two absolute paths that push the flags off the right of an
    // eighty-column console. The flags are the part worth reading.
    Toolchain shownAs = tool_;
    shownAs.cc1 = baseName(tool_.cc1);
    shownAs.cl = baseName(tool_.cl);
    std::string shownFile = project_.loaded() ? project_.relative(buf_.path())
                                              : baseName(buf_.path());
    console_.push_back("$ " + shownCommand(shownAs, kind, shownFile, lang_,
                                           kArches[arch_], config_));
    panelOff_ = 0;
    say(std::string("building ") + configName(config_) + " with " + toolchainName(kind) +
        (usesArch(kind) ? std::string(" for ") + kArches[arch_] : std::string()) +
        " ...");
    refresh();

    Build result = build(tool_, kind, buf_.path(), lang_, kArches[arch_], config_,
                         consoleSink, this);

    assembly_ = result.asmLines;
    lastDiag_ = result.diag;
    resetDebug();

    // cc1's own words are already in the console, caret line and all, because
    // they arrived there as it ran - the marker sits under the column it means,
    // which a one-line summary would throw away.
    if (result.diag.present) {
        // Land on the error. cc1 counts lines and columns from one; the buffer
        // counts from zero.
        cy_ = result.diag.line - 1;
        if (cy_ >= buf_.lineCount()) cy_ = buf_.lineCount() - 1;
        cx_ = result.diag.col - 1;
        clampCursor();
        focus_ = FocusText;
        tab_ = TabConsole;
        console_.push_back("");
        console_.push_back("[enter] here goes to the line above");
        if (console_.size() > static_cast<size_t>(panelRows_))
            panelOff_ = console_.size() - static_cast<size_t>(panelRows_);
        say(number(result.diag.line) + ":" + number(result.diag.col) + ": error: " +
            result.diag.message);
    } else if (!result.ok) {
        console_.push_back(std::string(toolchainName(kind)) + " failed");
        say(std::string(toolchainName(kind)) + " failed - see the console");
    } else {
        console_.push_back("ok - " + number(assembly_.size()) + " lines of assembly");
        tab_ = TabAssembly;
        panelOff_ = 0;
        say(number(assembly_.size()) + " lines of " +
            (usesArch(kind) ? kArches[arch_] : toolchainName(kind)) + " assembly");
    }
}

// Building is one thing and running is another, and the console has to keep
// them apart: a compiler that failed and a program that returned 1 look the
// same to anything that only asks whether the last command worked.
void Editor::buildAndRun() {
    ToolchainKind kind = resolve(tool_, lang_);
    if (!canCompile(kind, lang_)) {
        say(refusal(kind, lang_));
        return;
    }
    if (!runsHere(kind, kArches[arch_])) {
        say(whyNotRun(kind, kArches[arch_]));
        return;
    }

    if (buf_.dirty() || buf_.path().empty()) {
        if (!save()) return;
    }

    panelOpen_ = true;
    tab_ = TabConsole;
    console_.clear();

    Toolchain shownAs = tool_;
    shownAs.cc1 = baseName(tool_.cc1);
    shownAs.cl = baseName(tool_.cl);
    std::string shownFile = project_.loaded() ? project_.relative(buf_.path())
                                              : baseName(buf_.path());
    console_.push_back("$ " + shownProgramCommand(shownAs, kind, shownFile, lang_,
                                                  kArches[arch_], config_));
    panelOff_ = 0;
    say(std::string("building and running with ") + toolchainName(kind) + " ...");
    refresh();

    Ran result = runProgram(tool_, kind, buf_.path(), lang_, kArches[arch_], config_,
                            consoleSink, this);

    lastDiag_ = result.diag;

    if (result.diag.present) {
        cy_ = result.diag.line - 1;
        if (cy_ >= buf_.lineCount()) cy_ = buf_.lineCount() - 1;
        cx_ = result.diag.col - 1;
        clampCursor();
        focus_ = FocusText;
        console_.push_back("");
        console_.push_back("[enter] here goes to the line above");
        say(number(result.diag.line) + ":" + number(result.diag.col) + ": error: " +
            result.diag.message);
    } else if (!result.built) {
        console_.push_back(std::string(toolchainName(kind)) + " built no program");
        say(std::string(toolchainName(kind)) + " built no program - see the console");
    } else {
        // What it returned, said as a number rather than as success or failure,
        // because only the program knows which of those its number meant.
        console_.push_back("");
        console_.push_back("[program returned " + number(static_cast<size_t>(result.status)) + "]");
        say("ran " + shownFile + " - it returned " + number(static_cast<size_t>(result.status)));
    }

    if (console_.size() > static_cast<size_t>(panelRows_))
        panelOff_ = console_.size() - static_cast<size_t>(panelRows_);
}

// Everything with a name and an unsaved change, written out. A project build
// reads several files off the disk, so "save the one in front of you" - which
// is all a single-file build ever needed - would build yesterday's copy of
// every other one.
bool Editor::saveEveryDirty() {
    stash();
    for (size_t i = 0; i < docs_.size(); ++i) {
        if (!docs_[i].buf.dirty() || docs_[i].buf.path().empty()) continue;
        std::string error;
        if (!docs_[i].buf.save(error)) {
            say(error);
            restore();
            return false;
        }
        docs_[i].buf.breakRun();
    }
    restore();
    return true;
}

// The compilers a target takes, named in the order its groups are. One is the
// ordinary case and reads exactly as it always did; two is what the console
// line says when C and C++ are in one program, and saying "cc1" there while cl
// was also running would be the console telling half the truth.
std::string Editor::compilersNamed(const std::vector<Part>& parts) const {
    std::vector<std::string> named;
    for (size_t i = 0; i < parts.size(); ++i) {
        std::string word = toolchainShown(tool_, toolchainOf(tool_, parts[i]));
        bool already = false;
        for (size_t j = 0; j < named.size(); ++j)
            if (named[j] == word) already = true;
        if (!already) named.push_back(word);
    }
    std::string all = named.empty() ? std::string() : named[0];
    for (size_t i = 1; i < named.size(); ++i)
        all += (i + 1 == named.size() ? " and " : ", ") + named[i];
    return all;
}

// The project's program: the sources its build entry names, compiled and
// linked into one thing, left beside the project file where it can be found
// afterwards.
//
// This never asks what is in the edit view, and Ctrl-B never asks what the
// project says. Which one you meant is said by which one you pressed - there
// is nothing here that has to be guessed, and nothing that has to be closed
// before the other will work.
void Editor::buildProject(bool andRun) {
    std::vector<Part> parts;
    std::string why, detail;
    if (!project_.targetParts(parts, why, &detail)) {
        // The line says what is wrong and the console says what to do about
        // it: the message line is one line wide and clips what will not fit.
        say(why);
        if (!detail.empty()) {
            panelOpen_ = true;
            tab_ = TabConsole;
            console_.clear();
            console_.push_back(why);
            console_.push_back("");
            wrapInto(console_, detail, static_cast<size_t>(screenCols_ - 4));
            panelOff_ = 0;
        }
        return;
    }

    // Every part is asked, not just the first: a target of C and C++ has two
    // compilers in it and either of them may be the one that cannot take what
    // it was given, or cannot produce something this machine can run.
    for (size_t i = 0; i < parts.size(); ++i) {
        ToolchainKind kind = toolchainOf(tool_, parts[i]);
        if (!canCompile(kind, parts[i].lang)) { say(refusal(kind, parts[i].lang)); return; }
        if (andRun && !runsHere(kind, kArches[arch_])) {
            say(whyNotRun(kind, kArches[arch_]));
            return;
        }
    }

    if (!saveEveryDirty()) return;

    panelOpen_ = true;
    tab_ = TabConsole;
    console_.clear();

    std::string program = project_.targetProgram();
    size_t count = 0;
    for (size_t i = 0; i < parts.size(); ++i) count += parts[i].sources.size();

    console_.push_back("$ " + compilersNamed(parts) + " " + number(count) +
                       (count == 1 ? " source -o " : " sources -o ") +
                       project_.relative(program));
    for (size_t i = 0; i < parts.size(); ++i)
        for (size_t f = 0; f < parts[i].sources.size(); ++f)
            console_.push_back("    " + project_.relative(parts[i].sources[f]));
    panelOff_ = 0;
    say("building " + baseName(program) + " with " + compilersNamed(parts) + " ...");
    refresh();

    Built made = buildParts(tool_, parts, kArches[arch_], config_, program,
                            consoleSink, this);

    // For the messages below, which are about a compiler and are still true
    // when there was more than one: whichever one stopped is the one whose
    // diagnostic came back, and the words name them all rather than guess.
    const std::string compilers = compilersNamed(parts);

    lastDiag_ = made.diag;

    if (made.diag.present) {
        // The error is as likely as not in a file that is not open, so it is
        // opened before the caret is put on the line. A build of several files
        // that lands you on the wrong one is worse than one that lands you
        // nowhere.
        std::string where = made.diag.file;
        if (!where.empty() && !path::exists(where)) where = project_.absolute(where);
        if (!where.empty() && path::exists(where) && where != buf_.path()) open(where);

        cy_ = made.diag.line > 0 ? made.diag.line - 1 : 0;
        if (cy_ >= buf_.lineCount()) cy_ = buf_.lineCount() - 1;
        cx_ = made.diag.col > 0 ? made.diag.col - 1 : 0;
        clampCursor();
        focus_ = FocusText;
        say(baseName(made.diag.file) + ":" + number(made.diag.line) + ":" +
            number(made.diag.col) + ": error: " + made.diag.message);
    } else if (!made.ok) {
        say(compilers + " did not build it - see the console");
    } else if (!andRun) {
        console_.push_back("");
        console_.push_back("[built " + program + "]");
        say("built " + project_.relative(program) + " from " + number(count) +
            (count == 1 ? " source" : " sources"));
    } else {
        console_.push_back("");
        Ran result = runBuilt(program, consoleSink, this);
        console_.push_back("[program returned " + number(static_cast<size_t>(result.status)) + "]");
        say("ran " + project_.relative(program) + " - it returned " +
            number(static_cast<size_t>(result.status)));
    }

    if (console_.size() > static_cast<size_t>(panelRows_))
        panelOff_ = console_.size() - static_cast<size_t>(panelRows_);
}

bool Editor::breakpointOn(size_t line) const {
    if (buf_.path().empty()) return false;
    std::map<std::string, FileBreaks>::const_iterator found =
        breaks_.find(path::oneName(buf_.path()));
    if (found == breaks_.end()) return false;
    return found->second.lines.count(line) > 0;
}

void Editor::toggleBreak() {
    if (buf_.path().empty()) { say("save the file first - a breakpoint is on a line of a file"); return; }

    size_t line = cy_ + 1;   // the debugger counts from one, the buffer from zero
    FileBreaks& file = breaks_[path::oneName(buf_.path())];
    if (file.path.empty()) file.path = buf_.path();
    std::set<size_t>& here = file.lines;
    if (here.count(line)) {
        here.erase(line);
        if (debugging()) {
            // The whole set is put back rather than one taken away: neither
            // debugger promises the numbering of what it hands out, and there
            // are never enough breakpoints here for it to matter. A Shalimar
            // session does promise it - a breakpoint there is a file and a
            // line and nothing hands a number back - but it is put back the
            // same way, because one rule that is right everywhere beats two
            // that are each right in one place.
            if (shm_.running()) {
                shm_.clearBreakpoints();
                for (std::set<size_t>::iterator it = here.begin(); it != here.end(); ++it)
                    shm_.breakAt(file.path, *it);
            } else {
                debugger_.clearBreakpoints();
                for (std::set<size_t>::iterator it = here.begin(); it != here.end(); ++it)
                    debugger_.breakAt(file.path, *it);
            }
        }
        say("breakpoint off line " + number(line));
        return;
    }

    here.insert(line);
    if (shm_.running()) shm_.breakAt(file.path, line);
    else if (debugger_.running()) debugger_.breakAt(file.path, line);
    say("breakpoint on line " + number(line));
}

// A debugger names the file it stopped in the way its own line table spells it
// - "main.c" from lldb here, an absolute path from gdb - and neither is
// necessarily something that can be opened from where the editor is standing.
// So it is looked for: as given, under the project's root, and then among the
// project's own files by name, which is what finds src/main.c when all that
// was said is main.c.
std::string Editor::whereThatFileIs(const std::string& named) const {
    if (named.empty()) return std::string();
    if (path::exists(named)) return named;

    std::string underRoot = project_.absolute(named);
    if (path::exists(underRoot)) return underRoot;

    std::string leaf = path::filename(named);
    const std::vector<Group>& groups = project_.groups();
    for (size_t g = 0; g < groups.size(); ++g)
        for (size_t f = 0; f < groups[g].files.size(); ++f)
            if (path::filename(groups[g].files[f]) == leaf) {
                std::string full = project_.absolute(groups[g].files[f]);
                if (path::exists(full)) return full;
            }
    return std::string();
}

// Whatever a debugger printed, as lines. A string with newlines in it put into
// the panel writes them to the screen, which walks the cursor out of the box
// the panel is drawn in and leaves the frame in pieces.
void Editor::sayIfSettingsWereBad() {
    std::string kept = settings::setAside();
    if (kept.empty()) return;
    say("bad configuration file - kept as " + baseName(kept) + ", a new one made");
}

void Editor::sayLines(std::vector<std::string>& into, const std::string& text) {
    size_t from = 0;
    while (from <= text.size()) {
        size_t end = text.find('\n', from);
        std::string line = text.substr(from, end == std::string::npos ? std::string::npos
                                                                      : end - from);
        while (!line.empty() && line[line.size() - 1] == '\r') line.resize(line.size() - 1);
        if (!line.empty()) into.push_back(line);
        if (end == std::string::npos) break;
        from = end + 1;
    }
}

void Editor::showStop(const Stop& where) {
    panelOpen_ = true;
    tab_ = TabDebug;
    panelOff_ = 0;
    debug_.clear();

    // What the program printed on its way here goes to the console, where the
    // program's output goes when it is run without a debugger. It arrives on
    // the debugger's own stream mixed with the debugger's words, so it has to
    // be taken out of them - see dbg_programOutput. Stepping over a printf now
    // shows the line it printed, which is most of what stepping is for.
    // A Shalimar session needs none of that taking apart. Its channel keeps
    // the program's standard output away from the protocol on standard error,
    // so `said` is already the program's own printing and nothing else - which
    // is the whole reason that channel is not editor::Process.
    const std::string printed = debuggingShalimar()
                                    ? where.said
                                    : dbg_programOutput(debugger_.kind(), where.said);
    if (!printed.empty()) sayLines(console_, printed);

    if (where.exited) {
        stopFile_.clear();
        stopLine_ = 0;
        locals_.clear();
        stack_.clear();
        looking_ = 0;
        debug_.push_back("the program ran to the end and returned " + number(static_cast<size_t>(where.status)));
        debug_.push_back("");
        debug_.push_back("F8 starts it again. The breakpoints are still where you put them.");
        debugStop();
        say("the program returned " + number(static_cast<size_t>(where.status)));
        return;
    }

    if (!where.stopped) {
        // A step off the end of main lands in the loader, which has no source
        // here - lldb reports "dyld`start + 7000" and gdb something like it.
        // That is a real place to be standing and not a failure, so it is not
        // reported as one: the debugger is left running, and F8 or Stop
        // debugging both do what they say.
        if (dbg_stoppedWithNoSource(where.said)) {
            stopFile_.clear();
            stopLine_ = 0;
            locals_.clear();
            stack_.clear();
            looking_ = 0;
            debug_.push_back("stopped where there is no source to show");
            debug_.push_back("");
            debug_.push_back("Stepping past the end of main arrives in the code that");
            debug_.push_back("started it, which was not compiled here. F8 carries on to");
            debug_.push_back("the end, and Stop debugging leaves it.");
            debug_.push_back("");
            sayLines(debug_, where.said);
            say("stopped where there is no source - F8 carries on");
            return;
        }

        debug_.push_back("the debugger stopped answering");
        debug_.push_back("");
        sayLines(debug_, where.said);
        debugStop();
        say("the debugger stopped answering - see the Debug tab");
        return;
    }

    stopFile_ = where.file;
    stopLine_ = where.line;

    // The file it stopped in is opened if it is not open already, and then the
    // caret follows it.
    //
    // This used to move the caret only when the file was the one already in
    // front of you, on the grounds that jumping to a line of a file that is
    // not open would be a lie about where you are. It would - but the answer
    // is to open it, not to look away: stepping out of one file of a project
    // into another said "stopped at main.c:13" while showing circle.c, which
    // is a stranger lie than the one being avoided.
    if (!where.file.empty() && path::filename(where.file) != path::filename(buf_.path())) {
        std::string full = whereThatFileIs(where.file);
        if (!full.empty()) open(full);
    }

    // Shalimar has no variables to read - the compiler emits no table of a
    // function's names against its frame slots, and docs/DEBUGGING.md in
    // Compiler-S says so plainly rather than leaving it to be discovered. An
    // empty list is the truth here, and the tab says why below.
    if (debuggingShalimar()) {
        locals_.clear();
        stack_ = shm_.frames();
    } else {
        locals_ = debugger_.locals();
        stack_ = debugger_.frames();
    }

    if (path::filename(where.file) == path::filename(buf_.path()) && where.line > 0) {
        cy_ = where.line - 1;
        if (cy_ >= buf_.lineCount()) cy_ = buf_.lineCount() - 1;
        cx_ = 0;
        clampCursor();
        focus_ = FocusText;
    }

    stopFunction_ = where.function;
    looking_ = 0;
    writeDebugTab();

    say(path::filename(where.file) + ":" + number(where.line) +
        (where.function.empty() ? std::string() : " in " + where.function));
}

// The Debug tab, written from what is known about the stop rather than from
// the stop itself - so that it can be written again when the frame being
// looked at changes, without the program having moved.
void Editor::writeDebugTab() {
    debug_.clear();
    debug_.push_back(dbg_stopLine(stopFile_, stopLine_, stopFunction_));
    debug_.push_back("");

    // Whose variables these are, when they are not the ones the program
    // stopped among. Without it the line above stands over another function's
    // locals and the two contradict each other.
    if (looking_ > 0 && looking_ < stack_.size()) {
        debug_.push_back(dbg_lookingAt(stack_[looking_]));
        debug_.push_back("");
    }

    if (locals_.empty()) {
        // Empty means two different things. Under a debugger it means this
        // place has no variables; under a Shalimar session it means no place
        // ever will, because the compiler emits no table of a function's names
        // against its frame slots. Saying which is the difference between a
        // gap and a decision, and the second is not something to keep
        // rediscovering.
        debug_.push_back(debuggingShalimar()
                             ? "  (a Shalimar program says where it is, not what is in it)"
                             : "  (nothing in scope here)");
    } else {
        for (size_t i = 0; i < locals_.size(); ++i)
            debug_.push_back(dbg_variableLine(locals_[i]));
    }

    // The expressions being watched, which are the editor's own question
    // rather than the debugger's list of what is in scope - so they are their
    // own block, under the variables.
    const std::vector<Watch>& watching = debugger_.watches();
    if (!watching.empty()) {
        debug_.push_back("");
        debug_.push_back("watching");
        for (size_t i = 0; i < watching.size(); ++i)
            debug_.push_back(dbg_watchLine(watching[i]));
    }

    // Who is waiting for it. The first frame is where it is standing, which
    // the line at the top already says, so what is worth showing is what is
    // above that - and a program standing in main has nothing above it.
    if (stack_.size() > 1) {
        debug_.push_back("");
        debug_.push_back("called from");
        for (size_t i = 1; i < stack_.size(); ++i)
            debug_.push_back(dbg_frameLine(stack_[i], i == looking_));
    }

    debug_.push_back("");
    debug_.push_back("F8 carries on   F7 steps over   F6 steps into   F9 sets a breakpoint");

    // Setting a variable needs no stack at all, so it is said whether or not
    // there is one: a program standing in main has one frame and variables
    // like any other. None of that is true of Shalimar, and offering the keys
    // for it would be the panel promising what pressing them refuses.
    if (debuggingShalimar()) return;

    debug_.push_back("Ctrl-W puts the cursor in the panel; Enter on a variable sets it");
    if (!watching.empty())
        debug_.push_back("Enter on a watch changes it, and an empty answer takes it away");
    if (stack_.size() > 1) {
        debug_.push_back("Ctrl-Up looks at what called this   Ctrl-Down comes back down");
        debug_.push_back("Enter on a frame looks at it, and on the top line goes back");
    }
}

bool Editor::debuggingShalimar() const { return shm_.running(); }
bool Editor::debugging() const { return debugger_.running() || shm_.running(); }

void Editor::debug(bool project) {
    if (shm_.running()) { showStop(shm_.resume()); return; }
    if (debugger_.running()) { showStop(debugger_.resume()); return; }

    // What is going under the debugger, and in what language. For the project
    // that is settled before anything else is asked, because the refusals -
    // no build entry, a group of two languages - are about the project rather
    // than about debugging, and they read better said first.
    std::vector<Part> parts;
    if (project) {
        std::string why, detail;
        if (!project_.targetParts(parts, why, &detail)) {
            say(why);
            if (!detail.empty()) {
                panelOpen_ = true;
                tab_ = TabConsole;
                console_.clear();
                console_.push_back(why);
                console_.push_back("");
                wrapInto(console_, detail, static_cast<size_t>(screenCols_ - 4));
                panelOff_ = 0;
            }
            return;
        }
    } else {
        Part one;
        one.lang = lang_;
        one.sources.push_back(buf_.path());
        parts.push_back(one);
    }

    for (size_t i = 0; i < parts.size(); ++i) {
        ToolchainKind each = toolchainOf(tool_, parts[i]);
        if (!canCompile(each, parts[i].lang)) { say(refusal(each, parts[i].lang)); return; }
        if (!runsHere(each, kArches[arch_])) { say(whyNotRun(each, kArches[arch_])); return; }
    }

    // Which debugger, when there may be more than one compiler in the program.
    // Debug information does not mix: cl writes CodeView, cc1 writes DWARF on
    // two targets and nothing on the third, and shc writes none anywhere by
    // decision. So a program linked from two compilers has a debugger that can
    // see part of it, and the honest thing is to start it and say which part -
    // not to refuse a whole program because one group is invisible.
    ToolchainKind kind = toolchainOf(tool_, parts[0]);
    DebuggerKind engine = DebuggerNone;
    std::vector<std::string> blind;
    for (size_t i = 0; i < parts.size(); ++i) {
        ToolchainKind each = toolchainOf(tool_, parts[i]);
        DebuggerKind theirs = dbg_for(each, kArches[arch_]);
        if (theirs == DebuggerNone) {
            blind.push_back(parts[i].group.empty() ? std::string(toolchainName(each))
                                                   : parts[i].group);
            continue;
        }
        if (engine == DebuggerNone) { engine = theirs; kind = each; }
    }

    // Shalimar is asked about after that and before the refusals, because both
    // of them are about a debugger and there is no debugger here to have or to
    // lack. A Shalimar program carries its own position - shm_line(unit, line)
    // before every statement, in every build - and a debug build offers it to a
    // session inside the program. So what "no debugger" would have said is not
    // true of it, and what "built without -g" would have said is not either:
    // there is no -g, and what a debug build changes is which runtime archive
    // is linked. That last one still has to be said, because a release build
    // genuinely cannot be stopped - it has no code for it - so the sentence is
    // the same shape with the true reason in it.
    const bool shalimar = (toolchainOf(tool_, parts[0]) == ToolShc);
    if (shalimar) kind = ToolShc;
    if (!shalimar && engine == DebuggerNone) {
        say(dbg_whyNot(kind, kArches[arch_]));
        return;
    }
    if (config_ != ConfigDebug) {
        say(shalimar ? "release links a runtime with no debugger in it - Ctrl-D, then F8"
                     : "release is built without -g - Ctrl-D for debug, then F8");
        return;
    }
    if (project) {
        if (!saveEveryDirty()) return;
    } else if (buf_.dirty() || buf_.path().empty()) {
        if (!save()) return;
    }

    panelOpen_ = true;
    tab_ = TabConsole;
    console_.clear();
    console_.push_back(project ? "$ building the project for the debugger"
                               : "$ building for the debugger");
    if (project)
        for (size_t i = 0; i < parts.size(); ++i)
            for (size_t f = 0; f < parts[i].sources.size(); ++f)
                console_.push_back("    " + project_.relative(parts[i].sources[f]));

    // Said before the build rather than after it, because it is about what the
    // session will be able to do and whoever pressed F8 is about to find out
    // the hard way otherwise.
    for (size_t i = 0; i < blind.size(); ++i)
        console_.push_back("  (" + blind[i] + " carries no debug information - the "
                           "debugger cannot stop in it)");

    panelOff_ = 0;
    say("building for the debugger ...");
    refresh();

    // The project's program is built where the project keeps it and is not the
    // editor's to remove afterwards; a single file's is a temporary thing made
    // to be stepped through and cleared away.
    debugTemporary_ = !project;
    if (project)
        debugBuilt_ = buildParts(tool_, parts, kArches[arch_], config_,
                                 project_.targetProgram(), consoleSink, this);
    else
        debugBuilt_ = buildProgram(tool_, kind, buf_.path(), parts[0].lang, kArches[arch_],
                                   config_, consoleSink, this);
    lastDiag_ = debugBuilt_.diag;
    if (!debugBuilt_.ok) {
        if (debugBuilt_.diag.present) {
            std::string where = debugBuilt_.diag.file;
            if (!where.empty() && !path::exists(where)) where = project_.absolute(where);
            if (!where.empty() && path::exists(where) && where != buf_.path()) open(where);
            cy_ = debugBuilt_.diag.line - 1;
            if (cy_ >= buf_.lineCount()) cy_ = buf_.lineCount() - 1;
            cx_ = debugBuilt_.diag.col - 1;
            clampCursor();
            focus_ = FocusText;
            say(number(debugBuilt_.diag.line) + ":" + number(debugBuilt_.diag.col) + ": error: " +
                debugBuilt_.diag.message);
        } else {
            say(std::string(toolchainName(kind)) + " built no program - see the console");
        }
        if (debugTemporary_) removeProgram(debugBuilt_);
        debugBuilt_ = Built();
        return;
    }

    if (shalimar) {
        // Nothing is started here except the program itself. If it will not
        // arm, the reason is almost always that it was built without --debug -
        // a release build runs and never says #ready - so that is what is
        // said rather than "could not be started", which would send whoever
        // read it looking for a debugger to install.
        if (!shm_.start(debugBuilt_.program)) {
            console_.push_back("the program did not arm - it has no debugger in it");
            say("built without --debug, so there is nothing in it to stop - Ctrl-D, then F8");
            if (debugTemporary_) removeProgram(debugBuilt_);
            debugBuilt_ = Built();
            return;
        }
    } else if (!debugger_.start(engine, debugBuilt_.program)) {
        const char* named = dbg_name(engine);
        console_.push_back(std::string(named) + " could not be started");
        say(std::string(named) + " could not be started - is it installed?");
        if (debugTemporary_) removeProgram(debugBuilt_);
        debugBuilt_ = Built();
        return;
    }

    size_t set = 0;
    for (std::map<std::string, FileBreaks>::iterator file = breaks_.begin();
         file != breaks_.end(); ++file)
        for (std::set<size_t>::iterator line = file->second.lines.begin();
             line != file->second.lines.end(); ++line)
            if (shalimar ? shm_.breakAt(file->second.path, *line)
                         : debugger_.breakAt(file->second.path, *line)) ++set;

    console_.push_back(std::string("started ") +
                       (shalimar ? "the program itself" : dbg_name(debugger_.kind())) +
                       " with " + number(set) + " breakpoint" + (set == 1 ? "" : "s"));
    showStop(shalimar ? shm_.run() : debugger_.run());
}

void Editor::debugStep(Action how) {
    if (!debugging()) { say("nothing is running - F8 starts it"); return; }

    if (shm_.running()) {
        if (how == ActionStepInto)      showStop(shm_.stepInto());
        else if (how == ActionStepOut)  showStop(shm_.stepOut());
        else                            showStop(shm_.stepOver());
        return;
    }

    if (how == ActionStepInto)      showStop(debugger_.stepInto());
    else if (how == ActionStepOut)  showStop(debugger_.stepOut());
    else                            showStop(debugger_.stepOver());
}

void Editor::debugStop() {
    if (debugger_.running()) debugger_.stop();
    if (shm_.running()) shm_.stop();
    if (debugTemporary_) removeProgram(debugBuilt_);
    debugBuilt_ = Built();
    stopFile_.clear();
    stopLine_ = 0;
    locals_.clear();
    stack_.clear();
    looking_ = 0;
}

void Editor::showAbout() {
    panelOpen_ = true;
    tab_ = TabConsole;
    console_.clear();

    std::vector<std::string> said = about::lines();
    for (size_t i = 0; i < said.size(); ++i) console_.push_back(said[i]);

    panelOff_ = 0;
    say(std::string(about::name()) + " " + about::version());
}

// The manual's contents, in the panel. Not the manual itself: ten pages of
// prose compiled into the binary would be a second copy of what is in help/,
// and a second copy is how documentation comes to outlive the fact. What
// somebody at the keyboard wants is the reminder - what exists, and where.
void Editor::showHelpContents() {
    panelOpen_ = true;
    tab_ = TabConsole;
    console_.clear();
    std::vector<std::string> said = help::contents();
    for (size_t i = 0; i < said.size(); ++i) console_.push_back(said[i]);
    panelOff_ = 0;
    say("the manual is in help/ - F1 for the keys");
}

void Editor::showKeys() {
    panelOpen_ = true;
    tab_ = TabConsole;
    console_.clear();
    console_.push_back("F10          the menu             Ctrl-B   build this file");
    console_.push_back("F5           run this file        F4       build the project");
    console_.push_back("F1           these keys");
    console_.push_back("F9 / F8      breakpoint / debug   F7 / F6  step over / into");
    console_.push_back("Ctrl-Up      up the stack         Ctrl-Down down again");
    console_.push_back("             Debug menu: Debug project puts the project's own");
    console_.push_back("             program under the debugger instead of this file");
    console_.push_back("F2 / F3      previous / next file Ctrl-L   line numbers");
    console_.push_back("Ctrl-K       automatic, cc1, cl   Ctrl-T   next target");
    console_.push_back("Ctrl-D       debug or release");
    console_.push_back("Ctrl-W       next pane            Ctrl-T   next target");
    console_.push_back("Ctrl-P       project pane         Ctrl-A   re-indent (selection)");
    console_.push_back("             Edit menu: Plain frame draws it with - | + instead,");
    console_.push_back("             for a console whose font breaks the box characters");
    console_.push_back("Ctrl-E       bottom panel         Tab      lay this line out");
    console_.push_back("Ctrl-F       find                 Ctrl-G   find the next one");
    console_.push_back("Ctrl-R       replace              Ctrl-Z   undo, Ctrl-Y redo");
    console_.push_back("shift+arrows select              Ctrl-C   copy, X cut, V paste");
    console_.push_back("Ctrl-S       save                 Ctrl-Q   leave");
    console_.push_back("In the project pane, enter opens. In the panel, left and right");
    console_.push_back("change tab - Console, Debug, Assembly - and on Console,");
    console_.push_back("enter goes to the line cc1 named. On Debug, enter on a frame");
    console_.push_back("looks at it, and on the top line goes back to the stop.");
    panelOff_ = 0;
    say("keys");
}

void Editor::perform(Action action) {
    switch (action) {
        case ActionNew:          newFile(); break;
        case ActionOpen:         openPrompt(); break;
        case ActionSave:         save(); break;
        case ActionSaveAs:       saveAs(); break;
        case ActionQuit:         if (mayLeave()) running_ = false; break;
        case ActionCloseFile:    closeDocument(); break;
        case ActionProjectNew:   newProject(); break;
        case ActionProjectSave:  saveProject(); break;
        case ActionProjectAdd:   addToProject(); break;
        case ActionFileCreate:   createFile(); break;
        case ActionFileRename:   renameFile(); break;
        case ActionFileDelete:   deleteFile(); break;
        case ActionFileRegroup:  regroupFile(); break;
        case ActionNextFile:     nextDocument(1); break;
        case ActionPrevFile:     nextDocument(-1); break;
        case ActionLayOut:       reindentAll(); break;
        case ActionCopy:         copySelection(false); break;
        case ActionCut:          copySelection(true); break;
        case ActionPaste:        pasteClipboard(); break;
        case ActionSelectAll:    selectAll(); break;
        case ActionUndo:         undoEdit(); break;
        case ActionRedo:         redoEdit(); break;
        case ActionFind:         findPrompt(); break;
        case ActionFindNext:     findAgain(true); break;
        case ActionFindPrevious: findAgain(false); break;
        case ActionReplace:      replacePrompt(); break;
        case ActionToggleTree:
            treeOpen_ = !treeOpen_;
            if (!treeOpen_ && focus_ == FocusTree) focus_ = FocusText;
            break;
        case ActionTogglePlain: {
            bool plain = frame_ != &kBoxFrame;
            setPlainFrame(!plain);
            settings::rememberPlainFrame(!plain);
            // Everything moves, so nothing on the screen is where it was.
            painted_.clear();
            say(plain ? "the frame is drawn with the box characters again"
                      : "the frame is drawn with - | + now, and will be next time");
            break;
        }
        case ActionToggleNumbers:
            numbers_ = !numbers_;
            say(numbers_ ? "line numbers on" : "line numbers off");
            break;
        case ActionTogglePanel:
            panelOpen_ = !panelOpen_;
            if (!panelOpen_ && focus_ == FocusPanel) focus_ = FocusText;
            break;
        case ActionBuild:        compile(); break;
        case ActionRun:          buildAndRun(); break;
        case ActionBuildProject: buildProject(false); break;
        case ActionRunProject:   buildProject(true); break;
        case ActionToggleBreak:  toggleBreak(); break;
        case ActionDebug:        debug(false); break;
        case ActionDebugProject: debug(true); break;
        case ActionStepOver:
        case ActionStepInto:
        case ActionStepOut:      debugStep(action); break;
        case ActionFrameUp:      lookAlongStack(1); break;
        case ActionFrameDown:    lookAlongStack(-1); break;
        case ActionWatch:        watchExpression(); break;
        case ActionDebugStop:
            if (debugging()) { debugStop(); say("debugging stopped"); }
            else say("nothing is running");
            break;
        case ActionConfigDebug:
            config_ = ConfigDebug;
            if (project_.loaded()) project_.setConfig(config_);
            resetDebug();
            // The flags themselves, rather than a second copy of them written
            // out by hand: that copy is what went stale when cc1 grew a -g.
            say("debug:" + configFlags(resolve(tool_, lang_), config_, kArches[arch_]) +
                (optimises(resolve(tool_, lang_)) ? "" : " - cc1 has no -O"));
            break;
        case ActionConfigRelease:
            config_ = ConfigRelease;
            if (project_.loaded()) project_.setConfig(config_);
            resetDebug();
            say("release:" + configFlags(resolve(tool_, lang_), config_, kArches[arch_]) +
                (optimises(resolve(tool_, lang_)) ? "" : " - cc1 has no -O"));
            break;
        case ActionShowConsole:  panelOpen_ = true; tab_ = TabConsole; panelOff_ = 0; break;
        case ActionShowDebug:    panelOpen_ = true; tab_ = TabDebug; panelOff_ = 0; break;
        case ActionShowAssembly: panelOpen_ = true; tab_ = TabAssembly; panelOff_ = 0; break;
        case ActionArchWindows:
        case ActionArchLinux:
        case ActionArchDarwin:
            arch_ = static_cast<size_t>(action - ActionArchWindows);
            resetDebug();
            say(usesArch(tool_.kind)
                    ? std::string("target: ") + kArches[arch_]
                    : std::string("target is a cc1 setting - cl builds for its own host"));
            break;
        case ActionLangAuto:
            langChoice_ = LangCount;
            applyLanguage();
            say(std::string("language: chosen by the name - this one is ") +
                languageName(lang_));
            break;
        case ActionLangC:
        case ActionLangCpp:
        case ActionLangShalimar:
        case ActionLangText: {
            static const Language chosen[] = {LangC, LangCpp, LangShalimar, LangPlain};
            langChoice_ = chosen[action - ActionLangC];
            applyLanguage();
            resetDebug();   // which language it is decides which compiler runs
            say(std::string("language: ") + languageName(lang_) + ", for every file");
            break;
        }
        case ActionToolShc:
            tool_.kind = ToolShc;
            resetDebug();
            say("compiler: shc, for every file");
            break;
        case ActionToolAuto:
            tool_.kind = ToolAuto;
            resetDebug();   // which compiler it is decides what the panel says
            say(std::string("compiler: chosen by the file - this one goes to ") +
                toolchainName(resolve(tool_, lang_)));
            break;
        case ActionToolCc1:
            tool_.kind = ToolCc1;
            resetDebug();
            say("compiler: cc1, for every file");
            break;
        case ActionToolMsvc:
            tool_.kind = ToolMsvc;
            resetDebug();
            say("compiler: cl, for every file");
            break;
        case ActionToolCxx:
            // The machine's C++ compiler, which is a kind rather than a
            // program: cl on Windows and a gcc-style driver elsewhere, and
            // they take none of each other's flags.
            tool_.kind = hostCppToolchain();
            resetDebug();
            // Named rather than called "the host's", because which one it is
            // is the whole content of the answer and it is one string away.
            say("compiler: " + toolchainShown(tool_, tool_.kind) + ", for every file");
            break;
        case ActionHelpContents: showHelpContents(); break;
        case ActionKeys:         showKeys(); break;
        case ActionAbout:        showAbout(); break;
        case ActionNone:         break;
    }
}

void Editor::applyLanguage() {
    lang_ = (langChoice_ == LangCount) ? languageFor(buf_.path()) : langChoice_;
    // Shalimar is not C with fewer rules: ':' is its assignment where C reads
    // a label, and reading one as the other would walk every assignment out
    // to the function's own column.
    style_.dialect = (lang_ == LangShalimar) ? DialectShalimar : DialectC;
}

std::string Editor::prompt(const std::string& text, bool& cancelled) {
    std::string answer;
    cancelled = false;

    // The title is what was asked, without the colon and space it used to be
    // run together with its answer on the message line.
    std::string title = text;
    while (!title.empty() && (title[title.size() - 1] == ' ' || title[title.size() - 1] == ':'))
        title.resize(title.size() - 1);
    if (title.empty()) title = "?";
    // A title, so it is written like one. The questions are phrased for the
    // message line they used to be asked on and are left as they are there.
    if (title[0] >= 'a' && title[0] <= 'z') title[0] = static_cast<char>(title[0] - 'a' + 'A');

    bool done = false;
    for (; !done;) {
        askTitle_ = title;
        askAnswer_ = answer;
        refresh();

        int key = term_.readKey();
        if (key == KEY_NONE) {
            if (!term_.eof()) continue;
            cancelled = true;
            done = true;
        } else if (key == '\x1b') {
            cancelled = true;
            done = true;
        } else if (key == '\r' || key == '\n') {
            done = true;
        } else if (key == KEY_BACKSPACE || key == ctrl('h')) {
            if (!answer.empty()) answer.resize(utf8::startOf(answer, answer.size() - 1));
        } else if (key >= 32 && key < 127) {
            answer += static_cast<char>(key);
        }
    }

    // The box goes when the question has been answered, and the screen it was
    // over comes back on the next refresh.
    askTitle_.clear();
    askAnswer_.clear();
    return cancelled ? std::string() : answer;
}

void Editor::processKey(int key) {
    // While the menu is down it has the keyboard, and nothing reaches the text.
    if (menu_.active()) {
        perform(menu_.key(key));
        return;
    }

    if (key != ctrl('q')) quitConfirm_ = 0;

    switch (key) {
        case KEY_F10: openMenu(); return;
        case KEY_F1:  showKeys(); return;
        case KEY_F2:  nextDocument(-1); return;
        case KEY_F3:  nextDocument(1); return;
        case KEY_F4:  perform(ActionBuildProject); return;
        case KEY_F5:  perform(ActionRun); return;
        case KEY_F6:  perform(ActionStepInto); return;
        case KEY_F7:  perform(ActionStepOver); return;
        case KEY_F8:  perform(ActionDebug); return;
        case KEY_F9:  perform(ActionToggleBreak); return;

        case KEY_CTRL_UP:   perform(ActionFrameUp); return;
        case KEY_CTRL_DOWN: perform(ActionFrameDown); return;

        case ctrl('q'):
            if (!mayLeave()) return;
            running_ = false;
            return;

        case ctrl('s'): perform(ActionSave); return;
        case ctrl('b'): perform(ActionBuild); return;
        case ctrl('c'): perform(ActionCopy); return;
        case ctrl('x'): perform(ActionCut); return;
        case ctrl('v'): perform(ActionPaste); return;
        case ctrl('z'): perform(ActionUndo); return;
        case ctrl('y'): perform(ActionRedo); return;
        case ctrl('a'): perform(ActionLayOut); return;
        case ctrl('f'): perform(ActionFind); return;
        case ctrl('g'): perform(ActionFindNext); return;
        case ctrl('r'): perform(ActionReplace); return;
        case ctrl('p'): perform(ActionToggleTree); return;
        case ctrl('e'): perform(ActionTogglePanel); return;
        case ctrl('l'): perform(ActionToggleNumbers); return;

        case ctrl('d'):
            perform(config_ == ConfigDebug ? ActionConfigRelease : ActionConfigDebug);
            return;

        case ctrl('k'):
            // Round them all rather than between two, in the order the Tools
            // menu lists them, so what the key does and what the menu shows
            // are one thing rather than two that can disagree.
            perform(tool_.kind == ToolAuto
                        ? ActionToolCc1
                        : (tool_.kind == ToolCc1
                               ? ActionToolShc
                               : (tool_.kind == ToolShc
                                      ? ActionToolMsvc
                                      : (tool_.kind == ToolMsvc &&
                                                 hostCppToolchain() != ToolMsvc
                                             ? ActionToolCxx
                                             : ActionToolAuto))));
            return;
        case ctrl('w'): cycleFocus(); return;

        case ctrl('t'):
            arch_ = (arch_ + 1) % 3;
            say(std::string("target: ") + kArches[arch_] + " - Ctrl-B to build it");
            return;

        case KEY_ARROW_UP:
        case KEY_ARROW_DOWN:
        case KEY_ARROW_LEFT:
        case KEY_ARROW_RIGHT:
        case KEY_HOME:
        case KEY_END:
        case KEY_PAGE_UP:
        case KEY_PAGE_DOWN:
            if (focus_ == FocusTree) moveTree(key);
            else if (focus_ == FocusPanel) movePanel(key);
            else {
                // Moving without shift lets the selection go, which is what
                // every editor does and what the arrow keys mean here.
                dropSelection();
                moveCursor(key);
            }
            return;

        case KEY_SHIFT_UP:
        case KEY_SHIFT_DOWN:
        case KEY_SHIFT_LEFT:
        case KEY_SHIFT_RIGHT:
        case KEY_SHIFT_HOME:
        case KEY_SHIFT_END:
        case KEY_SHIFT_PAGE_UP:
        case KEY_SHIFT_PAGE_DOWN:
            if (focus_ == FocusText) extendTo(key);
            return;

        default: break;
    }

    if (focus_ == FocusTree) {
        if (key == '\r' || key == '\n') openSelected();
        return;
    }
    if (focus_ == FocusPanel) {
        if (key == '\r' || key == '\n') {
            if (tab_ == TabConsole) goToProblem();
            else if (tab_ == TabDebug) goToFrame();
        }
        return;   // otherwise the panel is there to be read
    }

    switch (key) {
        case '\r':
        case '\n':
            // A terminal in raw mode sends carriage return for the enter key;
            // input arriving down a pipe carries a newline instead. Both mean
            // the same thing here, and taking both is what lets the editor be
            // driven by a script.
            insertNewline();
            return;

        case KEY_BACKSPACE:
        case ctrl('h'):
            if (!eraseSelection()) backspace();
            return;
        case KEY_DELETE:
            if (!eraseSelection()) deleteForward();
            return;
        case '\t': tabKey(); return;

        default: {
            if (key < 32 || key >= 127) return;
            char c = static_cast<char>(key);

            // Three characters decide where their own line sits, and only these
            // three, so nothing moves under the caret unless it had to.
            eraseSelection();

            std::string before = buf_.line(cy_).substr(0, cx_);
            bool atHead = before.find_first_not_of(" \t") == std::string::npos;

            insertChar(c);

            if ((c == '}' && atHead) || (c == '#' && atHead) ||
                (c == ':' && endsALabel(before)))
                realign();
            return;
        }
    }
}

void Editor::run() {
    if (message_.empty()) say("F10 menu  Ctrl-B build  Ctrl-Z undo  Ctrl-F find  F1 keys  Ctrl-Q quit");

    int wasRows = 0, wasCols = 0;
    while (running_) {
        // A redraw for every timeout would rewrite the whole screen ten times a
        // second for no reason. It is drawn when a key has changed something,
        // or when the window itself has changed size.
        int rows = 0, cols = 0;
        term_.size(rows, cols);
        if (rows != wasRows || cols != wasCols) {
            needsDraw_ = true;
            wasRows = rows;
            wasCols = cols;
        }
        if (needsDraw_) {
            refresh();
            needsDraw_ = false;
        }

        int key = term_.readKey();
        if (key != KEY_NONE) {
            processKey(key);
            needsDraw_ = true;
        }
        if (term_.eof()) break;
    }

    Terminal::write("\x1b[2J\x1b[H");
}

}  // namespace editor
