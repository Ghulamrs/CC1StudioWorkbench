#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "editor.h"
#include "settings.h"
#include "symbols.h"
#include "workspace.h"

// What this copy of the editor is called where it is installed. There is one
// terminal editor and three names for what is built from it - ed1 on Linux and
// macOS, winconsole on Windows, and whatever anyone renames it to - so the name
// is taken from the command rather than written down in the messages.
static std::string calledIt(const char* argv0) {
    std::string name = (argv0 == 0 || *argv0 == 0) ? "ed1" : argv0;
    size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".exe") == 0)
        name.resize(name.size() - 4);
    return name;
}

int main(int argc, char** argv) {
    const std::string me = calledIt(argc > 0 ? argv[0] : 0);
    std::string file;
    std::string cc1;
    std::string project;
    std::string toolchain;
    std::string config;
    std::string cl;
    std::string shc;
    long width = 0;
    int plain = 0;
    int tabs = -1;
    int caseIndent = -1;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--cc1") == 0 && i + 1 < argc) {
            cc1 = argv[++i];
        } else if (std::strcmp(argv[i], "--toolchain") == 0 && i + 1 < argc) {
            toolchain = argv[++i];
        } else if (std::strcmp(argv[i], "--cl") == 0 && i + 1 < argc) {
            cl = argv[++i];
        } else if (std::strcmp(argv[i], "--shc") == 0 && i + 1 < argc) {
            shc = argv[++i];
        } else if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config = argv[++i];
        } else if (std::strcmp(argv[i], "--project") == 0 && i + 1 < argc) {
            project = argv[++i];
        } else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            long w = std::atol(argv[++i]);
            if (w >= 1 && w <= 16) width = w;
        } else if (std::strcmp(argv[i], "--plain") == 0) {
            plain = 1;
        } else if (std::strcmp(argv[i], "--tabs") == 0) {
            tabs = 1;
        } else if (std::strcmp(argv[i], "--case-indent") == 0) {
            caseIndent = 1;
        } else if (std::strcmp(argv[i], "-h") == 0 ||
                   std::strcmp(argv[i], "--help") == 0) {
            std::printf(
                "usage: %s [file] [--project dir] [--toolchain auto|cc1|msvc|shc]\n"
                "           [--config debug|release] [--cc1 path] [--cl path]\n"
                "           [--shc path] [--width n] [--tabs] [--case-indent] [--plain]\n"
                "  RStudio - the console half, which is ed1 on Linux\n"
                "  and macOS and winconsole on Windows. ed1gui is the same editor\n"
                "  in a window, over the same core.\n"
                "\n"
                "  --toolchain    auto (the default) lets the file choose: C goes\n"
                "                 to cc1, C++ to cl, Shalimar to shc. Naming one\n"
                "                 uses it for everything, and it says so where it\n"
                "                 cannot take the file\n"
                "  --config       debug (the default) or release. For cl that is\n"
                "                 /Od /Zi /D_DEBUG or /O2 /DNDEBUG; for cc1, -g and\n"
                "                 the define on the targets that carry a line table,\n"
                "                 and the define alone on the one that does not\n"
                "  --cc1, --cl,   the programs to run; $CC1 names the first, and\n"
                "  --shc          $SHC the third, and\n"
                "                 without either a cc1 beside this editor is used,\n"
                "                 and failing that PATH is asked. cl is\n"
                "                 also found through Visual Studio 2022 itself, so\n"
                "                 no Developer Command Prompt is needed\n"
                "  --project      what the pane on the left shows; the file's own\n"
                "                 directory by default\n"
                "  --width n      columns per indent step (4)\n"
                "  --tabs         indent with tabs instead of spaces\n"
                "  --plain        frame the screen with - | + instead of the box\n"
                "                 characters, for a console that draws those from\n"
                "                 a second font and breaks the lines at every join\n"
                "  --case-indent  put case labels one step inside their switch\n"
                "                 rather than in its own column\n"
                "\n"
                "  F10 menu   Ctrl-B build this file   F5 run this file\n"
                "  F4 build the project's program   Ctrl-A lay out\n"
                "  F9 breakpoint   F8 debug   F7/F6 step over/into\n"
                "  F1 keys    Ctrl-Q quit\n",
                me.c_str());
            return 0;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            std::fprintf(stderr, "%s: unknown option %s\n", me.c_str(), argv[i]);
            return 2;
        } else {
            file = argv[i];
        }
    }

    if (!toolchain.empty() && toolchain != "auto" && toolchain != "cc1" &&
        toolchain != "msvc" && toolchain != "cl" && toolchain != "shc") {
        std::fprintf(stderr, "%s: unknown toolchain %s\n", me.c_str(), toolchain.c_str());
        return 2;
    }

    editor::installPlatformDemangler();

    editor::Editor ed;

    // The project pane opens on the file's own directory unless told
    // otherwise, which is nearly always the directory someone wants to see.
    if (project.empty() && !file.empty()) {
        size_t at = file.find_last_of("/\\");
        project = (at == std::string::npos) ? std::string(".") : file.substr(0, at);
    }

    // Nothing named at all: the project you were last in, and failing that a
    // small one made for the purpose. Opening on "." was what this did before,
    // and it meant a first run showed whatever directory you happened to be
    // standing in, which is rarely a project and never a welcome.
    if (project.empty()) {
        project = editor::settings::lastProject();
        if (project.empty()) project = editor::demoDirectory();
        if (project.empty()) project = ".";
    }

    // Read first, so that anything named on the command line below overrides
    // it. The project file is what this project always does; a flag is what
    // today needs.
    ed.openProject(project);

    if (toolchain == "msvc" || toolchain == "cl") ed.setToolchain(editor::ToolMsvc);
    else if (toolchain == "cc1") ed.setToolchain(editor::ToolCc1);
    else if (toolchain == "shc") ed.setToolchain(editor::ToolShc);
    else if (toolchain == "auto") ed.setToolchain(editor::ToolAuto);

    if (config == "release") ed.setConfig(editor::ConfigRelease);
    else if (config == "debug") ed.setConfig(editor::ConfigDebug);
    else if (!config.empty()) {
        std::fprintf(stderr, "%s: unknown configuration %s\n", me.c_str(), config.c_str());
        return 2;
    }

    if (width > 0) ed.setIndentWidth(static_cast<size_t>(width));
    // What was chosen last time, unless this run says otherwise.
    if (plain || editor::settings::plainFrame()) ed.setPlainFrame(true);
    if (tabs >= 0) ed.setTabs(true);
    if (caseIndent >= 0) ed.setCaseIndent(1);

    if (!cc1.empty()) ed.setCc1(cc1);
    if (!cl.empty()) ed.setCl(cl);
    if (!shc.empty()) ed.setShc(shc);

    if (!file.empty()) ed.open(file);
    else ed.openFirstFile();   // something in it, rather than an empty sheet
    ed.run();
    return 0;
}
