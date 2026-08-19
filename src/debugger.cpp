#include "debugger.h"

#include <cstdlib>

#include "path.h"

namespace editor {

namespace {

// What is said to know an answer is complete. A prompt is not enough - the
// program being debugged writes down the same pipe - so the editor asks for
// something back that nothing else would ever print.
const char* const kMarker = "<<ed1-done>>";

// And each of them wants it asked for differently, for opposite reasons.
//
// lldb echoes every command it is given when its input is a pipe. A command
// with the marker written in it therefore puts the marker on the stream before
// the answer rather than after, and every answer read that way is the one
// before the one asked for. Joining it only where it is printed - "<<ed1" plus
// "-done>>" - means the echo cannot be mistaken for the reply.
//
// gdb does not echo commands, so it needs no such trick; and it must not be
// given one, because it prints its prompt between them. Split across two
// commands the marker arrives as "<<ed1(gdb) -done>>" and is never seen whole,
// which is a debugger that appears never to start.
void sayMarker(Process& child, DebuggerKind kind) {
    if (kind == DebuggerGdb) {
        child.say("echo <<ed1-done>>\\n");
        return;
    }
    child.say("script print(\"<<ed1\" + \"-done>>\")");
}

// Said once, before anything else, to make the thing driveable.
std::vector<std::string> preamble(DebuggerKind kind) {
    std::vector<std::string> said;
    if (kind == DebuggerGdb) {
        said.push_back("set confirm off");
        // Without this gdb stops every screenful to ask, and the answer it is
        // waiting for never comes down a pipe.
        said.push_back("set pagination off");
        said.push_back("set breakpoint pending on");
    } else {
        // The one that matters. lldb launches asynchronously by default, and
        // over a pipe that leaves it forwarding what it is told to the program
        // instead of running it: every command after `run` is echoed back and
        // nothing happens. Synchronous, and `run` returns where it stopped.
        said.push_back("script lldb.debugger.SetAsync(False)");
        said.push_back("settings set auto-confirm true");
        // The program gets no input from here either, exactly as it gets none
        // when the editor runs it without a debugger.
        said.push_back("settings set target.input-path /dev/null");
    }
    return said;
}

std::string quoted(const std::string& text) { return "\"" + text + "\""; }

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::string line;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            if (!line.empty() && line[line.size() - 1] == '\r') line.resize(line.size() - 1);
            out.push_back(line);
            line.clear();
            continue;
        }
        line += text[i];
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

// Both of them print a prompt and then, on the same line, the first line of
// the answer: "(gdb) i = 1", "(gdb) twice (n=1) at s.c:3". Left on, the prompt
// is read as part of the name - which is why the first variable of every
// listing went missing and every function came out empty, while everything on
// a line of its own was read correctly.
std::string withoutPrompt(const std::string& line) {
    std::string out = line;
    for (;;) {
        size_t at = 0;
        if (out.compare(0, 6, "(gdb) ") == 0) at = 6;
        else if (out.compare(0, 7, "(lldb) ") == 0) at = 7;
        else break;
        out = out.substr(at);
    }
    return out;
}

std::string trimmed(const std::string& text) {
    size_t from = text.find_first_not_of(" \t");
    if (from == std::string::npos) return std::string();
    size_t to = text.find_last_not_of(" \t");
    return text.substr(from, to - from + 1);
}

bool digits(const std::string& text) {
    if (text.empty()) return false;
    for (size_t i = 0; i < text.size(); ++i)
        if (text[i] < '0' || text[i] > '9') return false;
    return true;
}

size_t number(const std::string& text) {
    return static_cast<size_t>(std::strtoul(text.c_str(), 0, 10));
}

}  // namespace

DebuggerKind debuggerHere() {
#if defined(_WIN32)
    return DebuggerNone;
#elif defined(__APPLE__)
    return DebuggerLldb;
#else
    return DebuggerGdb;
#endif
}

const char* debuggerName(DebuggerKind kind) {
    switch (kind) {
        case DebuggerLldb: return "lldb";
        case DebuggerGdb:  return "gdb";
        default:           return "none";
    }
}

const char* debuggerProgram(DebuggerKind kind) { return debuggerName(kind); }

std::string noDebuggerBecause(const std::string& arch) {
    if (debuggerHere() == DebuggerNone)
        return "no debugger here - cc1 generates MASM for " + arch + ", which has no line table";
    return std::string();
}

// ---- reading what they say -------------------------------------------------

// lldb:  frame #0: 0x0000000100000508 dbg`main at dbg.c:13:9
// gdb:   Breakpoint 1, main () at dbg.c:13
//        13          total = total + twice(i);
Stop readStop(DebuggerKind kind, const std::string& said) {
    Stop stop;
    stop.said = said;

    std::vector<std::string> all = lines(said);
    for (size_t i = 0; i < all.size(); ++i) {
        std::string line = withoutPrompt(all[i]);

        // Gone, and what it went with. Both spell this in their own way and
        // neither is a stop: there is nothing left to look at.
        if (line.find("exited with status") != std::string::npos ||
            line.find("exited with code") != std::string::npos ||
            line.find("exited normally") != std::string::npos) {
            stop.exited = true;
            stop.status = 0;

            // lldb: "exited with status = 3 (0x00000003)", plain decimal.
            // gdb:  "exited with code 014", which is octal - it has printed it
            // that way since long before anyone here was reading it, and a
            // program returning twelve would otherwise be reported as fourteen.
            size_t at = line.find("status = ");
            int base = 10;
            if (at != std::string::npos) {
                at += 9;
            } else {
                at = line.find("code ");
                if (at != std::string::npos) { at += 5; base = 8; }
            }
            if (at != std::string::npos)
                stop.status = static_cast<int>(std::strtol(line.c_str() + at, 0, base));
            continue;
        }

        // Where it is. Both put it after " at ", and the file is what follows
        // up to a colon - read from the right, since a Windows path holds one
        // that is not a separator.
        // gdb's `finish` announces the frame it is leaving before it says
        // where it arrived: "Run till exit from #0 twice (n=1) at s.c:3",
        // followed by the line in main. Read the first, and stepping out
        // reports the function stepped out of - which looks like a step that
        // did nothing.
        if (line.compare(0, 18, "Run till exit from") == 0) continue;

        bool interesting = (kind == DebuggerLldb) ? line.find("frame #0:") != std::string::npos
                                                  : line.find(" at ") != std::string::npos;
        if (!interesting || stop.stopped) continue;

        size_t at = line.rfind(" at ");
        if (at == std::string::npos) continue;

        std::string where = trimmed(line.substr(at + 4));
        size_t colon = where.find_last_of(':');
        if (colon == std::string::npos) continue;

        std::string tail = where.substr(colon + 1);
        std::string head = where.substr(0, colon);
        if (!digits(tail)) continue;

        // lldb gives file:line:column and gdb gives file:line, so a second
        // colon with a number after it means the first number was the column.
        size_t second = head.find_last_of(':');
        if (second != std::string::npos && digits(head.substr(second + 1))) {
            tail = head.substr(second + 1);
            head = head.substr(0, second);
        }

        stop.file = head;
        stop.line = number(tail);
        stop.stopped = true;

        // The function is before the " at ", after the last backtick that lldb
        // puts between the program and the name, or before the "()" gdb puts
        // after it.
        std::string front = line.substr(0, at);
        size_t tick = front.find_last_of('`');
        if (tick != std::string::npos) front = front.substr(tick + 1);
        size_t comma = front.find_last_of(',');
        if (comma != std::string::npos) front = front.substr(comma + 1);

        // gdb names the address before the function when it did not stop at
        // the start of a line: "0x00000000004011b3 in main ()".
        size_t in = front.rfind(" in ");
        if (in != std::string::npos) front = front.substr(in + 4);
        // gdb writes "main ()" and lldb writes "twice(n=1)": the name is what
        // is before the bracket either way, and the arguments are not part of
        // it - they are in the variables, spelled properly.
        size_t bracket = front.find('(');
        if (bracket != std::string::npos) front = front.substr(0, bracket);
        stop.function = trimmed(front);
    }

    // gdb says where it is only when that changes. A step that stays in the
    // same function prints the new line and nothing else - "10\tfor (int i = 1;
    // ..." - so a reader looking only for " at " concludes the program did not
    // stop at all. It did; it is in the same place as before, one line on.
    if (!stop.stopped && !stop.exited) {
        for (size_t i = 0; i < all.size(); ++i) {
            std::string line = withoutPrompt(all[i]);
            size_t tab = line.find('\t');
            if (tab == std::string::npos || tab == 0) continue;
            if (!digits(line.substr(0, tab))) continue;
            stop.line = number(line.substr(0, tab));
            stop.stopped = true;
            break;   // the file and the function are whatever they already were
        }
    }

    return stop;
}

// lldb:  (int) total = 0
// gdb:   total = 0
std::vector<Variable> readVariables(DebuggerKind kind, const std::string& said) {
    std::vector<Variable> found;
    std::vector<std::string> all = lines(said);

    for (size_t i = 0; i < all.size(); ++i) {
        std::string line = trimmed(withoutPrompt(all[i]));
        if (line.empty() || line == kMarker) continue;

        std::string type;
        if (kind == DebuggerLldb) {
            if (line.empty() || line[0] != '(') continue;
            size_t close = line.find(')');
            if (close == std::string::npos) continue;
            type = line.substr(1, close - 1);
            line = trimmed(line.substr(close + 1));
        }

        size_t equals = line.find(" = ");
        if (equals == std::string::npos) continue;

        Variable variable;
        variable.name = trimmed(line.substr(0, equals));
        variable.type = type;
        variable.value = trimmed(line.substr(equals + 3));
        if (variable.name.empty() || variable.name.find(' ') != std::string::npos) continue;
        found.push_back(variable);
    }
    return found;
}

// ---- the conversation ------------------------------------------------------

Debugger::Debugger() : kind_(DebuggerNone) {}
Debugger::~Debugger() { stop(); }

bool Debugger::start(const std::string& executable, const std::string& program) {
    stop();

    kind_ = debuggerHere();
    if (kind_ == DebuggerNone) return false;

    executable_ = executable;
    std::string run = program.empty() ? debuggerProgram(kind_) : program;
    if (!child_.start(quoted(run) + " " + quoted(executable))) {
        kind_ = DebuggerNone;
        return false;
    }

    std::vector<std::string> first = preamble(kind_);
    for (size_t i = 0; i < first.size(); ++i) child_.say(first[i]);

    // Nothing is believed until it has answered once: a debugger that is not
    // installed is a shell that said "not found" and went away.
    bool found = false;
    sayMarker(child_, kind_);
    child_.readUntil(kMarker, &found);
    if (!found) {
        child_.stop();
        kind_ = DebuggerNone;
        return false;
    }
    return true;
}

void Debugger::stop() {
    if (child_.running()) {
        child_.say("quit");
        child_.stop();
    }
    kind_ = DebuggerNone;
}

std::string Debugger::ask(const std::string& command) {
    if (!running()) return std::string();

    child_.say(command);
    sayMarker(child_, kind_);

    bool found = false;
    std::string said = child_.readUntil(kMarker, &found);
    if (!found) child_.stop();
    return said;
}

bool Debugger::breakAt(const std::string& file, size_t line) {
    if (!running()) return false;

    char digitsIn[32];
    std::snprintf(digitsIn, sizeof digitsIn, "%lu", static_cast<unsigned long>(line));

    std::string said;
    if (kind_ == DebuggerGdb)
        said = ask("break " + path::filename(file) + ":" + digitsIn);
    else
        said = ask("breakpoint set --file " + quoted(path::filename(file)) + " --line " + digitsIn);

    // Both say the word when they made one, and say something else entirely
    // when they could not.
    return said.find("Breakpoint") != std::string::npos ||
           said.find("breakpoint") != std::string::npos;
}

bool Debugger::clearBreakpoints() {
    if (!running()) return false;
    ask(kind_ == DebuggerGdb ? "delete" : "breakpoint delete --force");
    return true;
}

Stop Debugger::afterMoving(const std::string& command) {
    Stop stop = readStop(kind_, ask(command));
    if (!running()) stop.stopped = false;

    // What it did not say has not changed. gdb names the file and the function
    // only when the step left the one it was in.
    if (stop.stopped) {
        if (stop.file.empty()) stop.file = last_.file;
        if (stop.function.empty()) stop.function = last_.function;
        last_ = stop;
    } else if (stop.exited) {
        last_ = Stop();
    }
    return stop;
}

Stop Debugger::run() {
    // gdb takes the redirection on the command; lldb was told once, in the
    // preamble, and would not understand it here.
    return afterMoving(kind_ == DebuggerGdb ? "run < /dev/null" : "run");
}

Stop Debugger::resume()   { return afterMoving("continue"); }
Stop Debugger::stepOver() { return afterMoving("next"); }
Stop Debugger::stepInto() { return afterMoving("step"); }
Stop Debugger::stepOut()  { return afterMoving("finish"); }

std::vector<Variable> Debugger::locals() {
    if (!running()) return std::vector<Variable>();

    // lldb's `frame variable` is everything in scope, arguments included. gdb
    // keeps the two apart and `info locals` leaves the arguments out, so both
    // are asked for - an argument is exactly the thing you want to see when
    // you have just stepped into a function.
    if (kind_ != DebuggerGdb) return readVariables(kind_, ask("frame variable"));

    std::vector<Variable> found = readVariables(kind_, ask("info args"));
    std::vector<Variable> locals = readVariables(kind_, ask("info locals"));
    for (size_t i = 0; i < locals.size(); ++i) found.push_back(locals[i]);
    return found;
}

}  // namespace editor
