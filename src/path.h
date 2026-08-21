#ifndef EDITOR_PATH_H
#define EDITOR_PATH_H

#include <string>
#include <vector>

namespace editor {

// The few things this editor asks of a filesystem, written out rather than
// taken from <filesystem>.
//
// <filesystem> is C++17 and this is C++14, which is what cc1 and everything
// around it is written in. What was actually used of it was small - joining and
// splitting paths, making a path absolute or relative to another, asking
// whether something is there, listing a directory, and four operations on
// files - so it is here instead, in the two spellings the three machines need.
//
// Everything works in forward slashes and hands them back, whatever the
// machine, exactly as the project file already did: Windows takes them
// everywhere it takes backslashes, and a path written on one machine then opens
// on the other. A backslash that arrives here is turned round on the way in.
//
// Paths are std::string, and on Windows they reach the narrow API as they are -
// which is what std::filesystem did with a narrow string too, so a name outside
// the machine's own code page is no worse off than it was.
namespace path {

std::string withSlashes(const std::string& path);

// Absolute, with . and .. taken out. Relative paths are read against the
// directory the editor is running in. Never fails: a path it cannot make sense
// of comes back as it went in, since every caller wants a string to compare or
// to show rather than an error to report.
std::string absolute(const std::string& path);

// The way from base to path, with .. for each step up - what a project file
// holds. Empty when there is no way, which is two different roots on Windows
// and cannot happen anywhere else.
std::string relativeTo(const std::string& path, const std::string& base);

// One name for a file, for comparing two paths or filing something under one.
// Two spellings of the same file have to answer the same here - as typed, as
// the pane hands it out, absolute or relative, forward slashes or back - or the
// editor opens a second tab on a file it already has, or keeps a breakpoint
// under a name nothing else ever asks for.
//
// Never shown to anybody. On Windows it is lower-cased, because the filesystem
// does not care about case and a person reading their own filename does.
std::string oneName(const std::string& path);

// Whether two paths are the same file, by the rule above.
bool same(const std::string& one, const std::string& other);

std::string parent(const std::string& path);
std::string filename(const std::string& path);
std::string join(const std::string& directory, const std::string& leaf);

bool exists(const std::string& path);
bool isDirectory(const std::string& path);

// Every directory on the way, like mkdir -p. True when the directory is there
// afterwards, whether or not this is what made it.
bool makeDirectories(const std::string& path);

bool rename(const std::string& from, const std::string& to);

// One file. Not a directory, and not what is under one - see removeTree, which
// says what it does in its name.
bool remove(const std::string& path);

// A directory and everything in it, or a single file. Refuses to walk into a
// symbolic link, and refuses a root outright: a recursive delete with an empty
// or careless path is the one mistake here that cannot be taken back.
bool removeTree(const std::string& path);

std::string tempDir();

// The directory the running program is in - not the one it was started from,
// which is a different question and already has an answer in absolute(). The
// machine is asked outright rather than argv[0] being read: a program started
// through PATH is handed a bare name, and one started through a link is handed
// the link. Empty when the machine will not say.
std::string programDirectory();

// A program of this name sitting beside the running one: its path when it is
// there, empty when it is not. On Windows ".exe" is added, that being what a
// program is called there.
//
// This is how the editor finds a compiler that was installed alongside it -
// `make product` and `build.bat product` put one directory together holding
// what you would actually run, and a compiler in it should be found by the
// editor next to it whatever directory the editor was started in.
std::string besideProgram(const std::string& name);

// Where this person's own files live, which is where anything the editor
// remembers between sessions belongs - it is about them and not about any one
// project. Empty when the machine will not say.
std::string homeDir();

struct Entry {
    std::string name;
    bool directory;

    Entry() : directory(false) {}
};

// What is in a directory, in the order the filesystem hands it over, without
// . and .. - sorting is the caller's business. `ok` says whether the directory
// could be read at all, which is not the same as its being empty.
std::vector<Entry> entries(const std::string& directory, bool* ok = 0);

}  // namespace path
}  // namespace editor

#endif
