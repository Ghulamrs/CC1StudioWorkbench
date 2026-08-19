#ifndef EDITOR_PROJECT_H
#define EDITOR_PROJECT_H

#include <string>
#include <vector>

#include "indent.h"
#include "toolchain.h"

namespace editor {

// A named set of files. Grouping is the project's own idea and has nothing to
// do with directories: a group may hold files from anywhere under the root,
// which is what makes moving one between groups a change to a list rather than
// a change to the disk.
struct Group {
    std::string name;
    std::vector<std::string> files;   // relative to the root, written with '/'
};

// ed1.json, and what it says. Six keys, flat except for the groups, and every
// one of them has a default - so the smallest project file that works is `{}`,
// and the editor works with no file at all:
//
//   {
//     "name": "Editor",
//     "indent": 4,
//     "tabs": false,
//     "toolchain": "auto",
//     "config": "debug",
//     "arch": "x86_64-windows",     // the machine's own when it is left out
//     "groups": {
//       "Examples": ["examples/hello.c", "examples/smart.cpp"]
//     }
//   }
//
// Two things are deliberately NOT in here. Where cc1 and cl live is a fact
// about a machine, not about a project, and a path written into a shared file
// is a path that is wrong on the other machine - those come from --cc1, --cl,
// $CC1 or PATH. And the indent settings are a number and a flag rather than an
// object, because an object with two members in it is a nest for no gain.
class Project {
public:
    Project();

    static const char* fileName();   // "ed1.json"

    // Looks for the file in `dir`. Absent is not an error - it means there is
    // no project, and the pane shows the directory instead.
    bool load(const std::string& dir, std::string& error);
    bool save(std::string& error);

    bool loaded() const { return loaded_; }
    const std::string& root() const { return root_; }
    // The directory paths are counted from, set even when there is no project
    // file - so the file operations work the same either way.
    void setRoot(const std::string& path) { root_ = path; }
    const std::string& file() const { return file_; }
    const std::string& name() const { return name_; }
    void setName(const std::string& name) { name_ = name; }

    const std::vector<Group>& groups() const { return groups_; }
    const IndentStyle& indent() const { return indent_; }
    ToolchainKind toolchain() const { return toolchain_; }
    Configuration config() const { return config_; }
    const std::string& arch() const { return arch_; }

    void setIndent(const IndentStyle& style) { indent_ = style; }
    void setToolchain(ToolchainKind kind) { toolchain_ = kind; }
    void setConfig(Configuration config) { config_ = config; }
    void setArch(const std::string& arch) { arch_ = arch; }

    // A project made up from a directory, with one group holding what is
    // already there. What "New project" writes.
    void begin(const std::string& dir, const std::string& name);

    // The shape a path may have: the root, or one directory under it, and no
    // deeper. Says why when the answer is no.
    //
    // Depth is the whole of the rule. As many directories as a project likes
    // may sit side by side on the ground floor - src, tests, examples, docs,
    // and any others - but none of them holds another. It is a rule the
    // project keeps rather than a habit people are asked to remember, because
    // a structure nobody has to explore is one anyone can read at a glance.
    static bool allows(const std::string& relative, std::string& why);

    // The directories in use, in the order they were first seen. Reported, not
    // limited.
    std::vector<std::string> directories() const;

    void addGroup(const std::string& group);
    bool addFile(const std::string& relative, const std::string& group);
    bool removeFile(const std::string& relative);   // from the list, not the disk
    bool renameFile(const std::string& from, const std::string& to);
    bool moveToGroup(const std::string& relative, const std::string& group);

    // Where a file sits, or groups().size() when it is not in the project.
    size_t groupOf(const std::string& relative) const;

    // The two directions between a path on disk and a path in the file.
    std::string absolute(const std::string& relative) const;
    std::string relative(const std::string& path) const;

private:
    bool loaded_;
    std::string root_;
    std::string file_;
    std::string name_;
    std::vector<Group> groups_;
    IndentStyle indent_;
    ToolchainKind toolchain_;
    Configuration config_;
    std::string arch_;
};

}  // namespace editor

#endif
