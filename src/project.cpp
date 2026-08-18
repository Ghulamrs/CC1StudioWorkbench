#include "project.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "json.h"

namespace fs = std::filesystem;

namespace editor {

namespace {

// Written with forward slashes whatever the machine, so a project file made on
// one opens on the other. Windows takes them everywhere it takes backslashes.
std::string withSlashes(const std::string& path) {
    std::string out = path;
    for (size_t i = 0; i < out.size(); ++i)
        if (out[i] == '\\') out[i] = '/';
    return out;
}

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
      arch_("x86_64-windows") {}

const char* Project::fileName() { return "ed1.json"; }

std::string Project::absolute(const std::string& rel) const {
    if (root_.empty()) return rel;
    return root_ + "/" + rel;
}

std::string Project::relative(const std::string& path) const {
    std::error_code ec;
    fs::path here = fs::absolute(fs::path(path), ec).lexically_normal();
    fs::path base = fs::absolute(fs::path(root_), ec).lexically_normal();
    fs::path out = here.lexically_relative(base);
    if (out.empty()) return withSlashes(path);
    return withSlashes(out.string());
}

void Project::begin(const std::string& dir, const std::string& name) {
    std::error_code ec;
    root_ = withSlashes(fs::absolute(fs::path(dir), ec).lexically_normal().string());
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

    std::error_code ec;
    std::string base = withSlashes(fs::absolute(fs::path(dir), ec).lexically_normal().string());
    std::string path = base + "/" + fileName();

    std::ifstream in(path.c_str());
    if (!in) return false;   // no project here, which is not a fault

    std::stringstream buffer;
    buffer << in.rdbuf();

    std::string why;
    Json root = Json::parse(buffer.str(), why);
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
    name_ = root.get("name").text(fs::path(base).filename().string());
    toolchain_ = toolchainFrom(root.get("toolchain").text("auto"));
    // Debug unless the file says otherwise: the one you want while the code is
    // still being written is the one you want by default.
    config_ = root.get("config").text("debug") == "release" ? ConfigRelease : ConfigDebug;
    arch_ = root.get("arch").text("x86_64-windows");

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

    std::ofstream out(file_.c_str());
    if (!out) {
        error = "cannot write " + file_;
        return false;
    }
    out << root.write() << "\n";
    if (!out) {
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

}  // namespace editor
