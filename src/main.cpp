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
    std::string compiler;
    editor::IndentStyle style;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--cc1") == 0 && i + 1 < argc) {
            cc1 = argv[++i];
        } else if (std::strcmp(argv[i], "--toolchain") == 0 && i + 1 < argc) {
            toolchain = argv[++i];
        } else if (std::strcmp(argv[i], "--compiler") == 0 && i + 1 < argc) {
            compiler = argv[++i];
        } else if (std::strcmp(argv[i], "--project") == 0 && i + 1 < argc) {
            project = argv[++i];
        } else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            long w = std::atol(argv[++i]);
            if (w >= 1 && w <= 16) style.width = static_cast<size_t>(w);
        } else if (std::strcmp(argv[i], "--tabs") == 0) {
            style.tabs = true;
        } else if (std::strcmp(argv[i], "--case-indent") == 0) {
            style.caseIndent = 1;
        } else if (std::strcmp(argv[i], "-h") == 0 ||
                   std::strcmp(argv[i], "--help") == 0) {
            std::printf(
                "usage: ed1 [file.c] [--project dir] [--toolchain cc1|msvc]\n"
                "           [--compiler path] [--cc1 path]\n"
                "           [--width n] [--tabs] [--case-indent]\n"
                "  an editor for the cc1 compiler\n"
                "\n"
                "  --toolchain    cc1 (the default) or msvc; msvc runs cl, and\n"
                "                 needs a Developer Command Prompt for it to be found\n"
                "  --compiler     the program to run, whichever toolchain is chosen\n"
                "  --cc1          the same thing, named for the usual case; $CC1\n"
                "                 does it too, and without any of them the compiler\n"
                "                 is looked for on PATH\n"
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

    editor::Editor ed;
    ed.setStyle(style);

    if (toolchain == "msvc" || toolchain == "cl") {
        ed.setToolchain(editor::ToolMsvc);
    } else if (!toolchain.empty() && toolchain != "cc1") {
        std::fprintf(stderr, "ed1: unknown toolchain %s\n", toolchain.c_str());
        return 2;
    }

    // --compiler is applied after the toolchain, so it overrides the default
    // program that choosing a toolchain sets.
    if (!compiler.empty()) ed.setCompiler(compiler);
    if (!cc1.empty()) ed.setCc1(cc1);

    // The project pane opens on the file's own directory unless told otherwise,
    // which is nearly always the directory someone wants to see.
    if (project.empty()) {
        size_t at = file.find_last_of("/\\");
        project = (at == std::string::npos) ? std::string(".") : file.substr(0, at);
    }
    ed.openProject(project);

    if (!file.empty()) ed.open(file);
    ed.run();
    return 0;
}
