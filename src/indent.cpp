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

LineFacts examine(const std::string& line, const IndentState& in,
                  IndentDialect dialect) {
    const bool c = dialect == DialectC;

    LineFacts f;
    f.startsInComment = in.comment;
    f.startsContinued = in.continued;

    bool comment = in.comment;
    bool inString = false;
    bool inChar = false;
    bool atHead = true;   // nothing but whitespace seen yet

    for (size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];

        if (comment) {
            if (ch == '*' && i + 1 < line.size() && line[i + 1] == '/') {
                comment = false;
                ++i;
            }
            continue;
        }
        if (inString || inChar) {
            // C has escapes where Shalimar's strings have none, so a quote
            // behind a backslash does not end anything there. In Shalimar the
            // first closing quote always ends the literal, and a literal that
            // is not closed on its line is an error rather than a carry-over.
            if (c && ch == '\\' && i + 1 < line.size()) {
                ++i;
                continue;
            }
            if (inString && ch == '"') inString = false;
            if (inChar && ch == '\'') inChar = false;
            continue;
        }

        if (c && ch == '/' && i + 1 < line.size() && line[i + 1] == '*') {
            comment = true;
            ++i;
            continue;
        }
        if (ch == '/' && i + 1 < line.size() && line[i + 1] == '/') break;
        if (ch == '"') {
            inString = true;
            atHead = false;
            f.code += ch;
            continue;
        }
        // Shalimar has no character literal at all - a lone quote is not a
        // token there - so an apostrophe inside a comment or a name cannot
        // open one.
        if (c && ch == '\'') {
            inChar = true;
            atHead = false;
            f.code += ch;
            continue;
        }

        if (ch == '{') {
            ++f.opens;
            atHead = false;
        } else if (ch == '}') {
            ++f.closes;
            if (atHead) ++f.leadingCloses;
            atHead = false;
        } else if (!isSpace(ch)) {
            atHead = false;
        }
        f.code += ch;
    }

    std::string t = trimmed(f.code);
    std::string raw = trimmed(line);

    f.blank = raw.empty();
    // The # is read off the raw line: it is the first thing on the line that
    // matters, and it is never inside anything.
    f.preprocessor = c && !f.startsInComment && !raw.empty() && raw[0] == '#';

    if (!t.empty()) {
        char last = t[t.size() - 1];
        // Shalimar has no statement terminator: every line that holds code is
        // a whole statement, so every one of them ends what came before.
        f.endsStatement = c ? (last == ';' || last == '}' || last == '{') : true;
    }

    f.endsInComment = comment;
    // A backslash at the very end carries the line on - a macro over several
    // lines, whose layout belongs to whoever wrote it. Shalimar has no such
    // thing.
    f.endsContinued = c && !raw.empty() && raw[raw.size() - 1] == '\\';

    f.caseLabel = c && (startsWithWord(t, "case") || startsWithWord(t, "default"));

    // switch, but only where it opens the brace on this line.
    if (c) {
        for (size_t i = 0; i < t.size(); ++i) {
            if (t[i] == '{') break;
            if (wordAt(t, i, "switch")) f.switchHead = true;
        }
    }

    bool control = startsWithWord(t, "if") || startsWithWord(t, "for") ||
                   startsWithWord(t, "while") || startsWithWord(t, "do") ||
                   startsWithWord(t, "else") || startsWithWord(t, "switch");
    // Only C can have a head with its body still to come. Shalimar's blocks
    // are always braced, so nothing there ever hangs.
    if (c && control && !t.empty()) {
        char last = t[t.size() - 1];
        // '{' opens a block and counts itself; ';' is a body that was already
        // written, empty or not.
        f.controlHead = (last != '{' && last != ';' && last != '}');
    }

    // A label is a name and a colon and nothing else before it. The test for a
    // question mark is what keeps `x = a ? b : c` from looking like one.
    //
    // Shalimar is asked nothing here at all: ':' is its assignment, so 'x : 5'
    // has exactly this shape, and reading it as a label would put every
    // assignment in the function's own column.
    if (c && !f.caseLabel && !f.preprocessor && !t.empty() && !f.startsInComment) {
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

IndentState stateBefore(const std::vector<std::string>& lines, size_t row,
                        IndentDialect dialect) {
    IndentState state;
    for (size_t i = 0; i < row && i < lines.size(); ++i)
        state = advance(state, examine(lines[i], state, dialect));
    return state;
}

std::string indentFor(const std::vector<std::string>& lines, size_t row,
                      const IndentStyle& style) {
    if (row >= lines.size()) return std::string();
    IndentState before = stateBefore(lines, row, style.dialect);
    LineFacts f = examine(lines[row], before, style.dialect);
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
        LineFacts f = examine(lines[i], state, style.dialect);
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

    IndentState before = stateBefore(upto, upto.size() - 1, style.dialect);
    IndentState after = advance(before, examine(head, before, style.dialect));

    std::string tail = lines[row].substr(col > lines[row].size() ? lines[row].size() : col);
    std::string rest = withoutLeadingSpace(tail);

    LineFacts next = examine(rest, after, style.dialect);
    // The line about to be opened is empty because it is new, not because its
    // author left it blank - it takes the level of the place it opens in.
    if (rest.empty()) next.blank = false;
    int level = levelFor(after, next, style);
    if (level == kKeep) return std::string();
    return indentString(level, style);
}

}  // namespace editor
