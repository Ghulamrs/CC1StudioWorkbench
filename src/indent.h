#ifndef EDITOR_INDENT_H
#define EDITOR_INDENT_H

#include <cstddef>
#include <string>
#include <vector>

namespace editor {

// Brace-directed layout, in the shape Shalimar's indenter settled on: text in,
// text out, no screen anywhere near it, so a test can reach every rule without
// a terminal. C needs four things Shalimar's language does not - escapes inside
// literals, block comments that outlive their line, the preprocessor, and
// switch labels - and each is a named rule below rather than a special case
// buried in the scanner.
//
// Which of the two is being laid out. Shalimar is not C with fewer rules by
// accident: two of its own punctuation marks would be read wrongly by the C
// scanner, and one of them silently. 'x : 5' is an assignment there and a goto
// label here, and a label is laid out in its function's own column - so a
// Shalimar program indented as C would walk left one statement at a time.
enum IndentDialect {
    DialectC = 0,
    DialectShalimar
};

struct IndentStyle {
    size_t width = 4;       // cc1's own sources indent by four, and have no tab in them
    bool tabs = false;
    // K&R puts a case label in its switch's own column, which is what an ANSI C
    // compiler's editor should default to. One step in is the other common taste.
    size_t caseIndent = 0;
    IndentDialect dialect = DialectC;
};

// What a line leaves behind for the line after it.
struct IndentState {
    int depth = 0;               // braces still open
    bool comment = false;        // a /* that has not met its */
    bool continued = false;      // the line ended in a backslash
    int hanging = 0;             // if/for/while heads still waiting for a statement
    std::vector<bool> inSwitch;  // for each open brace, whether a switch opened it
};

// What one line is, read from the line itself and the state it began in.
struct LineFacts {
    bool blank = false;
    bool preprocessor = false;
    bool caseLabel = false;
    bool gotoLabel = false;
    bool controlHead = false;    // if (x) with no brace and no semicolon after it
    bool switchHead = false;
    bool startsInComment = false;
    bool startsContinued = false;
    bool endsInComment = false;    // a /* left open for the next line
    bool endsContinued = false;    // a backslash carrying the line on
    size_t leadingCloses = 0;    // } before anything else on the line
    int opens = 0;
    int closes = 0;
    bool endsStatement = false;  // the last thing that is code is ; or }
    std::string code;            // the line with strings and comments taken out
};

LineFacts examine(const std::string& line, const IndentState& in,
                  IndentDialect dialect = DialectC);
IndentState advance(const IndentState& in, const LineFacts& f);

// The level a line sits at, in steps rather than columns. A line inside a block
// comment or carried over a backslash returns kKeep: its leading space is the
// author's and is not the indenter's to touch.
const int kKeep = -1;
int levelFor(const IndentState& in, const LineFacts& f, const IndentStyle& style);

std::string indentString(int level, const IndentStyle& style);
std::string withoutLeadingSpace(const std::string& line);

// The state the text arrives in at the start of `row`.
IndentState stateBefore(const std::vector<std::string>& lines, size_t row,
                        IndentDialect dialect = DialectC);

// The leading space `row` should have, or the line unchanged when it is kKeep.
std::string indentFor(const std::vector<std::string>& lines, size_t row,
                      const IndentStyle& style);

// Lays out every line. Existing leading space is discarded, except where a rule
// says to keep it.
std::vector<std::string> reindent(const std::vector<std::string>& lines,
                                  const IndentStyle& style);

// What a newline typed at `col` on `row` should insert after it. `tail` is the
// rest of the line, which matters: a } waiting on the other side of the caret
// closes the group the caret stands in, so the line about to begin belongs one
// step out - under the line that opened the group, not under its contents.
std::string indentAfterNewline(const std::vector<std::string>& lines, size_t row,
                               size_t col, const IndentStyle& style);

}  // namespace editor

#endif
