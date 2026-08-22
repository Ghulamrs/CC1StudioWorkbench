#ifndef EDITOR_HELP_H
#define EDITOR_HELP_H

#include <string>
#include <vector>

namespace editor {
namespace help {

// The manual's table of contents, for Help > Contents.
//
// The pages themselves are Markdown in help/ and are not compiled in: ten
// pages of prose in a string table would be a second copy of the manual, and
// the house bug in these projects is a second copy that outlives the first.
// What is here is the contents - a number, a title, a line about it, and the
// file to read - which is what somebody at the keyboard actually wants: a
// reminder of what exists and where it is.
//
// `file` is checked against help/ by tests/test.cpp, so a page that is renamed
// or removed and not updated here is caught rather than left pointing at
// nothing.
struct Page {
    const char* number;   // "1".."10", or empty for the language pages
    const char* title;
    const char* about;    // one line, and it has to fit beside the title
    const char* file;     // relative to help/
};

const std::vector<Page>& pages();

// The contents as the panel shows it: a heading, the pages in two columns of
// text, and where to go next. Built here rather than in either front end, so
// the window and the terminal cannot disagree about it.
std::vector<std::string> contents();

}  // namespace help
}  // namespace editor

#endif
