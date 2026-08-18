#include "editor.h"

#include <cstdlib>

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
void expandWithKinds(const std::string& s, const std::vector<unsigned char>& kinds,
                     std::string& text, std::vector<unsigned char>& out) {
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char k = i < kinds.size() ? kinds[i] : KindNormal;
        if (s[i] != '\t') {
            text += s[i];
            out.push_back(k);
            continue;
        }
        do {
            text += ' ';
            out.push_back(k);
        } while (text.size() % kTabStop != 0);
    }
}

// A window of the line, written as runs of one colour. One escape per run
// rather than one per character - a screen's worth of the latter is enough to
// be seen redrawing on a slow console.
std::string colouredWindow(const std::string& text,
                           const std::vector<unsigned char>& kinds,
                           size_t from, size_t width) {
    std::string out;
    size_t drawn = 0;
    int current = -1;

    for (size_t i = from; i < text.size() && drawn < width; ++i, ++drawn) {
        unsigned char k = i < kinds.size() ? kinds[i] : KindNormal;
        if (static_cast<int>(k) != current) {
            out += "\x1b[";
            out += colourFor(k);
            out += "m";
            current = k;
        }
        out += text[i];
    }
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
      focus_(FocusText), lang_(LangPlain), arch_(0), numbers_(true), needsDraw_(true),
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
    if (fromEnv && *fromEnv) tool_.program = fromEnv;

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

void Editor::openProject(const std::string& path) {
    tree_.setRoot(path);
    treeSel_ = 0;
    treeOff_ = 0;
    if (!tree_.error().empty()) say(tree_.error());
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
    for (size_t i = 0; i < col && i < line.size(); ++i)
        r += (line[i] == '\t') ? kTabStop - (r % kTabStop) : 1;
    return r;
}

void Editor::clampCursor() {
    if (cy_ >= buf_.lineCount()) cy_ = buf_.lineCount() - 1;
    if (cx_ > buf_.line(cy_).size()) cx_ = buf_.line(cy_).size();
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
            out += colouredWindow(text, spread, coloff_, static_cast<size_t>(sourceCols_));
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
        right = "no debug info";
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
        out += colouredWindow(text, spread, 0, static_cast<size_t>(screenCols_));
        out += "\x1b[K\r\n";
    }
}

void Editor::drawStatus(std::string& out) const {
    std::string name = buf_.path().empty() ? std::string("[no name]") : baseName(buf_.path());
    std::string left = " " + name;
    if (buf_.dirty()) left += " *";
    left += "  " + lineCountText(buf_.lineCount());

    std::string right = languageName(lang_);
    right += "  ";
    right += toolchainName(tool_.kind);
    // The target is only shown when it means something. cl generates for the
    // host it was installed as, and offering a choice that does nothing would
    // be the status bar telling a lie.
    if (usesArch(tool_.kind)) {
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
            if (cx_ > 0) --cx_;
            else if (cy_ > 0) { --cy_; cx_ = buf_.line(cy_).size(); }
            break;
        case KEY_ARROW_RIGHT:
            if (cx_ < line.size()) ++cx_;
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
        case KEY_ARROW_RIGHT: tree_.toggle(treeSel_); break;
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

void Editor::insertChar(char c) { buf_.insertChar(cy_, cx_, c); ++cx_; }

void Editor::insertNewline() {
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
    if (cx_ > 0) {
        buf_.eraseChar(cy_, cx_ - 1);
        --cx_;
    } else if (cy_ > 0) {
        cx_ = buf_.line(cy_ - 1).size();
        buf_.joinLine(cy_ - 1);
        --cy_;
    }
}

void Editor::deleteForward() {
    if (cx_ < buf_.line(cy_).size()) buf_.eraseChar(cy_, cx_);
    else if (cy_ + 1 < buf_.lineCount()) buf_.joinLine(cy_);
}

void Editor::realign() {
    const std::string& line = buf_.line(cy_);
    size_t had = leadingSpace(line);
    std::string want = indentFor(buf_.lines(), cy_, style_);
    if (want == line.substr(0, had)) return;

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
        buf_.replaceLine(cy_, want + line.substr(lead));
        cx_ = want.size();
        return;
    }
    if (style_.tabs) { insertChar('\t'); return; }
    for (size_t i = 0; i < style_.width; ++i) insertChar(' ');
}

void Editor::reindentAll() {
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
    say(buf_.path() + " written");
    tree_.reread();
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
    if (e.directory) { tree_.toggle(treeSel_); return; }
    open(e.path);
    focus_ = FocusText;
}

void Editor::resetDebug() {
    // Said plainly rather than left blank. cc1 emits no debug information at
    // all today - no -g, no DWARF, no CodeView - so there is nothing for a
    // debugger to read and no variables anyone could show. The tab is here so
    // that the panel is the shape it will keep; what fills it is compiler work,
    // not editor work.
    debug_.clear();
    debug_.push_back("Variables, watches and the call stack belong here.");
    debug_.push_back("");
    debug_.push_back("Nothing to show yet: cc1 emits no debug information -");
    debug_.push_back("no -g, no DWARF, no CodeView - so a debugger has no");
    debug_.push_back("symbols to read. Until the compiler emits some, this");
    debug_.push_back("tab stays empty rather than inventing values.");
    debug_.push_back("");
    debug_.push_back("target: " + std::string(kArches[arch_]));
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
    // Said here rather than left to a wall of parse errors. cc1 is a C
    // compiler; handed C++ it fails somewhere inside the first class, and the
    // diagnostic it gives explains nothing about why.
    if (lang_ == LangCpp && tool_.kind == ToolCc1) {
        say("cc1 compiles C, not C++ - Ctrl-K switches to cl");
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
    console_.push_back("$ " + shownCommand(tool_, buf_.path(), kArches[arch_]));
    panelOff_ = 0;
    say(std::string("building with ") + toolchainName(tool_.kind) +
        (usesArch(tool_.kind) ? std::string(" for ") + kArches[arch_] : std::string()) +
        " ...");
    refresh();

    Build result = build(tool_, buf_.path(), kArches[arch_], consoleSink, this);

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
        console_.push_back(std::string(toolchainName(tool_.kind)) + " failed");
        say(std::string(toolchainName(tool_.kind)) + " failed - see the console");
    } else {
        console_.push_back("ok - " + number(assembly_.size()) + " lines of assembly");
        tab_ = TabAssembly;
        panelOff_ = 0;
        say(number(assembly_.size()) + " lines of " +
            (usesArch(tool_.kind) ? kArches[arch_] : toolchainName(tool_.kind)) +
            " assembly");
    }
}

void Editor::showKeys() {
    panelOpen_ = true;
    tab_ = TabConsole;
    console_.clear();
    console_.push_back("F10          the menu             Ctrl-B   build with cc1");
    console_.push_back("F2 / F3      previous / next file Ctrl-L   line numbers");
    console_.push_back("Ctrl-K       cc1 or cl            Ctrl-T   next target");
    console_.push_back("Ctrl-W       next pane            Ctrl-T   next target");
    console_.push_back("Ctrl-P       project pane         Ctrl-F   lay the file out");
    console_.push_back("Ctrl-E       bottom panel         Tab      lay this line out");
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
        case ActionNextFile:     nextDocument(1); break;
        case ActionPrevFile:     nextDocument(-1); break;
        case ActionLayOut:       reindentAll(); break;
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
        case ActionToolCc1:
            setToolchain(ToolCc1);
            say("compiler: cc1");
            break;
        case ActionToolMsvc:
            // cl is only on PATH inside a Developer Command Prompt, and saying
            // so here saves a puzzled minute later.
            setToolchain(ToolMsvc);
            say("compiler: cl - run ed1 from a Developer Command Prompt");
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
        case ctrl('f'): perform(ActionLayOut); return;
        case ctrl('p'): perform(ActionToggleTree); return;
        case ctrl('e'): perform(ActionTogglePanel); return;
        case ctrl('l'): perform(ActionToggleNumbers); return;

        case ctrl('k'):
            perform(tool_.kind == ToolCc1 ? ActionToolMsvc : ActionToolCc1);
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
            else moveCursor(key);
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
        case ctrl('h'): backspace(); return;
        case KEY_DELETE: deleteForward(); return;
        case '\t': tabKey(); return;

        default: {
            if (key < 32 || key >= 127) return;
            char c = static_cast<char>(key);

            // Three characters decide where their own line sits, and only these
            // three, so nothing moves under the caret unless it had to.
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
    if (message_.empty()) say("F10 menu   Ctrl-B build   Ctrl-F lay out   F1 keys   Ctrl-Q quit");

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
