#ifndef EDITOR_ABOUT_H
#define EDITOR_ABOUT_H

#include <string>
#include <vector>

namespace editor {

// Who this is and whose it is, in one place because two front ends show it and
// nothing here is allowed to say a thing twice. The terminal prints these lines
// in its panel and the window puts them in a box; neither writes them out.
namespace about {

const char* name();      // the pair of programs, not either binary
const char* version();

// The notice, a line at a time and already laid out - blank lines included,
// since where they fall is part of how it reads.
//
// Seven lines, which is what the terminal's panel shows without scrolling. An
// eighth put the city out of sight, which is a poor way to name where someone
// is from.
std::vector<std::string> lines();

}  // namespace about
}  // namespace editor

#endif
