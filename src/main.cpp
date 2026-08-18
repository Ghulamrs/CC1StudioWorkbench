#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "editor.h"

int main(int argc, char** argv) {
    std::string file;
    std::string cc1;
    std::string project;
    std::string toolchain;
    std::string config;
    std::string cl;
    long width = 0;
    int tabs = -1;
    int caseIndent = -1;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--cc1") == 0 && i + 1 < argc) {
            cc1 = argv[++i];
        } else if (std::strcmp(argv[i], "--toolchain") == 0 && i + 1 < argc) {
            toolchain = argv[++i];
        } else if (std::strcmp(argv[i], "--cl") == 0 && i + 1 < argc) {
            cl = argv[++i];
        } else if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config = argv[++i];
        } else if (std::strcmp(argv[i], "--project") == 0 && i + 1 < argc) {
            project = argv[++i];
        } else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            long w = std::atol(argv[++i]);
            if (w >= 1 && w <= 16) width = w;
        } else if (std::strcmp(argv[i], "--tabs") == 0) {
            tabs = 1;
        } else if (std::strcmp(argv[i], "--case-indent") == 0) {
            caseIndent = 1;
        } else if (std::strcmp(argv[i], "-h") == 0 ||
                   std::strcmp(argv[i], "--help") == 0) {
            std::printf(
                "usage: ed1 [file.c] [--project dir] [--toolchain auto|cc1|msvc]\n"
                "           [--config debug|release] [--cc1 path] [--cl path]\n"
                "           [--width n] [--tabs] [--case-indent]\n"
                "  an editor for the cc1 compiler\n"
                "\n"
                "  --toolchain    auto (the default) lets the file choose: C goes\n"
                "                 to cc1, C++ to cl, since cc1 compiles C. Naming\n"
                "                 cc1 or msvc uses that one for everything\n"
                "  --config       debug (the default) or release. For cl that is\n"
                "                 /Od /D_DEBUG or /O2 /DNDEBUG; for cc1 it is the\n"
                "                 define alone, since cc1 has no -O and no -g\n"
                "  --cc1, --cl    the programs to run; $CC1 names the first, and\n"
                "                 without either they are looked for on PATH. cl is\n"
                "                 also found through Visual Studio 2022 itself, so\n"
                "                 no Developer Command Prompt is needed\n"
                "  --project      what the pane on the left shows; the file's own\n"
                "                 directory by default\n"
                "  --width n      columns per indent step (4)\n"
                "  --tabs         indent with tabs instead of spaces\n"
                "  --case-indent  put case labels one step inside their switch\n"
                "                 rather than in its own column\n"
                "\n"
                "  F10 menu   Ctrl-B build   Ctrl-F lay out   F1 keys   Ctrl-Q quit\n");
            return 0;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            std::fprintf(stderr, "ed1: unknown option %s\n", argv[i]);
            return 2;
        } else {
            file = argv[i];
        }
    }

    if (!toolchain.empty() && toolchain != "auto" && toolchain != "cc1" &&
        toolchain != "msvc" && toolchain != "cl") {
        std::fprintf(stderr, "ed1: unknown toolchain %s\n", toolchain.c_str());
        return 2;
    }

    editor::Editor ed;

    // The project pane opens on the file's own directory unless told otherwise,
    // which is nearly always the directory someone wants to see.
    if (project.empty()) {
        size_t at = file.find_last_of("/\\");
        project = (at == std::string::npos) ? std::string(".") : file.substr(0, at);
    }

    // Read first, so that anything named on the command line below overrides
    // it. The project file is what this project always does; a flag is what
    // today needs.
    ed.openProject(project);

    if (toolchain == "msvc" || toolchain == "cl") ed.setToolchain(editor::ToolMsvc);
    else if (toolchain == "cc1") ed.setToolchain(editor::ToolCc1);
    else if (toolchain == "auto") ed.setToolchain(editor::ToolAuto);

    if (config == "release") ed.setConfig(editor::ConfigRelease);
    else if (config == "debug") ed.setConfig(editor::ConfigDebug);
    else if (!config.empty()) {
        std::fprintf(stderr, "ed1: unknown configuration %s\n", config.c_str());
        return 2;
    }

    if (width > 0) ed.setIndentWidth(static_cast<size_t>(width));
    if (tabs >= 0) ed.setTabs(true);
    if (caseIndent >= 0) ed.setCaseIndent(1);

    if (!cc1.empty()) ed.setCc1(cc1);
    if (!cl.empty()) ed.setCl(cl);

    if (!file.empty()) ed.open(file);
    ed.run();
    return 0;
}
