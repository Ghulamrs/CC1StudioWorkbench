#include "project.h"

#include <algorithm>
#include <cstdio>

#include "json.h"
#include "path.h"

namespace editor {

namespace {

// Written with forward slashes whatever the machine, so a project file made on
// one opens on the other. Windows takes them everywhere it takes backslashes.
// The turning round is in path.h now, since everything there works that way.
std::string withSlashes(const std::string& text) { return path::withSlashes(text); }

ToolchainKind toolchainFrom(const std::string& word) {
    if (word == "cc1") return ToolCc1;
    if (word == "msvc" || word == "cl") return ToolMsvc;
    return ToolAuto;
}

const char* toolchainWord(ToolchainKind kind) {
    if (kind == ToolCc1) return "cc1";
    if (kind == ToolMsvc) return "msvc";
    return "auto";
}

}  // namespace

Project::Project()
    : loaded_(false), toolchain_(ToolAuto), config_(ConfigDebug),
      arch_(hostArch()) {}

const char* Project::fileName() { return "ed1.json"; }

std::string Project::absolute(const std::string& rel) const {
    if (root_.empty()) return rel;
    return root_ + "/" + rel;
}

std::string Project::relative(const std::string& file) const {
    std::string out = path::relativeTo(file, root_);
    if (out.empty()) return withSlashes(file);
    return out;
}

void Project::begin(const std::string& dir, const std::string& name) {
    root_ = path::absolute(dir);
    file_ = root_ + "/" + fileName();
    name_ = name;
    groups_.clear();

    // One group to start with. Splitting a project into more of them is a
    // decision about the project, and not one the editor should make for you.
    Group all;
    all.name = "Sources";
    groups_.push_back(all);

    loaded_ = true;
}

bool Project::load(const std::string& dir, std::string& error) {
    error.clear();
    loaded_ = false;

    std::string base = path::absolute(dir);
    std::string path = base + "/" + fileName();

    // stdio, not <fstream> - see the note in buffer.cpp.
    FILE* in = std::fopen(path.c_str(), "rb");
    if (!in) return false;   // no project here, which is not a fault

    std::string text;
    char chunk[4096];
    size_t got;
    while ((got = std::fread(chunk, 1, sizeof chunk, in)) > 0) text.append(chunk, got);
    std::fclose(in);

    std::string why;
    Json root = Json::parse(text, why);
    if (!why.empty()) {
        error = std::string(fileName()) + ": " + why;
        return false;
    }
    if (!root.is(Json::Object)) {
        error = std::string(fileName()) + ": the file should hold one object";
        return false;
    }

    root_ = base;
    file_ = path;
    name_ = root.get("name").text(path::filename(base));
    toolchain_ = toolchainFrom(root.get("toolchain").text("auto"));
    // Debug unless the file says otherwise: the one you want while the code is
    // still being written is the one you want by default.
    config_ = root.get("config").text("debug") == "release" ? ConfigRelease : ConfigDebug;
    // The machine this is being opened on, unless the file names a target. It
    // used to default to x86_64-windows wherever it was opened, which quietly
    // made every project a cross build on the other two machines - the assembly
    // came out for a target this one cannot assemble, and Run had nothing to
    // start. A file that names a target still gets it.
    arch_ = root.get("arch").text(hostArch());

    indent_.width = static_cast<size_t>(root.get("indent").integer(4));
    if (indent_.width < 1 || indent_.width > 16) indent_.width = 4;
    indent_.tabs = root.get("tabs").boolean(false);

    // A group is a name and a list of files, which is exactly what an object
    // whose members are arrays already is. Anything more would be a shape to
    // learn before the file could be edited by hand.
    groups_.clear();
    const Json& groups = root.get("groups");
    for (size_t i = 0; i < groups.size(); ++i) {
        Group group;
        group.name = groups.keyAt(i);
        const Json& files = groups.valueAt(i);
        for (size_t j = 0; j < files.size(); ++j) {
            std::string relative = withSlashes(files.at(j).text());
            if (relative.empty()) continue;

            std::string reason;
            if (!allows(relative, reason)) {
                if (error.empty()) error = relative + ": " + reason;
                continue;
            }
            group.files.push_back(relative);
        }
        groups_.push_back(group);
    }
    if (groups_.empty()) {
        Group all;
        all.name = "Sources";
        groups_.push_back(all);
    }

    // What the project builds, if it says. Absent is the ordinary case and
    // means "nothing beyond the file in front of you", which is what every
    // project written before this said by saying nothing.
    target_ = Target();
    const Json& built = root.get("build");
    if (built.is(Json::Object)) {
        target_.name = built.get("target").text(name_);
        const Json& from = built.get("groups");
        for (size_t i = 0; i < from.size(); ++i) {
            std::string group = from.at(i).text();
            if (!group.empty()) target_.groups.push_back(group);
        }
    }

    loaded_ = true;
    return true;
}

bool Project::save(std::string& error) {
    error.clear();
    if (!loaded_) {
        error = "there is no project to save";
        return false;
    }

    Json root = Json::object();
    root.set("name", Json::fromText(name_));
    root.set("toolchain", Json::fromText(toolchainWord(toolchain_)));
    root.set("config", Json::fromText(configName(config_)));
    root.set("arch", Json::fromText(arch_));
    root.set("indent", Json::fromNumber(static_cast<double>(indent_.width)));
    root.set("tabs", Json::fromBool(indent_.tabs));

    Json groups = Json::object();
    for (size_t i = 0; i < groups_.size(); ++i) {
        Json files = Json::array();
        for (size_t j = 0; j < groups_[i].files.size(); ++j)
            files.push(Json::fromText(groups_[i].files[j]));
        groups.set(groups_[i].name, files);
    }
    root.set("groups", groups);

    // Written back only when there is one, so that a project which builds
    // nothing does not grow an empty object saying so every time a file is
    // added to it.
    if (builds()) {
        Json target = Json::object();
        target.set("target", Json::fromText(target_.name.empty() ? name_ : target_.name));
        Json from = Json::array();
        for (size_t i = 0; i < target_.groups.size(); ++i)
            from.push(Json::fromText(target_.groups[i]));
        target.set("groups", from);
        root.set("build", target);
    }

    std::string text = root.write() + "\n";

    FILE* out = std::fopen(file_.c_str(), "wb");
    if (!out) {
        error = "cannot write " + file_;
        return false;
    }
    size_t written = std::fwrite(text.data(), 1, text.size(), out);
    bool trouble = (written != text.size()) || std::ferror(out) != 0;
    if (std::fclose(out) != 0) trouble = true;

    if (trouble) {
        error = "cannot write " + file_;
        return false;
    }
    return true;
}

bool Project::allows(const std::string& rel, std::string& why) {
    std::string path = withSlashes(rel);
    why.clear();

    if (path.empty()) {
        why = "a file needs a name";
        return false;
    }
    if (path[0] == '/' || (path.size() > 1 && path[1] == ':')) {
        why = "that is an absolute path - files live inside the project";
        return false;
    }
    if (path.find("..") != std::string::npos) {
        why = "no going up out of the project";
        return false;
    }
    if (path[path.size() - 1] == '/') {
        why = "that is a directory, not a file";
        return false;
    }

    size_t depth = 0;
    for (size_t i = 0; i < path.size(); ++i)
        if (path[i] == '/') ++depth;

    if (depth > 1) {
        why = "two levels at most: name.c, or one directory and name.c";
        return false;
    }
    return true;
}

std::vector<std::string> Project::directories() const {
    std::vector<std::string> found;
    for (size_t i = 0; i < groups_.size(); ++i) {
        for (size_t j = 0; j < groups_[i].files.size(); ++j) {
            const std::string& file = groups_[i].files[j];
            size_t slash = file.find('/');
            if (slash == std::string::npos) continue;   // a file on the ground

            std::string dir = file.substr(0, slash);
            bool seen = false;
            for (size_t k = 0; k < found.size(); ++k)
                if (found[k] == dir) seen = true;
            if (!seen) found.push_back(dir);
        }
    }
    return found;
}

void Project::addGroup(const std::string& group) {
    for (size_t i = 0; i < groups_.size(); ++i)
        if (groups_[i].name == group) return;

    Group made;
    made.name = group;
    groups_.push_back(made);
}

size_t Project::groupOf(const std::string& rel) const {
    std::string want = withSlashes(rel);
    for (size_t i = 0; i < groups_.size(); ++i)
        for (size_t j = 0; j < groups_[i].files.size(); ++j)
            if (groups_[i].files[j] == want) return i;
    return groups_.size();
}

bool Project::addFile(const std::string& rel, const std::string& group) {
    std::string want = withSlashes(rel);
    std::string why;
    if (!allows(want, why)) return false;
    if (groupOf(want) < groups_.size()) return false;   // already in the project

    addGroup(group.empty() ? std::string("Sources") : group);
    std::string into = group.empty() ? std::string("Sources") : group;

    for (size_t i = 0; i < groups_.size(); ++i) {
        if (groups_[i].name != into) continue;
        groups_[i].files.push_back(want);
        std::sort(groups_[i].files.begin(), groups_[i].files.end());
        return true;
    }
    return false;
}

bool Project::removeFile(const std::string& rel) {
    std::string want = withSlashes(rel);
    for (size_t i = 0; i < groups_.size(); ++i) {
        std::vector<std::string>& files = groups_[i].files;
        for (size_t j = 0; j < files.size(); ++j) {
            if (files[j] != want) continue;
            files.erase(files.begin() + static_cast<long>(j));
            return true;
        }
    }
    return false;
}

bool Project::renameFile(const std::string& from, const std::string& to) {
    std::string was = withSlashes(from);
    std::string now = withSlashes(to);
    std::string why;
    if (!allows(now, why)) return false;
    for (size_t i = 0; i < groups_.size(); ++i) {
        std::vector<std::string>& files = groups_[i].files;
        for (size_t j = 0; j < files.size(); ++j) {
            if (files[j] != was) continue;
            files[j] = now;
            std::sort(files.begin(), files.end());
            return true;
        }
    }
    return false;
}

bool Project::moveToGroup(const std::string& rel, const std::string& group) {
    std::string want = withSlashes(rel);
    size_t from = groupOf(want);
    if (from >= groups_.size()) return false;

    // Regrouping is a change to two lists and nothing else. Nothing on disk
    // moves, which is the point of groups being the project's own idea.
    if (!removeFile(want)) return false;
    addGroup(group);
    for (size_t i = 0; i < groups_.size(); ++i) {
        if (groups_[i].name != group) continue;
        groups_[i].files.push_back(want);
        std::sort(groups_[i].files.begin(), groups_[i].files.end());
        return true;
    }
    return false;
}

// The sources a program is made of, and the language they are in. Headers are
// passed over - they are in the project to be opened, not to be compiled - and
// so is anything that is neither C nor C++.
bool Project::targetSources(std::vector<std::string>& sources, Language& lang,
                            std::string& why, std::string* detail) const {
    sources.clear();
    lang = LangPlain;
    why.clear();
    if (detail) detail->clear();

    if (!builds()) {
        why = std::string("this project does not say what it builds");
        if (detail)
            *detail = std::string("Add a \"build\" entry to ") + fileName() +
                      " naming the program and the groups its sources are in, like "
                      "\"build\": { \"target\": \"" + name_ +
                      "\", \"groups\": [\"Sources\"] }. Until then, Ctrl-B still "
                      "compiles the file in front of you, which needs no project at all.";
        return false;
    }

    bool sawC = false, sawCpp = false, sawShalimar = false;
    for (size_t i = 0; i < target_.groups.size(); ++i) {
        size_t at = groups_.size();
        for (size_t g = 0; g < groups_.size(); ++g)
            if (groups_[g].name == target_.groups[i]) { at = g; break; }

        if (at == groups_.size()) {
            why = "no such group in this project: " + target_.groups[i];
            if (detail)
                *detail = std::string("The \"build\" entry in ") + fileName() +
                          " names a group the project does not have. Groups are the "
                          "headings in the pane on the left.";
            sources.clear();
            return false;
        }

        for (size_t f = 0; f < groups_[at].files.size(); ++f) {
            const std::string& relative = groups_[at].files[f];
            // What decides is the extension, because that is what the
            // compilers go by. A .h is C by name and is still not a source.
            size_t dot = relative.find_last_of('.');
            std::string suffix = (dot == std::string::npos) ? std::string()
                                                            : relative.substr(dot);
            if (suffix == ".c") sawC = true;
            else if (suffix == ".cpp" || suffix == ".cc" || suffix == ".cxx") sawCpp = true;
            else if (suffix == ".shl" || suffix == ".shm") sawShalimar = true;
            else continue;

            sources.push_back(absolute(relative));
        }
    }

    // Named in the project and not on disk. The compiler would say "cannot
    // open" and stop, with no line to go to and nothing about the project;
    // this is a fault in the configuration, and the editor is the one holding
    // the list.
    std::vector<std::string> gone;
    for (size_t i = 0; i < sources.size(); ++i)
        if (!path::exists(sources[i])) gone.push_back(relative(sources[i]));

    if (!gone.empty()) {
        why = gone[0] + " is in this project and not on disk";
        if (gone.size() > 1) {
            why += " (and " + std::to_string(gone.size() - 1) +
                   (gone.size() == 2 ? " other" : " others") + ")";
        }
        if (detail) {
            *detail = std::string("The build list in ") + fileName() +
                      " names files that are not there: ";
            for (size_t i = 0; i < gone.size(); ++i) {
                if (i) *detail += ", ";
                *detail += gone[i];
            }
            *detail += ". Put them back, or take them out of the group - a project that "
                       "lists a file it has not got cannot be built from.";
        }
        sources.clear();
        return false;
    }

    // Naming them is the point of the message. "more than one language" tells
    // whoever is reading nothing they did not already suspect; "both C and
    // C++" tells them which file to move.
    std::vector<std::string> found;
    if (sawC) found.push_back("C");
    if (sawCpp) found.push_back("C++");
    if (sawShalimar) found.push_back("Shalimar");

    if (found.size() > 1) {
        std::string named = found[0];
        for (size_t i = 1; i < found.size(); ++i)
            named += (i + 1 == found.size() ? " and " : ", ") + found[i];
        why = "this project holds " + (found.size() == 2 ? std::string("both ")
                                                         : std::string()) +
              named + ", which cannot make one program";
        if (detail)
            *detail = "Each of them has its own compiler - cc1 for C, cl for C++, shc "
                      "for Shalimar - and there is no one compiler here to give a "
                      "program made of two of them to. Put each in a project of its "
                      "own, or build them a file at a time with Ctrl-B, which never "
                      "asks what the project says.";
        sources.clear();
        return false;
    }
    if (sources.empty()) {
        why = "the groups this project builds from hold no source";
        if (detail)
            *detail = "A group can hold anything - headers, notes, a Makefile - and none "
                      "of that is compiled. Name a group with .c, .cpp or .shl files in "
                      "it.";
        return false;
    }

    if (sawShalimar) {
        lang = LangShalimar;
        return oneShalimarProgram(sources, why, detail);
    }
    lang = sawCpp ? LangCpp : LangC;
    return true;
}

// Which of a Shalimar target's sources is the program, put first.
//
// The others are not dropped: shc looks for a function the program calls and
// does not define in the files named after it, so the rest of the group is
// exactly the place to look. What the project decides is which one has the
// main() - the language has no way to say so from inside the file, since
// every program has one.
//
// With one source there is nothing to decide. With more, the one whose name
// matches the target's is the program, and if none does this refuses and says
// which it was choosing between. Taking the first silently would build a
// different program from the one the name promised, and say nothing.
bool Project::oneShalimarProgram(std::vector<std::string>& sources, std::string& why,
                                 std::string* detail) const {
    if (sources.size() == 1) return true;

    const std::string wanted = target_.name.empty() ? name_ : target_.name;
    size_t at = sources.size();
    for (size_t i = 0; i < sources.size(); ++i) {
        std::string leaf = path::filename(sources[i]);
        size_t dot = leaf.find_last_of('.');
        if (dot != std::string::npos) leaf.resize(dot);
        if (leaf != wanted) continue;
        if (at != sources.size()) { at = sources.size(); break; }   // two of them
        at = i;
    }

    if (at < sources.size()) {
        std::swap(sources[0], sources[at]);
        return true;
    }

    why = "this project has " + std::to_string(sources.size()) +
          " Shalimar programs and builds one";
    if (detail) {
        *detail = "Every Shalimar file has a main(), so the project is what says which "
                  "one is the program; the others are where shc looks for what it calls "
                  "and does not define. Name the target after the one to build - "
                  "\"build\": { \"target\": \"" +
                  (sources.empty() ? std::string("name")
                                   : stemOf(path::filename(sources[0]))) +
                  "\" } - or build any of them with Ctrl-B, which never asks what the "
                  "project says. This target is called \"" + wanted +
                  "\" and no source here is.";
    }
    sources.clear();
    return false;
}

// A file name without its suffix.
std::string Project::stemOf(const std::string& leaf) {
    size_t dot = leaf.find_last_of('.');
    return dot == std::string::npos ? leaf : leaf.substr(0, dot);
}

std::string Project::targetProgram() const {
    std::string name = target_.name.empty() ? name_ : target_.name;
    if (name.empty()) name = "program";
#ifdef _WIN32
    if (name.size() < 4 || name.compare(name.size() - 4, 4, ".exe") != 0) name += ".exe";
#endif
    return path::join(root_, name);
}

}  // namespace editor
