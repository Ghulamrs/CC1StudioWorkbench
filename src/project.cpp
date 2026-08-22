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
    if (word == "shc") return ToolShc;
    return ToolAuto;
}

const char* toolchainWord(ToolchainKind kind) {
    if (kind == ToolCc1) return "cc1";
    if (kind == ToolMsvc) return "msvc";
    if (kind == ToolShc) return "shc";
    return "auto";
}

// What the suffix says, which is what the compilers go by. A .h is C by name
// and is still not a source, so it comes back as LangPlain and is skipped.
Language languageOf(const std::string& relative) {
    size_t dot = relative.find_last_of('.');
    if (dot == std::string::npos) return LangPlain;
    std::string suffix = relative.substr(dot);
    if (suffix == ".c") return LangC;
    if (suffix == ".cpp" || suffix == ".cc" || suffix == ".cxx") return LangCpp;
    if (suffix == ".shl" || suffix == ".shm") return LangShalimar;
    return LangPlain;
}

const char* languageWord(Language lang) {
    if (lang == LangC) return "C";
    if (lang == LangCpp) return "C++";
    if (lang == LangShalimar) return "Shalimar";
    return "text";
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
        // Two spellings, and the plain one is not deprecated. A group that has
        // nothing to say about its compiler is a list of files and is written
        // back as one; a group that names a compiler needs somewhere to put
        // the name, and an object is that somewhere.
        //
        //   "Sources":  ["src/main.c"]
        //   "Engine":   { "files": ["src/engine.cpp"], "toolchain": "cl" }
        const Json& entry = groups.valueAt(i);
        const Json& files = entry.is(Json::Object) ? entry.get("files") : entry;
        if (entry.is(Json::Object))
            group.toolchain = toolchainFrom(entry.get("toolchain").text("auto"));
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
        // The plain array unless there is something to say, so that adding a
        // file to a project written before any of this existed leaves the file
        // looking the way its author left it.
        if (groups_[i].toolchain == ToolAuto) {
            groups.set(groups_[i].name, files);
        } else {
            Json named = Json::object();
            named.set("files", files);
            named.set("toolchain", Json::fromText(toolchainWord(groups_[i].toolchain)));
            groups.set(groups_[i].name, named);
        }
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
ToolchainKind toolchainOf(const Toolchain& tool, const Part& part) {
    // The group's word beats the language's, and the editor's own --cc1 or
    // Language menu still beats both - that last one is inside resolve, and is
    // why this asks it rather than working the answer out here.
    if (part.toolchain != ToolAuto) return part.toolchain;
    return resolve(tool, part.lang);
}

ToolchainKind Project::toolchainFor(const std::string& group) const {
    for (size_t i = 0; i < groups_.size(); ++i)
        if (groups_[i].name == group) return groups_[i].toolchain;
    return ToolAuto;
}

void Project::setGroupToolchain(const std::string& group, ToolchainKind kind) {
    for (size_t i = 0; i < groups_.size(); ++i)
        if (groups_[i].name == group) { groups_[i].toolchain = kind; return; }
}

bool Project::targetParts(std::vector<Part>& parts, std::string& why,
                          std::string* detail) const {
    parts.clear();
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

    std::vector<std::string> gone;

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
            parts.clear();
            return false;
        }

        const Group& group = groups_[at];

        // What is in it, by language and in the order the group names them.
        std::vector<std::string> byLanguage[LangCount];
        bool sawShalimar = false, sawOther = false;
        for (size_t f = 0; f < group.files.size(); ++f) {
            Language lang = languageOf(group.files[f]);
            if (lang == LangPlain) continue;
            if (lang == LangShalimar) sawShalimar = true; else sawOther = true;
            std::string full = absolute(group.files[f]);
            if (!path::exists(full)) gone.push_back(group.files[f]);
            byLanguage[lang].push_back(full);
        }

        // Shalimar shares a group with nothing, whatever the group says about
        // a compiler, because no compiler here takes both - and because the
        // rest of the refusal below is about the *link*, which this one never
        // reaches. Said per group, since that is where the fix is.
        if (sawShalimar && sawOther) {
            why = target_.groups[i] + " holds Shalimar and C or C++ in one group";
            if (detail)
                *detail = "No compiler takes both, so this is not a matter of naming one: "
                          "shc reads Shalimar and nothing else, and cc1 and cl read C and "
                          "C++ and not Shalimar. Put the Shalimar in a group of its own.";
            parts.clear();
            return false;
        }

        // A group that names a compiler is one part, and that compiler takes
        // whatever is in it. Where the group holds C and C++ both, the part is
        // C++: cl compiles C as C++ under /TP, and somebody who wrote
        // "toolchain": "cl" on a group holding both asked for exactly that.
        // A compiler that cannot take the result says so itself, in canCompile's
        // own words, rather than being second-guessed here.
        if (group.toolchain != ToolAuto) {
            Part part;
            part.group = group.name;
            part.toolchain = group.toolchain;
            part.lang = LangPlain;
            for (int l = 0; l < LangCount; ++l) {
                if (byLanguage[l].empty()) continue;
                if (part.lang == LangPlain || l == LangCpp)
                    part.lang = static_cast<Language>(l);
                for (size_t f = 0; f < byLanguage[l].size(); ++f)
                    part.sources.push_back(byLanguage[l][f]);
            }
            if (!part.sources.empty()) parts.push_back(part);
            continue;
        }

        // And a group that names none is split by language, one part each, in
        // the order the languages are numbered rather than the order the files
        // happened to be listed - so the same project gives the same command
        // line whichever way somebody sorted their group.
        for (int l = 0; l < LangCount; ++l) {
            if (byLanguage[l].empty()) continue;
            Part part;
            part.group = group.name;
            part.toolchain = ToolAuto;
            part.lang = static_cast<Language>(l);
            part.sources = byLanguage[l];
            parts.push_back(part);
        }
    }

    // Named in the project and not on disk. The compiler would say "cannot
    // open" and stop, with no line to go to and nothing about the project;
    // this is a fault in the configuration, and the editor is the one holding
    // the list.
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
        parts.clear();
        return false;
    }

    if (parts.empty()) {
        why = "the groups this project builds from hold no source";
        if (detail)
            *detail = "A group can hold anything - headers, notes, a Makefile - and none "
                      "of that is compiled. Name a group with .c, .cpp or .shl files in "
                      "it.";
        return false;
    }

    // Shalimar among the rest. This is the one refusal that is not about the
    // editor's tidiness and not about a missing feature either: it is what a
    // Shalimar object *is*. Every one of them exports shm_user_main,
    // shm_init_globals and shm_name_files whatever file it came from, so two
    // collide by construction; a Shalimar function reads its own file's
    // globals, which only that file's shm_init_globals sets up; and the
    // language has no declarations, so a call that crossed a link could not be
    // checked - and checking the whole program together is the rule the
    // cross-file search exists to keep. ../Compiler-S/docs/LINKING.md has it in
    // full, with what would have to change.
    bool shalimar = false;
    for (size_t i = 0; i < parts.size(); ++i)
        if (parts[i].lang == LangShalimar) shalimar = true;

    if (shalimar && parts.size() > 1) {
        why = "Shalimar makes a whole program, so it cannot be part of one";
        if (detail)
            *detail = "shc compiles, assembles and links in one step, and a Shalimar "
                      "object is not a piece of something larger: whichever file it came "
                      "from it exports the same three startup symbols, so two of them "
                      "collide, and the language has no declarations, so a call across a "
                      "link could not be checked. Give the Shalimar its own project, or "
                      "take it out of this target's groups and build it with Ctrl-B. "
                      "See Compiler-S/docs/LINKING.md.";
        parts.clear();
        return false;
    }

    if (shalimar) {
        std::vector<std::string>& only = parts[0].sources;
        if (!oneShalimarProgram(only, why, detail)) {
            parts.clear();
            return false;
        }
    }
    return true;
}

bool Project::targetSources(std::vector<std::string>& sources, Language& lang,
                            std::string& why, std::string* detail) const {
    sources.clear();
    lang = LangPlain;

    std::vector<Part> parts;
    if (!targetParts(parts, why, detail)) return false;

    // One compiler, or this is not the question to be asking. Everything that
    // takes several compilers goes through targetParts and links; this is for
    // whoever wants the older answer - one command, one language - and it says
    // so rather than picking a part and being quietly wrong about the rest.
    if (parts.size() > 1) {
        std::vector<std::string> named;
        for (size_t i = 0; i < parts.size(); ++i) {
            std::string word = languageWord(parts[i].lang);
            bool already = false;
            for (size_t j = 0; j < named.size(); ++j)
                if (named[j] == word) already = true;
            if (!already) named.push_back(word);
        }
        std::string all = named.empty() ? std::string() : named[0];
        for (size_t i = 1; i < named.size(); ++i)
            all += (i + 1 == named.size() ? " and " : ", ") + named[i];
        why = "this target holds " + all + ", so it takes more than one compiler";
        if (detail)
            *detail = "That is built rather than refused - each group goes to the compiler "
                      "that can take it and the objects meet at the linker - but whatever "
                      "asked this question wanted one command and one language, and there "
                      "is no honest single answer to give it.";
        return false;
    }

    sources = parts[0].sources;
    lang = parts[0].lang;
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
