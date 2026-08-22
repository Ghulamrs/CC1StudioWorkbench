#ifndef EDITOR_COMPILE_H
#define EDITOR_COMPILE_H

#include <cstddef>
#include <string>
#include <vector>

#include "project.h"
#include "toolchain.h"

namespace editor {

// The error a compiler stopped at. cc1 reports one per run - Source::fail is
// [[noreturn]] and exits - so this is a single diagnostic and not a list. cl
// carries on and reports several; the first is the one worth standing on, and
// the rest are in the console.
struct Diagnostic {
    bool present = false;
    std::string file;
    size_t line = 0;   // as the compiler counts them, from 1
    size_t col = 0;    // likewise; 1 when the compiler gave no column
    std::string message;
};

struct Build {
    bool ok = false;
    Diagnostic diag;
    std::string output;
    std::vector<std::string> asmLines;
};

// The architectures cc1 will generate for. The host's own is the default; the
// other two reach -S and no further, since the assembler here is this
// machine's - which is exactly what the pane wants to show.
extern const char* const kArches[3];

// Called with each line the compiler writes, as it writes it. What the console
// is for: a build that says nothing until it is over looks like one that hung.
typedef void (*LineSink)(void* context, const std::string& line);

Build build(const Toolchain& tool, ToolchainKind kind, const std::string& sourcePath,
            Language lang, const std::string& arch, Configuration config,
            LineSink sink = 0, void* context = 0);

// What came of building a program and running it. Three things can happen and
// they are not the same thing: the compiler can refuse, the program can fail to
// be produced, or the program can run and return something. A program that
// returns 1 is not a build that failed, and the editor must not say it was.
struct Ran {
    bool built = false;    // a program came out of the compiler
    bool ran = false;      // and it was started
    int status = 0;        // what it returned, once it had
    Diagnostic diag;       // why it did not build, when it did not
    std::string output;    // everything the compiler said, then everything it said
};

// A program built and left where it is, for something else to run. Running it
// is one use and a debugger attaching to it is the other, and the second needs
// the file to still be there afterwards - which is the whole difference between
// this and runProgram.
struct Built {
    bool ok;
    Diagnostic diag;
    std::string output;
    std::string program;     // where it is, when it was built
    std::vector<std::string> leftovers;   // to remove with it

    Built() : ok(false) {}
};

Built buildProgram(const Toolchain& tool, ToolchainKind kind, const std::string& sourcePath,
                   Language lang, const std::string& arch, Configuration config,
                   LineSink sink = 0, void* context = 0);

// A program from several sources, put where the caller says. The project's
// build; `buildProgram` above is still what a single file gets, and the two do
// not know about each other - compiling the file in front of you never depends
// on a project being open, or on it being shut.
Built buildTarget(const Toolchain& tool, ToolchainKind kind,
                  const std::vector<std::string>& sources, Language lang,
                  const std::string& arch, Configuration config,
                  const std::string& program, LineSink sink = 0, void* context = 0);

// A program from several groups, each with its own compiler. What Project's
// targetParts hands back, built.
//
// One part is still one command - buildTarget, unchanged, which is the only
// thing shc can do anyway and is what every project that ever worked already
// did. More than one part is a compile per part and then a link, and the link
// is the editor's own command because no compiler here takes an object as an
// input.
//
// The objects go in a directory of the editor's making and are removed with it,
// whether the link worked or not. What survives is the program.
Built buildParts(const Toolchain& tool, const std::vector<Part>& parts,
                 const std::string& arch, Configuration config,
                 const std::string& program, LineSink sink = 0, void* context = 0);

// Runs a program that has already been built, with every line it writes handed
// to the sink. The project's Run, where the building was its own step and the
// program is not a temporary file to be cleared away afterwards.
Ran runBuilt(const std::string& program, LineSink sink = 0, void* context = 0);

// Removes what buildProgram left.
void removeProgram(const Built& built);

// Compile, assemble, link and run, with every line handed to the sink as it
// arrives - the compiler's first and then the program's own. Only for a target
// runsHere accepts; anything else stops at the assembly and there is nothing to
// start. The program is built where the assembly is built and removed after.
//
// It is run with its output joined to its errors and its input empty. A program
// that waits for input from a keyboard will not get one.
Ran runProgram(const Toolchain& tool, ToolchainKind kind, const std::string& sourcePath,
               Language lang, const std::string& arch, Configuration config,
               LineSink sink = 0, void* context = 0);

// Reads whichever of the two spellings a compiler used:
//
//   file:line:col: error: message      cc1, gcc and clang
//   file(line,col): error C2059: msg   cl, and ml64
//
// Both are recognised without being told which to expect, so pointing the
// editor at a third compiler that speaks either one needs no new code.
// `source` is the file being compiled, and is used only by the one compiler
// that does not name it: shc writes 'Error: line 3: ...' and nothing more,
// because Shalimar has no include and so only ever has one file to mean.
Diagnostic parseDiagnostic(const std::string& text, const std::string& source = std::string());

}  // namespace editor

#endif
