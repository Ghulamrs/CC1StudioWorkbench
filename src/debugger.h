#ifndef EDITOR_DEBUGGER_H
#define EDITOR_DEBUGGER_H

#include <cstddef>
#include <string>
#include <vector>

#include "process.h"

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
enum DebuggerKind {
    DebuggerNone = 0,
    DebuggerLldb,      // what a Mac has
    DebuggerGdb        // what the Linux box has
};

// The one this machine has, and the program to run for it. Windows gets none:
// cc1 generates MASM there, which carries no line table, so there would be
// nothing for a debugger to read even if one were installed.
DebuggerKind debuggerHere();
const char* debuggerName(DebuggerKind kind);
const char* debuggerProgram(DebuggerKind kind);

// Why there is no debugging here, when there is not.
std::string noDebuggerBecause(const std::string& arch);

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

    // Starts the debugger on a program that has already been built with -g.
    // `program` is the debugger to run, empty for this machine's usual one.
    bool start(const std::string& executable, const std::string& program = std::string());
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
};

// Reading what each of them says. Free functions, and tested as such: a stop
// line is a fiddly thing to parse and it should not need a live debugger and a
// built program to check that it is parsed right.
Stop readStop(DebuggerKind kind, const std::string& said);
std::vector<Variable> readVariables(DebuggerKind kind, const std::string& said);

}  // namespace editor

#endif
