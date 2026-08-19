#include "workspace.h"

#include <cstdio>
#include <cstring>
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

bool endsWith(const std::string& name, const std::string& suffix) {
    if (name.size() < suffix.size()) return false;
    return name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// The suffixes worth putting in a project made without being told. Anything
// else in the directory is left out rather than guessed at.
bool worthAdding(const std::string& name) {
    const char* const kinds[6] = {".c", ".h", ".cpp", ".hpp", ".cc", ".s"};
    for (size_t i = 0; i < 6; ++i)
        if (endsWith(name, kinds[i])) return true;
    return false;
}

// Which of the two groups it belongs in. What you declare and what you write
// are different things to look at, and a project made without being told
// should still put them in different places.
bool isHeader(const std::string& name) {
    return endsWith(name, ".h") || endsWith(name, ".hpp");
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

    // Both groups exist before anything is found, so a project with no headers
    // yet has the place to put the first one rather than needing the group made
    // at the same moment as the file.
    project.addGroup("Headers");

    // Here, and one level down. Two levels is what a project path is allowed
    // anyway, so there would be nowhere to put anything deeper.
    size_t found = 0;
    std::vector<path::Entry> here = path::entries(root);
    for (size_t i = 0; i < here.size(); ++i) {
        if (!here[i].directory) {
            if (!worthAdding(here[i].name)) continue;
            project.addFile(here[i].name, isHeader(here[i].name) ? "Headers" : "Sources");
            ++found;
            continue;
        }
        if (!worthDescending(here[i].name)) continue;

        std::vector<path::Entry> under = path::entries(path::join(root, here[i].name));
        for (size_t j = 0; j < under.size(); ++j) {
            if (under[j].directory || !worthAdding(under[j].name)) continue;
            project.addFile(here[i].name + "/" + under[j].name,
                            isHeader(under[j].name) ? "Headers" : "Sources");
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

// Small on purpose, and every line of it is doing something you can watch: the
// numbers appear one at a time as you step, which is the whole of what a
// debugger is for.
//
// The same program is in examples/projectile.c, where it can be read and built
// like any other file. It is a string here as well because a first run has to
// be able to write it without knowing where this editor was installed.
//
// The guard around M_PI is not decoration. It is in <math.h> on a Mac and on
// Linux, and MSVC keeps it behind _USE_MATH_DEFINES - so without this the
// first program a first run opens would not compile on one of the three
// machines it is written for.
//
// Which also means the value here is only ever used on Windows, and 3.14 is
// enough for it: the program prints two decimal places, and it prints the same
// two - 2.88, 10.19, 40.77 - whichever value of pi it was given.
const char* const kDemoProgram =
    "#include <stdio.h>\n"
    "#include <math.h>\n"
    "\n"
    "/* Projectile motion - and something to try the debugger on.\n"
    "\n"
    "   F5 builds and runs it. F9 on a line puts a breakpoint there, F8 starts\n"
    "   it and stops on that line, and F7 steps a line at a time - watch the\n"
    "   Debug tab as rad, t_flight, h_max and range are worked out. */\n"
    "\n"
    "#ifndef M_PI\n"
    "#define M_PI 3.14   /* MSVC hides the real one behind _USE_MATH_DEFINES */\n"
    "#endif\n"
    "\n"
    "int main(void) {\n"
    "    double v0 = 20.0;     // initial velocity (m/s)\n"
    "    double angle = 45.0;  // launch angle (degrees)\n"
    "    double g = 9.81;      // gravity (m/s^2)\n"
    "\n"
    "    double rad = angle * M_PI / 180.0;\n"
    "    double t_flight = 2 * v0 * sin(rad) / g;\n"
    "    double h_max = (v0 * v0 * pow(sin(rad), 2)) / (2 * g);\n"
    "    double range = (v0 * v0 * sin(2 * rad)) / g;\n"
    "\n"
    "    printf(\"Time of flight: %.2f s\\n\", t_flight);\n"
    "    printf(\"Max height: %.2f m\\n\", h_max);\n"
    "    printf(\"Range: %.2f m\\n\", range);\n"
    "    return 0;\n"
    "}\n";

std::string demoDirectory() {
    std::string home = path::homeDir();
    if (home.empty()) return std::string();

    std::string dir = path::join(home, "cc1-demo");
    std::string file = path::join(dir, "src/first.c");

    // Made once. If it is already there it is yours now, and whatever you have
    // done to it is what opens.
    if (path::exists(file)) return dir;

    if (!path::makeDirectories(path::join(dir, "src"))) return std::string();

    FILE* out = std::fopen(file.c_str(), "wb");
    if (!out) return std::string();
    std::fwrite(kDemoProgram, 1, std::strlen(kDemoProgram), out);
    std::fclose(out);
    return dir;
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
