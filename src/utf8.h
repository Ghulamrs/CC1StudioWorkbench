#ifndef EDITOR_UTF8_H
#define EDITOR_UTF8_H

#include <cstddef>
#include <string>

namespace editor {

// UTF-8, to the extent an editor needs it: never land the caret inside a
// character, never delete half of one, and count the columns a line really
// takes on screen.
//
// The buffer stays bytes throughout. That is deliberate - a file opened and
// saved comes back byte for byte, whatever is in it - and these say where the
// boundaries are rather than converting anything.
namespace utf8 {

// A byte that continues a character rather than starting one.
bool isContinuation(unsigned char byte);

// How many bytes the character starting with this byte takes: 1 for ASCII,
// 2 to 4 otherwise, and 1 for anything malformed so that nothing gets stuck.
size_t lengthFrom(unsigned char lead);

// The start of the character containing `at`, moving back if `at` is inside one.
size_t startOf(const std::string& text, size_t at);

// The next and previous character boundaries.
size_t next(const std::string& text, size_t at);
size_t previous(const std::string& text, size_t at);

// How many characters are in the text.
size_t count(const std::string& text);

// The screen columns the first `upTo` bytes take. Most characters are one
// column; the wide ones - Chinese, Japanese, Korean, fullwidth forms, emoji -
// are two, and combining marks are none, because they are drawn on top of what
// came before rather than beside it.
size_t columns(const std::string& text, size_t upTo);

// The code point at a boundary, or 0 at the end.
unsigned long codePointAt(const std::string& text, size_t at);

// How many columns one character takes.
size_t widthOf(unsigned long codePoint);

}  // namespace utf8
}  // namespace editor

#endif
