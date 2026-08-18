#ifndef EDITOR_COMPILE_H
#define EDITOR_COMPILE_H

#include <cstddef>
#include <string>
#include <vector>

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
            Language lang, const std::string& arch, LineSink sink = 0,
            void* context = 0);

// Reads whichever of the two spellings a compiler used:
//
//   file:line:col: error: message      cc1, gcc and clang
//   file(line,col): error C2059: msg   cl, and ml64
//
// Both are recognised without being told which to expect, so pointing the
// editor at a third compiler that speaks either one needs no new code.
Diagnostic parseDiagnostic(const std::string& text);

}  // namespace editor

#endif
