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

// Debug or release. What each compiler can actually do about it differs, and
// the editor says which rather than pretending they are the same:
//
//   cl   /Od /D_DEBUG    or  /O2 /DNDEBUG - a real difference in the code
//   cc1  -g -D_DEBUG=1   or  -DNDEBUG=1   - the define, and on the two targets
//        that can carry it, real debug information. cc1 still has no -O.
//
// The define is not nothing: it is what assert and every #ifdef NDEBUG in the
// source are looking for.
enum Configuration {
    ConfigDebug = 0,
    ConfigRelease,
    ConfigCount
};

const char* configName(Configuration config);

// The flags this compiler is given for this configuration, already spaced. The
// target is asked for because a debug build's -g depends on it.
std::string configFlags(ToolchainKind kind, Configuration config,
                        const std::string& arch);

// Whether the configuration changes the code, or only what is defined while
// compiling it.
bool optimises(ToolchainKind kind);

// Whether this compiler writes debug information for this target, and so
// whether a debug build asks for it.
//
// cc1 writes DWARF for x86_64-linux and arm64-darwin - line tables, types,
// objects and lexical blocks - and gdb and lldb both read it. The Windows
// target is where cc1 stops: it generates MASM there, MASM carries no line
// table, and the assembler cannot spell the relocations CodeView would need.
// cc1 does take -g for that target in the GNU spelling, which routes the DWARF
// out of the Linux emitter, but this editor asks for the assembly the target's
// own assembler reads.
//
// cl is a different matter and always could: /Zi writes CodeView into a .pdb,
// which is what a Windows debugger reads. So on this machine C and C++ are not
// in the same position - the C file goes to cc1 and carries no line table,
// while the C++ file goes to cl and carries everything.
bool emitsDebugInfo(ToolchainKind kind, const std::string& arch);

// What the Debug panel says above its listing: what this build has by way of
// debug information, and what the listing is instead. Both front ends call it,
// rather than each writing the words out - which is how the window came to be
// still saying there was none.
std::vector<std::string> debugNote(ToolchainKind kind, const std::string& arch);

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

// The architecture this machine is, named the way the target menu names it.
// cc1 carries only this one past -S, since the assembler and linker it hands
// off to are this machine's own.
const char* hostArch();

// Whether a build for this target can be run here, which is a different
// question from whether it can be compiled. Every target compiles to assembly
// anywhere; only the host's own reaches a program.
bool runsHere(ToolchainKind kind, const std::string& arch);

// Why it cannot, in words that say what to do about it.
std::string whyNotRun(ToolchainKind kind, const std::string& arch);

struct Recipe {
    std::string command;
    std::string assemblyPath;
    std::vector<std::string> leftovers;
};

Recipe assemblyRecipe(const Toolchain& tool, ToolchainKind kind,
                      const std::string& source, Language lang,
                      const std::string& arch, Configuration config);

std::string shownCommand(const Toolchain& tool, ToolchainKind kind,
                         const std::string& source, Language lang,
                         const std::string& arch, Configuration config);

// The command that produces a program rather than assembly, and where the
// program lands. cc1 with neither -S nor -c compiles, assembles and links;
// cl does the same when it is not given /c. Only worth asking for when
// runsHere says so - a cross target would stop at the assembly and there
// would be nothing to run.
//
// Recipe::assemblyPath holds the program here, since it is the thing the
// recipe produced and the thing the caller has to remove afterwards.
Recipe programRecipe(const Toolchain& tool, ToolchainKind kind,
                     const std::string& source, Language lang,
                     const std::string& arch, Configuration config);

std::string shownProgramCommand(const Toolchain& tool, ToolchainKind kind,
                                const std::string& source, Language lang,
                                const std::string& arch, Configuration config);

// Puts this process into the environment a Developer Command Prompt would have,
// once, so that cl can be found when the editor was started from an ordinary
// console. Does nothing anywhere but Windows, and nothing when already inside
// one. Returns false if Visual Studio could not be found at all.
bool prepareFor(ToolchainKind kind);

}  // namespace editor

#endif
