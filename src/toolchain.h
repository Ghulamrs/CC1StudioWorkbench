#ifndef EDITOR_TOOLCHAIN_H
#define EDITOR_TOOLCHAIN_H

#include <string>
#include <vector>

namespace editor {

// What compiles the file. cc1 is what this editor was written for, but nothing
// above this header knows that: a toolchain is a command to run, a file it
// leaves the assembly in, and a way of reading what it complains about. MSVC is
// the second one, and a third would be this much work again and no more.
enum ToolchainKind {
    ToolCc1 = 0,
    ToolMsvc,
    ToolCount
};

struct Toolchain {
    ToolchainKind kind;
    std::string program;   // what to run; the default depends on the kind

    Toolchain() : kind(ToolCc1), program("cc1") {}
};

const char* toolchainName(ToolchainKind kind);
const char* defaultProgram(ToolchainKind kind);

// Whether -arch means anything to it. cc1 generates for three architectures;
// cl generates for the one it was installed as, and offering a choice that
// does nothing would be a lie told by the status bar.
bool usesArch(ToolchainKind kind);

// What to run, where the assembly will be, and what to clear up afterwards.
struct Recipe {
    std::string command;
    std::string assemblyPath;
    std::vector<std::string> leftovers;
};

Recipe assemblyRecipe(const Toolchain& tool, const std::string& source,
                      const std::string& arch);

// The command without the redirection, for showing in the console.
std::string shownCommand(const Toolchain& tool, const std::string& source,
                         const std::string& arch);

}  // namespace editor

#endif
