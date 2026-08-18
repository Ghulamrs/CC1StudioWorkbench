#include "indent.h"

namespace editor {

namespace {

bool isIdentChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_';
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

std::string trimmed(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && isSpace(s[a])) ++a;
    size_t b = s.size();
    while (b > a && isSpace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

// Whether `word` stands alone at `at` rather than being part of a longer name -
// so that 'iffy' is not an if and 'switcher' is not a switch.
bool wordAt(const std::string& s, size_t at, const char* word) {
    size_t n = 0;
    while (word[n]) ++n;
    if (at + n > s.size()) return false;
    if (s.compare(at, n, word) != 0) return false;
    if (at > 0 && isIdentChar(s[at - 1])) return false;
    if (at + n < s.size() && isIdentChar(s[at + n])) return false;
    return true;
}

bool startsWithWord(const std::string& s, const char* word) {
    return wordAt(s, 0, word);
}

}  // namespace

LineFacts examine(const std::string& line, const IndentState& in) {
    LineFacts f;
    f.startsInComment = in.comment;
    f.startsContinued = in.continued;

    bool comment = in.comment;
    bool inString = false;
    bool inChar = false;
    bool atHead = true;   // nothing but whitespace seen yet

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];

        if (comment) {
            if (c == '*' && i + 1 < line.size() && line[i + 1] == '/') {
                comment = false;
                ++i;
            }
            continue;
        }
        if (inString || inChar) {
            // C has escapes where Shalimar's strings had none, so a quote
            // behind a backslash does not end anything.
            if (c == '\\' && i + 1 < line.size()) {
                ++i;
                continue;
            }
            if (inString && c == '"') inString = false;
            if (inChar && c == '\'') inChar = false;
            continue;
        }

        if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
            comment = true;
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') break;
        if (c == '"') {
            inString = true;
            atHead = false;
            f.code += c;
            continue;
        }
        if (c == '\'') {
            inChar = true;
            atHead = false;
            f.code += c;
            continue;
        }

        if (c == '{') {
            ++f.opens;
            atHead = false;
        } else if (c == '}') {
            ++f.closes;
            if (atHead) ++f.leadingCloses;
            atHead = false;
        } else if (!isSpace(c)) {
            atHead = false;
        }
        f.code += c;
    }

    std::string t = trimmed(f.code);
    std::string raw = trimmed(line);

    f.blank = raw.empty();
    // The # is read off the raw line: it is the first thing on the line that
    // matters, and it is never inside anything.
    f.preprocessor = !f.startsInComment && !raw.empty() && raw[0] == '#';

    if (!t.empty()) {
        char last = t[t.size() - 1];
        f.endsStatement = (last == ';' || last == '}' || last == '{');
    }

    f.endsInComment = comment;
    // A backslash at the very end carries the line on - a macro over several
    // lines, whose layout belongs to whoever wrote it.
    f.endsContinued = !raw.empty() && raw[raw.size() - 1] == '\\';

    f.caseLabel = startsWithWord(t, "case") || startsWithWord(t, "default");

    // switch, but only where it opens the brace on this line.
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] == '{') break;
        if (wordAt(t, i, "switch")) f.switchHead = true;
    }

    bool control = startsWithWord(t, "if") || startsWithWord(t, "for") ||
                   startsWithWord(t, "while") || startsWithWord(t, "do") ||
                   startsWithWord(t, "else") || startsWithWord(t, "switch");
    if (control && !t.empty()) {
        char last = t[t.size() - 1];
        // Only a head with its body still to come hangs. '{' opens a block and
        // counts itself; ';' is a body that was already written, empty or not.
        f.controlHead = (last != '{' && last != ';' && last != '}');
    }

    // A label is a name and a colon and nothing else before it. The test for a
    // question mark is what keeps `x = a ? b : c` from looking like one.
    if (!f.caseLabel && !f.preprocessor && !t.empty() && !f.startsInComment) {
        size_t i = 0;
        while (i < t.size() && isIdentChar(t[i])) ++i;
        if (i > 0 && !(t[0] >= '0' && t[0] <= '9')) {
            size_t j = i;
            while (j < t.size() && isSpace(t[j])) ++j;
            if (j < t.size() && t[j] == ':' && (j + 1 >= t.size() || t[j + 1] != ':') &&
                t.find('?') == std::string::npos && !control)
                f.gotoLabel = true;
        }
    }

    return f;
}

IndentState advance(const IndentState& in, const LineFacts& f) {
    IndentState out = in;
    out.comment = f.endsInComment;
    out.continued = f.endsContinued;

    // Walk the braces in the order they appeared, so the switch stack rises and
    // falls with them rather than with a count.
    bool sawOpen = false;
    for (size_t i = 0; i < f.code.size(); ++i) {
        if (f.code[i] == '{') {
            out.inSwitch.push_back(f.switchHead && !sawOpen);
            sawOpen = true;
            ++out.depth;
        } else if (f.code[i] == '}') {
            if (!out.inSwitch.empty()) out.inSwitch.pop_back();
            if (out.depth > 0) --out.depth;
        }
    }

    if (f.blank) {
        // A blank line settles nothing. An if waiting for its body is still
        // waiting on the other side of it.
        return out;
    }

    if (f.controlHead) {
        ++out.hanging;
    } else if (f.endsStatement || f.opens > 0 || f.closes > 0 || f.caseLabel ||
               f.gotoLabel || f.preprocessor) {
        // A statement ends every head that was waiting: in `if (a) if (b) x;`
        // the one semicolon closes both.
        out.hanging = 0;
    }

    return out;
}

int levelFor(const IndentState& in, const LineFacts& f, const IndentStyle& style) {
    if (f.startsInComment || f.startsContinued) return kKeep;
    if (f.blank) return 0;
    if (f.preprocessor) return 0;

    bool switchBody = !in.inSwitch.empty() && in.inSwitch.back();

    int level = in.depth;

    if (f.leadingCloses > 0) {
        // Dedent before the line is written, so a } settles under the line that
        // opened its group rather than under the group's contents.
        level -= static_cast<int>(f.leadingCloses);
    } else if (switchBody) {
        level += static_cast<int>(style.caseIndent);
        if (f.caseLabel) level -= 1;
    } else {
        level += in.hanging;
    }

    if (f.gotoLabel) level -= 1;

    return level < 0 ? 0 : level;
}

std::string indentString(int level, const IndentStyle& style) {
    if (level <= 0) return std::string();
    if (style.tabs) return std::string(static_cast<size_t>(level), '\t');
    return std::string(static_cast<size_t>(level) * style.width, ' ');
}

std::string withoutLeadingSpace(const std::string& line) {
    size_t a = 0;
    while (a < line.size() && isSpace(line[a])) ++a;
    return line.substr(a);
}

IndentState stateBefore(const std::vector<std::string>& lines, size_t row) {
    IndentState state;
    for (size_t i = 0; i < row && i < lines.size(); ++i)
        state = advance(state, examine(lines[i], state));
    return state;
}

std::string indentFor(const std::vector<std::string>& lines, size_t row,
                      const IndentStyle& style) {
    if (row >= lines.size()) return std::string();
    IndentState before = stateBefore(lines, row);
    LineFacts f = examine(lines[row], before);
    int level = levelFor(before, f, style);
    if (level == kKeep) {
        std::string keep = lines[row];
        size_t a = 0;
        while (a < keep.size() && isSpace(keep[a])) ++a;
        return keep.substr(0, a);
    }
    return indentString(level, style);
}

std::vector<std::string> reindent(const std::vector<std::string>& lines,
                                  const IndentStyle& style) {
    std::vector<std::string> out;
    out.reserve(lines.size());

    IndentState state;
    for (size_t i = 0; i < lines.size(); ++i) {
        LineFacts f = examine(lines[i], state);
        int level = levelFor(state, f, style);

        if (level == kKeep) {
            out.push_back(lines[i]);
        } else if (f.blank) {
            // A blank line stays blank rather than collecting trailing space.
            out.push_back(std::string());
        } else {
            out.push_back(indentString(level, style) + withoutLeadingSpace(lines[i]));
        }

        state = advance(state, f);
    }

    return out;
}

std::string indentAfterNewline(const std::vector<std::string>& lines, size_t row,
                               size_t col, const IndentStyle& style) {
    if (row >= lines.size()) return std::string();

    // Measure the head of the line as its own line, so a brace opened before
    // the caret counts and one after it does not.
    std::vector<std::string> upto(lines.begin(), lines.begin() + static_cast<long>(row));
    std::string head = lines[row].substr(0, col > lines[row].size() ? lines[row].size() : col);
    upto.push_back(head);

    IndentState before = stateBefore(upto, upto.size() - 1);
    IndentState after = advance(before, examine(head, before));

    std::string tail = lines[row].substr(col > lines[row].size() ? lines[row].size() : col);
    std::string rest = withoutLeadingSpace(tail);

    LineFacts next = examine(rest, after);
    // The line about to be opened is empty because it is new, not because its
    // author left it blank - it takes the level of the place it opens in.
    if (rest.empty()) next.blank = false;
    int level = levelFor(after, next, style);
    if (level == kKeep) return std::string();
    return indentString(level, style);
}

}  // namespace editor
