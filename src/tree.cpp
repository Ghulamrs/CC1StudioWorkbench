#include "tree.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

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

void Tree::setRoot(const std::string& path) {
    std::error_code ec;
    fs::path p = fs::absolute(fs::path(path), ec);
    root_ = ec ? path : p.lexically_normal().string();
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
    std::error_code ec;
    std::vector<fs::directory_entry> found;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::directory_iterator(); it.increment(ec)) {
        found.push_back(*it);
    }
    if (ec && found.empty()) {
        if (depth == 0) error_ = "cannot read " + dir;
        return;
    }

    // Directories first, then files, each in name order - the order a person
    // reads a project in, rather than the order the filesystem hands them over.
    std::sort(found.begin(), found.end(),
              [](const fs::directory_entry& a, const fs::directory_entry& b) {
                  std::error_code e1, e2;
                  bool da = a.is_directory(e1), db = b.is_directory(e2);
                  if (da != db) return da;
                  return a.path().filename().string() < b.path().filename().string();
              });

    for (size_t i = 0; i < found.size(); ++i) {
        std::string name = found[i].path().filename().string();
        if (skip(name)) continue;

        std::error_code e;
        bool isDir = found[i].is_directory(e);

        TreeEntry entry;
        entry.path = found[i].path().string();
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

size_t Tree::find(const std::string& path) const {
    std::error_code ec;
    std::string want = fs::absolute(fs::path(path), ec).lexically_normal().string();
    for (size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].path == want) return i;
    return entries_.size();
}

}  // namespace editor
