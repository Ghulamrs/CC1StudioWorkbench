#include "editor.h"

#include "utf8.h"
#include "symbols.h"
#include "workspace.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace editor {

namespace {

const int kTreeWidth = 22;   // enough for a name and two levels of nesting
const int kPanelRows = 7;    // the command, and a few lines of what it said

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
      focus_(FocusText), lang_(LangPlain), config_(ConfigDebug), arch_(0), numbers_(true), needsDraw_(true),
      marked_(false), markRow_(0), markCol_(0),
      quitConfirm_(0), running_(true),
      screenRows_(24), screenCols_(80),
      bodyRows_(14), panelRows_(kPanelRows),
      treeCols_(kTreeWidth), sourceCols_(80), gutterCols_(4) {
    // The host's own architecture first, since that is the one cc1 will carry
    // past -S on this machine.
#if defined(_WIN32)
    arch_ = 0;
#elif defined(__APPLE__)
    arch_ = 2;
#else
    arch_ = 1;
#endif
    const char* fromEnv = std::getenv("CC1");
    if (fromEnv && *fromEnv) tool_.cc1 = fromEnv;

    console_.push_back("cc1 output appears here.  Ctrl-B builds.");
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

size_t Editor::findDocument(const std::string& path) const {
    for (size_t i = 0; i < docs_.size(); ++i) {
        const std::string& have = (i == doc_) ? buf_.path() : docs_[i].buf.path();
        if (!have.empty() && have == path) return i;
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
    lang_ = languageFor(path);

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
        say(project_.name() + " - " + number(project_.groups().size()) + " groups");
    } else {
        // No project file is not a fault. It means the pane shows the
        // directory, which is what it did before projects existed.
        tree_.setRoot(path);
        project_.setRoot(tree_.root());
        if (!error.empty()) say(error);
        else if (!tree_.error().empty()) say(tree_.error());
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

    // One row each for the menu, the file tabs, the status bar and the message
    // line; the panel takes a header row plus its own.
    int taken = 4;
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
    if (bodyRows_ < 1) bodyRows_ = 1;

    if (treeOpen_) {
        treeCols_ = kTreeWidth;
        if (treeCols_ > screenCols_ / 3) treeCols_ = screenCols_ / 3;
        sourceCols_ = screenCols_ - treeCols_ - 3;
    } else {
        treeCols_ = 0;
        sourceCols_ = screenCols_;
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

void Editor::drawTabs(std::string& out) const {
    std::string bar;
    size_t visible = 0;

    for (size_t i = 0; i < docs_.size(); ++i) {
        const Buffer& b = (i == doc_) ? buf_ : docs_[i].buf;
        std::string name = b.path().empty() ? std::string("[no name]") : baseName(b.path());
        if (b.dirty()) name += "*";
        std::string cell = " " + name + " ";

        if (visible + cell.size() > static_cast<size_t>(screenCols_)) break;
        if (i == doc_) {
            bar += "\x1b[7m";
            bar += cell;
            bar += "\x1b[m";
        } else {
            bar += "\x1b[90m";
            bar += cell;
            bar += "\x1b[39m";
        }
        visible += cell.size();
    }

    out += bar;
    out += "\x1b[4m";
    if (visible < static_cast<size_t>(screenCols_))
        out += std::string(static_cast<size_t>(screenCols_) - visible, ' ');
    out += "\x1b[m\x1b[K\r\n";
}

void Editor::drawBody(std::string& out) const {
    // Drawing starts at rowoff_, so the lines above it are walked first - a
    // block comment opened off the top of the screen still colours what is on
    // it. Only the state is carried; no colours are worked out for them.
    SyntaxState state;
    for (size_t i = 0; i < rowoff_ && i < buf_.lineCount(); ++i)
        advanceState(buf_.line(i), lang_, state);

    for (int y = 0; y < bodyRows_; ++y) {
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
            out += " | ";
        }

        size_t row = rowoff_ + static_cast<size_t>(y);

        if (numbers_) {
            std::string cell(static_cast<size_t>(gutterCols_), ' ');
            if (row < buf_.lineCount()) {
                std::string num = number(row + 1);
                // Right-aligned, with the last column left as a gap so the
                // digits never touch the text.
                size_t at = cell.size() - 1 - num.size();
                for (size_t i = 0; i < num.size(); ++i) cell[at + i] = num[i];
            }
            // The line the caret is on is picked out, which is the whole reason
            // for having the numbers where you can see them.
            out += (row == cy_) ? "\x1b[93m" : "\x1b[90m";
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
        out += "\x1b[K\r\n";
    }
}

void Editor::drawPanel(std::string& out) const {
    if (!panelOpen_) return;

    std::string header;
    const char* names[TabCount] = {" Console ", " Debug ", " Assembly "};
    size_t visible = 0;
    for (int i = 0; i < TabCount; ++i) {
        bool on = (tab_ == static_cast<Tab>(i));
        if (on) header += "\x1b[7m";
        header += names[i];
        if (on) header += "\x1b[m";
        visible += std::string(names[i]).size();
    }

    std::string right;
    if (tab_ == TabConsole)
        right = number(console_.size()) + (console_.size() == 1 ? " line" : " lines");
    else if (tab_ == TabDebug)
        right = assembly_.empty() ? "nothing built yet" : "read from the assembly";
    else
        right = assembly_.empty() ? std::string("nothing built yet")
                                  : number(assembly_.size()) + " lines";
    // The escape codes take no columns, so the padding is measured from the
    // visible text rather than from the string's length.
    std::string pad;
    if (visible + right.size() + 1 < static_cast<size_t>(screenCols_))
        pad = std::string(static_cast<size_t>(screenCols_) - visible - right.size(), ' ');

    out += "\x1b[4m";
    out += header;
    out += pad;
    out += right;
    out += "\x1b[m\x1b[K\r\n";

    const std::vector<std::string>& lines = panelLines();
    Language panelLang = (tab_ == TabAssembly) ? LangAsm : LangPlain;
    for (int y = 0; y < panelRows_; ++y) {
        size_t row = panelOff_ + static_cast<size_t>(y);
        if (row >= lines.size()) {
            out += std::string(static_cast<size_t>(screenCols_), ' ');
            out += "\x1b[K\r\n";
            continue;
        }
        SyntaxState panelState;
        std::vector<unsigned char> kinds = highlight(lines[row], panelLang, panelState);
        std::string text;
        std::vector<unsigned char> spread;
        expandWithKinds(lines[row], kinds, text, spread);
        out += colouredWindow(text, spread, 0, static_cast<size_t>(screenCols_), 0, 0);
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
    right += toolchainName(kind);
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

void Editor::drawDropdown(std::string& out) const {
    if (!menu_.dropped()) return;

    const MenuColumn& col = menu_.columns()[menu_.column()];

    size_t width = 0;
    for (size_t i = 0; i < col.items.size(); ++i) {
        size_t w = col.items[i].label.size() + col.items[i].key.size() + 4;
        if (w > width) width = w;
    }

    size_t at = menu_.titleAt(menu_.column());
    if (at + width + 1 > static_cast<size_t>(screenCols_))
        at = static_cast<size_t>(screenCols_) - width - 1;

    for (size_t i = 0; i < col.items.size(); ++i) {
        // Drawn last and placed by hand, so it lies over the text rather than
        // pushing it aside.
        out += "\x1b[" + number(i + 2) + ";" + number(at + 1) + "H";
        std::string row = " " + col.items[i].label;
        row.resize(width - col.items[i].key.size() - 1, ' ');
        row += col.items[i].key;
        row += " ";
        out += (i == menu_.item()) ? "\x1b[7m" : "\x1b[4m";
        out += row;
        out += "\x1b[m";
    }
}

void Editor::placeCursor(std::string& out) const {
    size_t row = 1, col = 1;
    if (menu_.dropped()) {
        row = menu_.item() + 2;
        col = menu_.titleAt(menu_.column()) + 2;
    } else if (focus_ == FocusTree) {
        row = 3 + (treeSel_ - treeOff_);
        col = 1;
    } else if (focus_ == FocusPanel) {
        row = static_cast<size_t>(3 + bodyRows_ + 1);
        col = 1;
    } else {
        row = 3 + (cy_ - rowoff_);
        col = (treeOpen_ ? static_cast<size_t>(treeCols_) + 3 : 0) +
              static_cast<size_t>(gutterCols_) + (rx_ - coloff_) + 1;
    }
    out += "\x1b[" + number(row) + ";" + number(col) + "H";
}

void Editor::refresh() {
    layout();
    clampCursor();
    scroll();

    std::string out;
    out += "\x1b[?25l\x1b[H";

    drawMenuBar(out);
    drawTabs(out);
    drawBody(out);
    drawPanel(out);
    drawStatus(out);
    drawMessage(out);
    drawDropdown(out);
    placeCursor(out);

    out += "\x1b[?25h";
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
    if (cancelled) { say("nothing looked for"); return; }
    if (want.empty()) { findAgain(true); return; }

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

void Editor::reindentAll() {
    buf_.beginEdit(EditOther, cx_, cy_);
    buf_.replaceAll(reindent(buf_.lines(), style_));
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
    // the old one back.
    for (size_t i = 0; i < docs_.size(); ++i) {
        Buffer& b = (i == doc_) ? buf_ : docs_[i].buf;
        if (b.path() == path) b.setPath(done.path);
    }
    lang_ = languageFor(buf_.path());
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
        if (b.path() == path) b.setPath(std::string());
    }
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

void Editor::showKeys() {
    panelOpen_ = true;
    tab_ = TabConsole;
    console_.clear();
    console_.push_back("F10          the menu             Ctrl-B   build with cc1");
    console_.push_back("F2 / F3      previous / next file Ctrl-L   line numbers");
    console_.push_back("Ctrl-K       automatic, cc1, cl   Ctrl-T   next target");
    console_.push_back("Ctrl-D       debug or release");
    console_.push_back("Ctrl-W       next pane            Ctrl-T   next target");
    console_.push_back("Ctrl-P       project pane         Ctrl-A   lay the file out");
    console_.push_back("Ctrl-E       bottom panel         Tab      lay this line out");
    console_.push_back("Ctrl-F       find                 Ctrl-G   find the next one");
    console_.push_back("Ctrl-R       replace              Ctrl-Z   undo, Ctrl-Y redo");
    console_.push_back("shift+arrows select              Ctrl-C   copy, X cut, V paste");
    console_.push_back("Ctrl-S       save                 Ctrl-Q   leave");
    console_.push_back("In the project pane, enter opens. In the panel, left and right");
    console_.push_back("change tab - Console, Debug, Assembly - and on Console,");
    console_.push_back("enter goes to the line cc1 named.");
    panelOff_ = 0;
    say("keys");
}

void Editor::perform(Action action) {
    switch (action) {
        case ActionNew:          newFile(); break;
        case ActionOpen:         openPrompt(); break;
        case ActionSave:         save(); break;
        case ActionSaveAs:       saveAs(); break;
        case ActionQuit:         running_ = false; break;
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
        case ActionToggleNumbers:
            numbers_ = !numbers_;
            say(numbers_ ? "line numbers on" : "line numbers off");
            break;
        case ActionTogglePanel:
            panelOpen_ = !panelOpen_;
            if (!panelOpen_ && focus_ == FocusPanel) focus_ = FocusText;
            break;
        case ActionBuild:        compile(); break;
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
        case ActionKeys:         showKeys(); break;
        case ActionNone:         break;
    }
}

std::string Editor::prompt(const std::string& text, bool& cancelled) {
    std::string answer;
    cancelled = false;

    for (;;) {
        say(text + answer);
        refresh();

        int key = term_.readKey();
        if (key == KEY_NONE) {
            if (term_.eof()) { cancelled = true; return std::string(); }
            continue;
        }
        if (key == '\x1b') { cancelled = true; return std::string(); }
        if (key == '\r' || key == '\n') return answer;
        if (key == KEY_BACKSPACE || key == ctrl('h')) {
            if (!answer.empty()) answer.resize(answer.size() - 1);
            continue;
        }
        if (key >= 32 && key < 127) answer += static_cast<char>(key);
    }
}

void Editor::processKey(int key) {
    // While the menu is down it has the keyboard, and nothing reaches the text.
    if (menu_.active()) {
        perform(menu_.key(key));
        return;
    }

    if (key != ctrl('q')) quitConfirm_ = 0;

    switch (key) {
        case KEY_F10: menu_.open(); return;
        case KEY_F1:  showKeys(); return;
        case KEY_F2:  nextDocument(-1); return;
        case KEY_F3:  nextDocument(1); return;

        case ctrl('q'):
            if (buf_.dirty() && quitConfirm_ == 0) {
                quitConfirm_ = 1;
                say("unsaved changes - press Ctrl-Q again to leave them behind");
                return;
            }
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
            // Round the three rather than between two, so automatic is never
            // more than two presses away from wherever you are.
            perform(tool_.kind == ToolAuto ? ActionToolCc1
                                           : (tool_.kind == ToolCc1 ? ActionToolMsvc
                                                                    : ActionToolAuto));
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
        if ((key == '\r' || key == '\n') && tab_ == TabConsole) goToProblem();
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
