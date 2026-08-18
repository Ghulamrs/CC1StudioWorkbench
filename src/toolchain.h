#ifndef EDITOR_TOOLCHAIN_H
#define EDITOR_TOOLCHAIN_H

#include <string>
#include <vector>

#include "syntax.h"

namespace editor {

// What compiles the file. Nothing above this header knows which compiler is
// running: a toolchain is a command, a file the assembly lands in, and a way of
// reading complaints. Two are built in, and each is handed the language it is
// good for rather than being left to guess from a suffix.
enum ToolchainKind {
    ToolAuto = 0,   // the file's language chooses
    ToolCc1,        // C, and the three architectures cc1 generates for
    ToolMsvc,       // C and C++, on the host cl was installed for
    ToolCount
};

struct Toolchain {
    ToolchainKind kind;
    std::string cc1;   // the program to run for the cc1 toolchain
    std::string cl;    // the program to run for the MSVC one

    Toolchain() : kind(ToolAuto), cc1("cc1"), cl("cl") {}
};

// Which one actually runs, once the file's language is known. This is the whole
// of the routing rule: cc1 is a C compiler, so C++ goes to the one that can
// take it, and C goes to the compiler this editor was written for.
ToolchainKind resolve(const Toolchain& tool, Language lang);

const char* toolchainName(ToolchainKind kind);
const char* programOf(const Toolchain& tool, ToolchainKind kind);

// Whether -arch means anything to it. cc1 generates for three architectures;
// cl generates for the one it was installed as, and offering a choice that does
// nothing would be the status bar telling a lie.
bool usesArch(ToolchainKind kind);

// Whether it can take the language at all, and why not when it cannot.
bool canCompile(ToolchainKind kind, Language lang);
std::string refusal(ToolchainKind kind, Language lang);

struct Recipe {
    std::string command;
    std::string assemblyPath;
    std::vector<std::string> leftovers;
};

Recipe assemblyRecipe(const Toolchain& tool, ToolchainKind kind,
                      const std::string& source, Language lang,
                      const std::string& arch);

std::string shownCommand(const Toolchain& tool, ToolchainKind kind,
                         const std::string& source, Language lang,
                         const std::string& arch);

// Puts this process into the environment a Developer Command Prompt would have,
// once, so that cl can be found when the editor was started from an ordinary
// console. Does nothing anywhere but Windows, and nothing when already inside
// one. Returns false if Visual Studio could not be found at all.
bool prepareFor(ToolchainKind kind);

}  // namespace editor

#endif
