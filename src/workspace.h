#ifndef EDITOR_WORKSPACE_H
#define EDITOR_WORKSPACE_H

#include <string>

#include "project.h"

namespace editor {

// What changing a project actually involves: a rule to check, something done
// on disk, the project's list kept in step, and the file written back out.
//
// It lives here rather than in either front end because both need all four and
// neither needs them differently. The terminal asks its questions on the
// message line and the window asks them in a dialog; what happens after the
// answer is this, once.
struct Outcome {
    bool ok = false;
    std::string message;   // what to tell whoever asked, either way
    std::string path;      // the file it ended up at, when there is one
};

// Makes an empty file and puts it in the project. `relative` is as the project
// counts paths - the two-level rule is checked before anything is written.
Outcome createFile(Project& project, const std::string& relative,
                   const std::string& group);

// Renames on disk and follows it in the project.
Outcome renameFile(Project& project, const std::string& fromAbsolute,
                   const std::string& toRelative);

// Removes from disk and from the project. The asking is the caller's; by the
// time this is reached the answer was yes.
Outcome deleteFile(Project& project, const std::string& absolute);

// Regrouping changes two lists and nothing on disk.
Outcome moveToGroup(Project& project, const std::string& absolute,
                    const std::string& group);

// Puts a file that already exists into the project.
Outcome addExisting(Project& project, const std::string& absolute,
                    const std::string& group);

Outcome beginProject(Project& project, const std::string& directory,
                     const std::string& name, const std::string& firstFile);
Outcome saveProject(Project& project);

}  // namespace editor

#endif
