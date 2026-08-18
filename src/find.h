#ifndef EDITOR_FIND_H
#define EDITOR_FIND_H

#include <cstddef>
#include <string>
#include <vector>

namespace editor {

// Searching, kept away from the screen like every other rule here, so that the
// awkward parts - wrapping round the end, not finding the same place twice -
// can be checked without a terminal.
struct Match {
    bool found = false;
    size_t row = 0;
    size_t col = 0;
};

// The next occurrence at or after (row, col), wrapping round to the top and
// giving up when it arrives back where it started.
Match findNext(const std::vector<std::string>& lines, const std::string& needle,
               size_t row, size_t col);

// The one before (row, col), wrapping the other way.
Match findPrevious(const std::vector<std::string>& lines, const std::string& needle,
                   size_t row, size_t col);

// Replaces every occurrence and says how many. Occurrences do not overlap, and
// the replacement is never searched again - so replacing "a" with "aa" ends.
size_t replaceAll(std::vector<std::string>& lines, const std::string& needle,
                  const std::string& with);

}  // namespace editor

#endif
