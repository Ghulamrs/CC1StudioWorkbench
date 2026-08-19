#include "workspace.h"

#include <cstdio>
#include <vector>

#include "path.h"

namespace editor {

namespace {

Outcome no(const std::string& why) {
    Outcome out;
    out.ok = false;
    out.message = why;
    return out;
}

Outcome yes(const std::string& said, const std::string& path = std::string()) {
    Outcome out;
    out.ok = true;
    out.message = said;
    out.path = path;
    return out;
}

std::string baseName(const std::string& path) {
    size_t at = path.find_last_of("/\\");
    return at == std::string::npos ? path : path.substr(at + 1);
}

// Writing the project back out is part of every change, so nothing can leave
// the list disagreeing with the disk. With no project file there is nothing to
// write, and the change on disk still stands.
Outcome andSave(Project& project, const std::string& said, const std::string& path) {
    if (!project.loaded()) return yes(said, path);
    std::string error;
    if (!project.save(error)) return no(error);
    return yes(said, path);
}

}  // namespace

Outcome createFile(Project& project, const std::string& relative,
                   const std::string& group) {
    std::string why;
    if (!Project::allows(relative, why)) return no(relative + ": " + why);

    std::string where = project.absolute(relative);
    if (path::exists(where)) return no(relative + " is already there");

    std::string parent = path::parent(where);
    if (!parent.empty()) path::makeDirectories(parent);

    FILE* made = std::fopen(where.c_str(), "wb");
    if (!made) return no("could not make " + relative);
    std::fclose(made);

    project.addFile(relative, group);
    return andSave(project, relative + " made", where);
}

Outcome renameFile(Project& project, const std::string& fromAbsolute,
                   const std::string& toRelative) {
    std::string why;
    if (!Project::allows(toRelative, why)) return no(toRelative + ": " + why);

    std::string to = project.absolute(toRelative);
    if (path::exists(to)) return no(toRelative + " is already there");

    std::string parent = path::parent(to);
    if (!parent.empty()) path::makeDirectories(parent);

    if (!path::rename(fromAbsolute, to))
        return no("could not rename " + baseName(fromAbsolute) + " to " + toRelative);

    project.renameFile(project.relative(fromAbsolute), toRelative);
    return andSave(project, baseName(fromAbsolute) + " is now " + toRelative, to);
}

namespace {

// The suffixes worth putting in a project made without being told. Anything
// else in the directory is left out rather than guessed at.
bool worthAdding(const std::string& name) {
    const char* const kinds[6] = {".c", ".h", ".cpp", ".hpp", ".cc", ".s"};
    for (size_t i = 0; i < 6; ++i) {
        size_t at = name.size();
        std::string suffix = kinds[i];
        if (at < suffix.size()) continue;
        if (name.compare(at - suffix.size(), suffix.size(), suffix) == 0) return true;
    }
    return false;
}

// The same directories the pane on the left refuses to show: build output and
// version control, which would bury the files anyone is looking for.
bool worthDescending(const std::string& name) {
    if (name.empty() || name[0] == '.') return false;
    return name != "obj" && name != "build" && name != "x64" && name != "Debug" &&
           name != "Release" && name != "node_modules";
}

}  // namespace

Outcome beginFromWhatIsThere(Project& project, const std::string& directory) {
    std::string root = path::absolute(directory);
    project.begin(root, path::filename(root));

    // Here, and one level down. Two levels is what a project path is allowed
    // anyway, so there would be nowhere to put anything deeper.
    size_t found = 0;
    std::vector<path::Entry> here = path::entries(root);
    for (size_t i = 0; i < here.size(); ++i) {
        if (!here[i].directory) {
            if (!worthAdding(here[i].name)) continue;
            project.addFile(here[i].name, "Sources");
            ++found;
            continue;
        }
        if (!worthDescending(here[i].name)) continue;

        std::vector<path::Entry> under = path::entries(path::join(root, here[i].name));
        for (size_t j = 0; j < under.size(); ++j) {
            if (under[j].directory || !worthAdding(under[j].name)) continue;
            project.addFile(here[i].name + "/" + under[j].name, "Sources");
            ++found;
        }
    }

    Outcome done = saveProject(project);
    if (!done.ok) return done;

    done.message = project.name() + " - no " + Project::fileName() + " here, so one was made";
    if (found > 0) {
        char many[32];
        std::snprintf(many, sizeof many, "%lu", static_cast<unsigned long>(found));
        done.message += std::string(" with ") + many + " file" + (found == 1 ? "" : "s");
    }
    return done;
}

Outcome deleteFile(Project& project, const std::string& absolute) {
    std::string relative = project.relative(absolute);

    if (!path::remove(absolute))
        return no("could not delete " + relative + " - it is still there");

    project.removeFile(relative);
    return andSave(project, relative + " deleted", std::string());
}

Outcome moveToGroup(Project& project, const std::string& absolute,
                    const std::string& group) {
    std::string relative = project.relative(absolute);

    // Not in the project yet means moving it in is the same as adding it.
    if (!project.moveToGroup(relative, group) && !project.addFile(relative, group))
        return no("could not move " + relative);

    return andSave(project, relative + " is in " + group, absolute);
}

Outcome addExisting(Project& project, const std::string& absolute,
                    const std::string& group) {
    std::string relative = project.relative(absolute);

    std::string why;
    if (!Project::allows(relative, why)) return no(relative + ": " + why);
    if (!project.addFile(relative, group)) return no(relative + " is already in the project");

    return andSave(project, relative + " added to " + group, absolute);
}

Outcome beginProject(Project& project, const std::string& directory,
                     const std::string& name, const std::string& firstFile) {
    project.begin(directory, name);
    if (!firstFile.empty()) {
        std::string relative = project.relative(firstFile);
        std::string why;
        if (Project::allows(relative, why)) project.addFile(relative, "Sources");
    }
    return andSave(project, std::string(Project::fileName()) + " written - " + name,
                   project.file());
}

Outcome saveProject(Project& project) {
    if (!project.loaded()) return no("there is no project to save");
    return andSave(project, project.file() + " written", project.file());
}

}  // namespace editor
