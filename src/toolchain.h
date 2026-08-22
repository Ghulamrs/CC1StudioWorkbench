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
    ToolShc,        // Shalimar, and the same three architectures
    ToolCount
};

// Debug or release. What each compiler can actually do about it differs, and
// the editor says which rather than pretending they are the same:
//
//   cl   /Od /D_DEBUG    or  /O2 /DNDEBUG - a real difference in the code
//   cc1  -g -D_DEBUG=1   or  -DNDEBUG=1   - the define, and on the two targets
//        that can carry it, real debug information. cc1 still has no -O.
//   shc  nothing either way. Shalimar has no preprocessor, so there is no
//        define to set, and no debug information by decision rather than by
//        omission - the language is for numeric programs written on a phone,
//        and a symbolic debugger is not something they reach for.
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
    std::string shc;   // and for Shalimar

    Toolchain() : kind(ToolAuto), cc1("cc1"), cl("cl"), shc("shc") {}
};

// Which one actually runs, once the file's language is known. This is the whole
// of the routing rule: each language goes to the compiler that can take it -
// C to cc1, which this editor was written for, C++ to cl, and Shalimar to shc.
// No two of them overlap, so 'by language' is not a preference here; it is the
// answer, and naming a compiler by hand is how to override it.
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

// The same, for a program made of several sources and put where the caller
// says rather than in a temporary place. cc1 takes them all at once - "several
// inputs link together" is its own usage - and cl does too, given a directory
// to leave the objects in; the objects are named after the sources, so they
// can be listed as leftovers without going and looking.
Recipe targetRecipe(const Toolchain& tool, ToolchainKind kind,
                    const std::vector<std::string>& sources, Language lang,
                    const std::string& arch, Configuration config,
                    const std::string& program);

// ---- a compiler per group, and one link at the end -------------------------
//
// The two above make a program in one command, which is all a target ever
// needed while a target was one language. A target holding C and C++ cannot be
// made that way: cc1 takes the C, cl takes the C++, and neither will take a
// program that is half the other's. So the step is split - each group compiles
// to objects with its own compiler, and the objects meet at the linker, which
// does not care which compiler wrote them.
//
// The linker is named here rather than reached through a compiler, because no
// compiler in this editor takes an object as an input: handing cc1 a .o gets
// "stray '\377' in program", the object read as C.

// One group's sources to object files, and where they land. `objectDir` is the
// caller's to make and to clear away; the objects are named after the sources,
// so `objects` comes back filled without going and looking.
Recipe objectRecipe(const Toolchain& tool, ToolchainKind kind,
                    const std::vector<std::string>& sources, Language lang,
                    const std::string& arch, Configuration config,
                    const std::string& objectDir, std::vector<std::string>& objects);

// The objects into a program. `withCpp` says whether any of them came from a
// C++ compiler, which is what decides the runtime the link needs; nothing else
// about where they came from matters by this point.
//
// On Windows this is link.exe with the C runtime named, the same list cc1's own
// driver names and for the same reason - cc1's objects carry no /DEFAULTLIB
// directive, so nothing else says which CRT to use. The static one is chosen
// because that is what cc1 has always linked; cl's objects are compiled /MT or
// /MTd to match, which is a real difference from cl on its own and is why
// objectRecipe asks for the configuration.
//
// Everywhere else it is 'cc', the same host driver cc1 hands its own linking
// to, with -lm always and -g under debug - without -g a Mac's debug map points
// at object files that are deleted the moment the link finishes.
Recipe linkRecipe(const Toolchain& tool, const std::vector<std::string>& objects,
                  bool withCpp, const std::string& arch, Configuration config,
                  const std::string& program);

// What that link will actually run, for the console. The linker is not one of
// the compilers the editor knows by name, so this is the only place its name is
// visible to whoever is watching a build.
std::string linkerName(bool withCpp);

// Puts this process into the environment a Developer Command Prompt would have,
// once, so that the compiler can be run when the editor was started from an
// ordinary console. Does nothing anywhere but Windows, and nothing when already
// inside one.
//
// It is needed for cc1 on Windows as much as for cl, which is not obvious: cc1
// assembles and links what it compiles by calling ml64 and link by their bare
// names, and those two ship with Visual Studio and reach PATH only after
// vcvars64.bat has run. Without this, cc1 compiles and then says "'ml64.exe' is
// not recognized" - which reads as a broken cc1 rather than a missing
// environment. It went unnoticed for so long because every suite is run from a
// prompt that already had it.
//
// Returns false only when cl is wanted and Visual Studio cannot be found at
// all; cc1 without it can still reach -S, so it is not stopped here.
bool prepareFor(ToolchainKind kind);

}  // namespace editor

#endif
