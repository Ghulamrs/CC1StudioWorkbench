#ifndef EDITOR_DEBUGGER_H
#define EDITOR_DEBUGGER_H

#include <cstddef>
#include <string>
#include <vector>

#include "process.h"
#include "toolchain.h"

namespace editor {

// A debugger, driven rather than written.
//
// cc1 emits DWARF for x86_64-linux and arm64-darwin, and the debuggers on those
// machines read it - so what stands between this editor and a breakpoint is not
// a compiler but a conversation. This holds that conversation: it starts gdb or
// lldb on a program the editor built, tells it where to stop, and reads back
// where it is and what the variables are.
//
// The two speak the same ideas in different words. That difference lives here,
// in one place, in a table of spellings and two readers for what they say back
// - the same shape as the two assembly spellings in symbols.cpp and the two
// filesystems in path.cpp.
// The free functions here carry a dbg_ prefix so that a debugger-support
// function is recognisable wherever it is read, away from this header. The
// Debugger class's own methods do not: the type already says it, and
// Debugger::dbg_start() would say it twice.
enum DebuggerKind {
    DebuggerNone = 0,
    DebuggerLldb,      // what a Mac has
    DebuggerGdb,       // what the Linux box has
    DebuggerCdb        // what Windows has, for what cl built
};

// The DWARF debugger this machine has: lldb on a Mac, gdb on Linux, and none
// on Windows, where neither is installed.
DebuggerKind dbg_here();
const char* dbg_name(DebuggerKind kind);
const char* dbg_program(DebuggerKind kind);

// Which debugger can read what this compiler writes for this target - which is
// not a question about the machine alone. On Windows the two compilers are in
// different positions: a C file goes to cc1 and comes out as MASM with no line
// table, while a C++ file goes to cl and comes out with CodeView in a .pdb.
// The first can never be debugged there; the second could be, by something
// that reads a .pdb.
DebuggerKind dbg_for(ToolchainKind kind, const std::string& arch);

// Why there is none, when there is none - in the terms that apply to this
// compiler and this target rather than in general.
std::string dbg_whyNot(ToolchainKind kind, const std::string& arch);

// Where the program is, now that it has stopped. `stopped` and `exited` are
// both false when the answer could not be read at all, which is a debugger
// that died rather than a program that did.
struct Stop {
    bool stopped;          // alive, and standing still
    bool exited;           // ran to the end
    int status;            // what it returned, when it did
    std::string file;
    size_t line;
    std::string function;
    std::string said;      // what the debugger printed, for the console

    Stop() : stopped(false), exited(false), status(0), line(0) {}
};

struct Variable {
    std::string name;
    std::string type;
    std::string value;
};

class Debugger {
public:
    Debugger();
    ~Debugger();

    // Starts a debugger on a program already built with debug information.
    // Which debugger is the caller's to decide, because it follows from the
    // compiler rather than from the machine - see dbg_for. `program` names
    // the debugger to run, and is empty for the usual one.
    bool start(DebuggerKind kind, const std::string& executable,
               const std::string& program = std::string());
    bool running() const { return kind_ != DebuggerNone && child_.running(); }
    DebuggerKind kind() const { return kind_; }
    void stop();

    // Before running, or while stopped. A line with no code on it is moved to
    // the next line that has some, by the debugger rather than by this.
    bool breakAt(const std::string& file, size_t line);
    bool clearBreakpoints();

    Stop run();
    Stop resume();
    Stop stepOver();     // over a call
    Stop stepInto();     // into one
    Stop stepOut();      // out of the one we are in

    std::vector<Variable> locals();

    // Anything else, for the console: what was typed, answered as it came.
    std::string ask(const std::string& command);

private:
    Debugger(const Debugger&);
    Debugger& operator=(const Debugger&);

    Stop afterMoving(const std::string& command);

    Process child_;
    DebuggerKind kind_;
    std::string executable_;

    // Where it was standing before this move, so that a debugger which reports
    // only what changed can be asked where it is.
    Stop last_;
};

// Reading what each of them says. Free functions, and tested as such: a stop
// line is a fiddly thing to parse and it should not need a live debugger and a
// built program to check that it is parsed right.
Stop dbg_readStop(DebuggerKind kind, const std::string& said);
// What the program itself printed, taken out of a debugger's transcript.
//
// A debugged program writes down the same pipe the debugger talks on - which
// is why kMarker exists - so `said` holds the two mixed together. This takes
// the debugger's own words out and leaves the program's.
//
// It works by removing what is recognisably the debugger's rather than by
// looking for what might be the program's, because the program's output has no
// shape at all: lldb echoes the source lines around a stop, so a file that
// prints "hello" has a debugger line containing "hello" in every transcript
// where it stops nearby. Anything not recognised is kept - a stray debugger
// line in the console is a smaller fault than a line of the program's output
// that never arrives.
//
// The three differ in one way that matters here, and it is not the one it
// first looks like. lldb echoes every command it is given when its input is a
// pipe, so what follows an lldb prompt is that echo and the line is lldb's
// entire. gdb and cdb echo nothing, so after their prompt comes either a
// message of their own or the program's output - the prompt comes off and what
// is left is weighed on its own. A program's output lands there as soon as it
// is not buffered, which is exactly when this is worth having.
//
// Blank lines go. A transcript is mostly blank lines, and a program whose
// output has them loses them, which is the one thing here that is a real loss
// rather than a tidying.
std::string dbg_programOutput(DebuggerKind kind, const std::string& said);

std::vector<Variable> dbg_readVariables(DebuggerKind kind, const std::string& said);

}  // namespace editor

#endif
