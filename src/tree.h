#ifndef EDITOR_TREE_H
#define EDITOR_TREE_H

#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include "project.h"

namespace editor {

// The project down the left. Directories are read with std::filesystem rather
// than with opendir on one machine and FindFirstFile on the other - it is
// C++17, both compilers have it, and one implementation cannot drift from the
// other the way two would.
struct TreeEntry {
    std::string path;
    std::string name;
    bool directory = false;
    bool group = false;    // a project's group, which is not a directory
    bool open = false;
    int depth = 0;
};

class Tree {
public:
    Tree();

    void setRoot(const std::string& path);

    // Shows the project's groups instead of the directory. A group is not a
    // directory and nothing on disk matches it, which is the point: it is the
    // project's own arrangement of the same files.
    void showProject(const Project& project);
    const std::string& root() const { return root_; }
    const std::string& error() const { return error_; }

    const std::vector<TreeEntry>& entries() const { return entries_; }
    size_t size() const { return entries_.size(); }

    // Opens a directory, or closes one already open. A file is left to the
    // caller, which is the only place that knows what opening a file means.
    void toggle(size_t index);
    void reread();

    // Where a path sits in the list, or size() when it is not shown.
    size_t find(const std::string& path) const;

private:
    void gather(const std::string& dir, int depth);
    bool showingProject_;

    std::string root_;
    std::string error_;
    std::vector<TreeEntry> entries_;
    std::set<std::string> opened_;
};

}  // namespace editor

#endif
