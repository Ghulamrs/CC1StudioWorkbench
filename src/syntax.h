#ifndef EDITOR_SYNTAX_H
#define EDITOR_SYNTAX_H

#include <cstddef>
#include <string>
#include <vector>

namespace editor {

// Colouring, one kind per byte of a line. Like the indenter, this is text in
// and marks out - no screen, no escape codes - so the rules can be checked by a
// test rather than by looking at a terminal and deciding it seems about right.
const unsigned char KindNormal  = 0;
const unsigned char KindKeyword = 1;
const unsigned char KindType    = 2;
const unsigned char KindString  = 3;
const unsigned char KindChar    = 4;
const unsigned char KindComment = 5;
const unsigned char KindPreproc = 6;
const unsigned char KindNumber  = 7;
const unsigned char KindLabel   = 8;

enum Language {
    LangPlain = 0,
    LangC,
    LangCpp,
    LangShalimar,
    LangAsm,   // what cc1 and shc write, so the assembly tab is coloured too
    LangCount
};

// Chosen from the file's name. A .h is treated as C here rather than C++: this
// editor began as a C compiler's, and the C++ keywords it would add are the
// ones you least want highlighted in a C header.
//
// Shalimar answers to two suffixes. The app writes .shm and every program it
// ships is one; .shl is what the language is called on the desktop. Refusing
// either would mean a file that opens in one place and not the other.
Language languageFor(const std::string& path);
const char* languageName(Language lang);

// The suffix a new file of this language is given, without the dot.
const char* languageSuffix(Language lang);

// What a line leaves behind - a block comment, or a string carried on with a
// backslash. Highlighting a line needs the same running state the indenter
// needs, and for the same reason.
struct SyntaxState {
    bool comment = false;
    bool string = false;
};

// Moves `state` past a line without working out any colours. Drawing starts
// part way down a file, and the only way to know whether that line is inside a
// block comment is to have been past the ones above it - this does that at the
// cost of a character loop rather than a vector for every line skipped.
void advanceState(const std::string& line, Language lang, SyntaxState& state);

// One kind per byte of `line`, and `state` moved on to the next line.
std::vector<unsigned char> highlight(const std::string& line, Language lang,
                                     SyntaxState& state);

// The SGR body for a kind - "32" and so on. Sixteen-colour codes on purpose:
// every one of them works in a Windows console in virtual-terminal mode, which
// is not true of the 256-colour and true-colour forms.
const char* colourFor(unsigned char kind);

}  // namespace editor

#endif
