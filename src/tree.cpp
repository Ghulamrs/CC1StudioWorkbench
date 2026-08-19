#include "tree.h"

#include <algorithm>

#include "path.h"

namespace editor {

namespace {

// Directories that are build output or version control. Showing them would
// bury the four files anyone is actually looking for.
bool skip(const std::string& name) {
    if (name.empty()) return true;
    if (name[0] == '.') return true;          // .git, .vs, and the rest
    return name == "obj" || name == "build" || name == "x64" || name == "Debug" ||
           name == "Release" || name == "node_modules";
}

}  // namespace

Tree::Tree() : showingProject_(false) {}

void Tree::setRoot(const std::string& where) {
    root_ = path::absolute(where);
    showingProject_ = false;
    opened_.clear();
    reread();
}

void Tree::showProject(const Project& project) {
    showingProject_ = true;
    root_ = project.root();
    error_.clear();
    entries_.clear();

    for (size_t i = 0; i < project.groups().size(); ++i) {
        const Group& group = project.groups()[i];

        TreeEntry head;
        head.name = group.name;
        // Marked with a name no path can collide with, so a group called src
        // and a directory called src do not open and close together.
        head.path = "group:" + group.name;
        head.directory = true;
        head.group = true;
        head.depth = 0;
        head.open = opened_.count(head.path) > 0 || opened_.empty();
        entries_.push_back(head);

        if (!head.open) continue;

        for (size_t j = 0; j < group.files.size(); ++j) {
            TreeEntry file;
            file.name = group.files[j];
            file.path = project.absolute(group.files[j]);
            file.depth = 1;
            entries_.push_back(file);
        }
    }
}

void Tree::reread() {
    if (showingProject_) return;   // the project decides what is shown, not the disk
    entries_.clear();
    error_.clear();
    if (root_.empty()) return;
    gather(root_, 0);
}

void Tree::gather(const std::string& dir, int depth) {
    bool readable = false;
    std::vector<path::Entry> found = path::entries(dir, &readable);
    if (!readable) {
        if (depth == 0) error_ = "cannot read " + dir;
        return;
    }

    // Directories first, then files, each in name order - the order a person
    // reads a project in, rather than the order the filesystem hands them over.
    std::sort(found.begin(), found.end(),
              [](const path::Entry& a, const path::Entry& b) {
                  if (a.directory != b.directory) return a.directory;
                  return a.name < b.name;
              });

    for (size_t i = 0; i < found.size(); ++i) {
        std::string name = found[i].name;
        if (skip(name)) continue;

        bool isDir = found[i].directory;

        TreeEntry entry;
        entry.path = path::join(dir, name);
        entry.name = name;
        entry.directory = isDir;
        entry.depth = depth;
        entry.open = isDir && opened_.count(entry.path) > 0;
        entries_.push_back(entry);

        if (entry.open) gather(entry.path, depth + 1);
    }
}

void Tree::toggle(size_t index) {
    if (index >= entries_.size()) return;
    TreeEntry entry = entries_[index];
    if (!entry.directory) return;

    if (entry.group) {
        // Groups start open, so the set holds the ones that are, and closing
        // the first one has to put every other group into it first.
        if (opened_.empty())
            for (size_t i = 0; i < entries_.size(); ++i)
                if (entries_[i].group) opened_.insert(entries_[i].path);
        if (opened_.count(entry.path))
            opened_.erase(entry.path);
        else
            opened_.insert(entry.path);
        return;   // the caller shows the project again
    }

    if (opened_.count(entry.path))
        opened_.erase(entry.path);
    else
        opened_.insert(entry.path);
    reread();
}

size_t Tree::find(const std::string& where) const {
    std::string want = path::absolute(where);
    for (size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].path == want) return i;
    return entries_.size();
}

}  // namespace editor
