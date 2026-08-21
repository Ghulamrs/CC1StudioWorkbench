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

// One place on the call stack: the function standing there, and the line of it
// that is being waited on. The fields are three of a Stop's, because a frame is
// a place the program is standing in - the only difference is that it got there
// by calling rather than by arriving, and is waiting for the call to come back.
struct StackFrame {
    std::string function;
    std::string file;
    size_t line;

    StackFrame() : line(0) {}
};

// An expression the editor keeps asking about. The list belongs to the
// debugger rather than to either front end, because the rule that matters is
// "read them all again whenever the program has moved" - and a rule written
// out in one front end and not the other is how they came to disagree about a
// stop with no source.
struct Watch {
    std::string expression;
    std::string value;
    bool ok;          // false when the debugger would not answer, and `value` is why

    Watch() : ok(false) {}
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

    // How it got here: where it is standing first, and what called it after
    // that. One frame means it is in main and nothing called it, which is the
    // ordinary case and is not worth saying.
    std::vector<StackFrame> frames();

    // Which of those frames the questions after this are about. All three keep
    // a current frame and answer `locals` from it, so this is what makes the
    // variables of a caller readable at all.
    //
    // A move resets it: every stop starts at frame 0, which is where the
    // program is standing. Nothing here has to put it back.
    bool selectFrame(size_t which);

    // Writing a variable back. Every one of them will do it, each in its own
    // words, and each into the frame it is currently in - so a caller's
    // variable is set the same way as one of the stopped frame's, selectFrame
    // having already said which.
    //
    // False when it would not: a name that is not in scope, a value it cannot
    // parse, something that is not an lvalue. The line it complained on goes
    // into `said`, because the debugger's own words are a better message than
    // any this could invent - "No symbol \"x\" in current context" names the
    // mistake, and one line is what a message line holds.
    bool setVariable(const std::string& name, const std::string& value,
                     std::string* said = 0);

    // What an expression comes to, in the frame being looked at. False into
    // `ok` when the debugger would not answer, and the answer is then its own
    // complaint - the same bargain setVariable makes.
    std::string evaluate(const std::string& expression, bool* ok = 0);

    // Expressions to keep asking about. They are read again after every move
    // and after every change of frame, which is the whole point of them: a
    // watch that had to be asked for again by hand would be `evaluate`.
    //
    // They outlive a debugging session, as a breakpoint does - starting the
    // program again finds them still here.
    void addWatch(const std::string& expression);
    void setWatch(size_t which, const std::string& expression);
    void removeWatch(size_t which);
    const std::vector<Watch>& watches() const { return watches_; }
    void readWatches();

    // Anything else, for the console: what was typed, answered as it came.
    std::string ask(const std::string& command);

private:
    Debugger(const Debugger&);
    Debugger& operator=(const Debugger&);

    Stop afterMoving(const std::string& command);

    Process child_;
    DebuggerKind kind_;
    bool onConsole_;    // it was given a console, so it echoes and writes escapes
    std::string executable_;

    // Where it was standing before this move, so that a debugger which reports
    // only what changed can be asked where it is.
    Stop last_;

    std::vector<Watch> watches_;
};

// Reading what each of them says. Free functions, and tested as such: a stop
// line is a fiddly thing to parse and it should not need a live debugger and a
// built program to check that it is parsed right.
Stop dbg_readStop(DebuggerKind kind, const std::string& said);
// Whether a stop that named no place is a program standing somewhere with no
// source rather than a debugger that has died.
//
// Stepping off the end of main arrives in the code that started the program,
// which was not compiled here: lldb says "stop reason" and "frame #0", gdb
// "#0  0x". That is a real place to be standing and F8 carries on from it, so
// it must not be reported as a failure and the debugger must not be stopped.
//
// It lives here because both front ends need it and only one of them had it.
// The terminal asked these three questions inline and the window asked none,
// so the same step ended the session in one and not the other.
bool dbg_stoppedWithNoSource(const std::string& said);

// What a console adds to what a debugger says, and how it is taken back out.
//
// On Windows cdb is given a pseudo-console rather than a pipe, so that the
// program it debugs is not full-buffered by its runtime - see
// Process::startOnConsole. A console is a terminal: it writes escape
// sequences, and it echoes what is typed at it. Neither is the debugger's
// words, and both would otherwise be read as though they were.
std::string dbg_withoutEscapes(const std::string& text);
std::string dbg_withoutEcho(const std::string& said, const std::string& asked,
                            const std::string& marker);

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

// The call stack, as each of them prints it.
//
// It stops at main. Below main is the code that started the program, which was
// not compiled here and has no source to go to - the same place a step off the
// end of main arrives in, see dbg_stoppedWithNoSource. lldb shows one frame of
// it and gdb three, and three lines of somebody else's libc above the answer
// are three lines to be read past at every stop.
//
// A frame that names no source is dropped for the same reason, and because
// there is nothing the editor could do if it were clicked.
std::vector<StackFrame> dbg_readFrames(DebuggerKind kind, const std::string& said);

// How a frame is written in the Debug tab - "  main   stepped.c:11" - and
// which frame a line of that tab is about.
//
// The pair is here, in one place, because the two front ends compose that tab
// separately: the terminal fills a vector of lines and the window a string for
// a RichTextBox. A rule that counted rows would be a rule each of them could
// count differently, which is how they came to disagree about a stop with no
// source. Written by one function and read back by another that matches what
// was written, they cannot.
//
// dbg_frameOnLine answers stack.size() when the line is not a frame at all -
// the heading, a variable, the hint at the bottom. A recursive call writes the
// same line twice and the first of them is answered, which is right for going
// to it: the two name the same place in the same file.
// The frame being looked at wears a > where the others have a space, in the
// manner of the gutter's own marks. dbg_frameOnLine answers a line written
// either way, since it matches what dbg_frameLine writes rather than what a
// caller thinks it wrote.
std::string dbg_frameLine(const StackFrame& frame, bool looking = false);
size_t dbg_frameOnLine(const std::vector<StackFrame>& stack, const std::string& line);

// Whose variables are being shown, when they are not the ones belonging to the
// frame the program stopped in. The tab says this above them rather than
// leaving "stopped at stepped.c:3 in twice" standing over another function's
// locals, which is a sentence and a list that contradict each other.
// How a variable is written in the Debug tab - "  total = 0   [int]" - and
// which variable a line of that tab is about, for the same reason the frames
// have a pair like it: the two front ends compose that tab separately, and
// pressing enter on one of these lines is how either of them sets it.
//
// dbg_variableOnLine answers locals.size() when the line is not a variable.
std::string dbg_variableLine(const Variable& variable);
size_t dbg_variableOnLine(const std::vector<Variable>& locals, const std::string& line);

// And the same pair for a watch: "  total + i = 1" in the tab, and which watch
// a line of it is about. A watch that could not be answered shows what the
// debugger said instead of a value, in brackets, so that the tab never has an
// expression with nothing after it.
std::string dbg_watchLine(const Watch& watch);
size_t dbg_watchOnLine(const std::vector<Watch>& watches, const std::string& line);

// The value out of an answer, in each of their spellings:
//
//   lldb:  (int) $0 = 12
//   gdb:   $1 = 12
//   cdb:   int 0n12
//
// Empty when there is no value in it, which is a debugger that complained -
// and the complaint is what the caller shows instead.
std::string dbg_readValue(DebuggerKind kind, const std::string& said);

std::string dbg_lookingAt(const StackFrame& frame);

// The first line of the tab, which names the frame the program stopped in -
// frame 0, the one the stack is counted from. It is written here for the
// reason the frame lines are: both front ends compose that tab separately, and
// pressing enter on this line is how either of them goes back to the stop
// after looking at a caller. Written by one function and compared against it,
// they cannot disagree about which line that is.
std::string dbg_stopLine(const std::string& file, size_t line, const std::string& function);

}  // namespace editor

#endif
