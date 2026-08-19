#include "debugger.h"

#include <cctype>
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
    if (kind == DebuggerCdb) {
        // cdb does not echo commands either, so one .echo is enough.
        child.say(".echo <<ed1-done>>");
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
    } else if (kind == DebuggerCdb) {
        // Line information is not loaded unless it is asked for, and without
        // l+t both t and p step one instruction rather than one line of
        // source - which looks like a step that went nowhere, since the line
        // does not change. n 10 asks for numbers in decimal, though it does
        // not stop cdb prefixing them with 0n.
        said.push_back(".lines -e");
        said.push_back("l+t");
        said.push_back("n 10");
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
        if (out.compare(0, 6, "(gdb) ") == 0) {
            at = 6;
        } else if (out.compare(0, 7, "(lldb) ") == 0) {
            at = 7;
        } else {
            // cdb's is the thread it is talking about: "0:000> ".
            size_t i = 0;
            while (i < out.size() && out[i] >= '0' && out[i] <= '9') ++i;
            if (i == 0 || i >= out.size() || out[i] != ':') break;
            size_t j = i + 1;
            while (j < out.size() && std::isxdigit(static_cast<unsigned char>(out[j]))) ++j;
            if (j == i + 1 || out.compare(j, 2, "> ") != 0) break;
            at = j + 2;
        }
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
        case DebuggerCdb:  return "cdb";
        default:           return "none";
    }
}

// Two of them are on PATH and one is not. cdb comes with the Windows SDK's
// debugging tools, which put it under Windows Kits and add it to nothing, so
// it is named in full or not found at all.
const char* debuggerProgram(DebuggerKind kind) {
    if (kind != DebuggerCdb) return debuggerName(kind);

    // Made once and never destroyed, and a pointer rather than the string
    // itself. A function-local static with a destructor registers an atexit
    // handler the first time it is reached, and in the mixed-mode binary that
    // corrupts the heap - the second of the three hazards in the README, which
    // this walked straight into. ed1gui died the moment F8 asked which
    // debugger applied, and its own fault log named this function.
    static std::string* found = 0;
    if (found) return found->c_str();

    const char* under[2] = {"ProgramFiles(x86)", "ProgramFiles"};
    for (size_t i = 0; i < 2; ++i) {
        const char* root = std::getenv(under[i]);
        if (!root) continue;
        std::string where = std::string(root) + "\\Windows Kits\\10\\Debuggers\\x64\\cdb.exe";
        if (path::exists(where)) {
            found = new std::string(where);   // never deleted, on purpose
            return found->c_str();
        }
    }
    found = new std::string("cdb");   // on PATH, or about to say it is not there
    return found->c_str();
}

DebuggerKind debuggerFor(ToolchainKind kind, const std::string& arch) {
    // Nothing to read is the first way to have no debugger.
    if (!emitsDebugInfo(kind, arch)) return DebuggerNone;

    // cl writes CodeView into a .pdb, and what reads a .pdb is cdb - Microsoft's
    // own console debugger, which comes with the Windows SDK's debugging tools
    // and is not installed by default. It is driven the same way as the other
    // two and is looked for rather than assumed.
    if (kind == ToolMsvc)
        return path::exists(debuggerProgram(DebuggerCdb)) ? DebuggerCdb : DebuggerNone;

    return debuggerHere();
}

std::string noDebuggerBecause(ToolchainKind kind, const std::string& arch) {
    if (debuggerFor(kind, arch) != DebuggerNone) return std::string();

    if (kind == ToolMsvc)
        return "cl writes a .pdb and cdb reads one, but cdb is not installed - "
               "add Debugging Tools for Windows";
    if (!emitsDebugInfo(kind, arch))
        return "cc1 generates MASM for " + arch + ", which carries no line table";
    return std::string("no ") + debuggerName(debuggerHere()) + " on this machine";
}

// ---- reading what they say -------------------------------------------------

// lldb:  frame #0: 0x0000000100000508 dbg`main at dbg.c:13:9
// gdb:   Breakpoint 1, main () at dbg.c:13
//        13          total = total + twice(i);
// cdb answers a move with an address and an instruction, and says where that
// is in the source only when asked - so what is read here is its answer with
// the answer to `ln` appended, or to `r edx` when the program has ended.
//
//   C:\\work\\seam.cpp(10)+0x9
//   (00007ff6`44e87160)   seam!main+0x27   |  (00007ff6`44e871c0)   seam!pre_c_init
//
// Whether the program has ended is asked of cdb rather than guessed from where
// it stopped. It ends by breaking somewhere in ntdll, and which thread that is
// on is not fixed: often NtTerminateProcess on the main thread, but one run in
// four it was a worker sitting in ZwWaitForWorkViaWorkerFactory instead. Any
// test against the function it stopped in is a test that fails a quarter of the
// time. `.lastevent` says it outright:
//
//   Last event: 8ec.1ff0: Hit breakpoint 0
//   Last event: 8ec.1ff0: Exit process 0:8ec, code c
Stop readCdbStop(const std::string& said) {
    Stop stop;
    stop.said = said;

    std::vector<std::string> all = lines(said);
    for (size_t i = 0; i < all.size(); ++i) {
        std::string line = trimmed(withoutPrompt(all[i]));

        size_t ended = line.find("Exit process");
        if (ended != std::string::npos) {
            stop.exited = true;
            stop.stopped = false;
            size_t code = line.find("code ", ended);
            if (code != std::string::npos)
                stop.status = static_cast<int>(std::strtol(line.c_str() + code + 5, 0, 16));
            continue;
        }
        if (line.find("No runnable debuggees") != std::string::npos) {
            stop.exited = true;
            continue;
        }
        if (stop.stopped || stop.exited) continue;

        // The source line, which is the only thing here with a bracketed
        // number at the end of a path.
        size_t open = line.rfind('(');
        if (open == std::string::npos || open == 0) continue;
        size_t close = line.find(')', open);
        if (close == std::string::npos) continue;

        std::string number = line.substr(open + 1, close - open - 1);
        if (!digits(number)) continue;

        stop.file = line.substr(0, open);
        stop.line = editor::number(number);
        stop.stopped = true;

        // The function is on the line under it, between the module's ! and
        // whatever offset follows: "seam!main+0x27".
        for (size_t j = i + 1; j < all.size() && stop.function.empty(); ++j) {
            std::string under = withoutPrompt(all[j]);
            size_t bang = under.find('!');
            if (bang == std::string::npos) continue;
            std::string rest = under.substr(bang + 1);
            size_t end = rest.find_first_of("+ \t|(");
            stop.function = trimmed(end == std::string::npos ? rest : rest.substr(0, end));
        }
    }
    return stop;
}

Stop readStop(DebuggerKind kind, const std::string& said) {
    if (kind == DebuggerCdb) return readCdbStop(said);

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

        // cdb writes a decimal with 0n in front of it, which is how it tells
        // you it is not hex. The reader of a variables pane does not need
        // telling.
        if (kind == DebuggerCdb && variable.value.compare(0, 2, "0n") == 0)
            variable.value = variable.value.substr(2);
        if (variable.name.empty() || variable.name.find(' ') != std::string::npos) continue;
        found.push_back(variable);
    }
    return found;
}

// ---- the conversation ------------------------------------------------------

Debugger::Debugger() : kind_(DebuggerNone) {}
Debugger::~Debugger() { stop(); }

bool Debugger::start(DebuggerKind kind, const std::string& executable,
                     const std::string& program) {
    stop();

    kind_ = kind;
    if (kind_ == DebuggerNone) return false;

    executable_ = executable;
    std::string run = program.empty() ? debuggerProgram(kind_) : program;

    std::string command = quoted(run) + " " + quoted(executable);
    if (kind_ == DebuggerCdb) {
        // -y keeps it to the .pdb beside the program. Left alone it asks
        // Microsoft's symbol server about every system library it loads, over
        // the network, before saying anything at all.
        command = quoted(run) + " -y " + quoted(path::parent(executable)) +
                  " " + quoted(executable);
    }

    if (!child_.start(command)) {
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
    if (kind_ == DebuggerGdb) {
        said = ask("break " + path::filename(file) + ":" + digitsIn);
    } else if (kind_ == DebuggerCdb) {
        // The backticks are cdb's, and are what tell it that this is a source
        // line rather than a symbol. It says nothing at all when it works.
        said = ask("bp `" + path::filename(file) + ":" + digitsIn + "`");
        return said.find("Couldn't resolve") == std::string::npos &&
               said.find("Bp expression") == std::string::npos;
    } else {
        said = ask("breakpoint set --file " + quoted(path::filename(file)) + " --line " + digitsIn);
    }

    // Both of the others say the word when they made one, and say something
    // else entirely when they could not.
    return said.find("Breakpoint") != std::string::npos ||
           said.find("breakpoint") != std::string::npos;
}

bool Debugger::clearBreakpoints() {
    if (!running()) return false;
    ask(kind_ == DebuggerGdb ? "delete"
                             : (kind_ == DebuggerCdb ? "bc *" : "breakpoint delete --force"));
    return true;
}

Stop Debugger::afterMoving(const std::string& command) {
    std::string said = ask(command);

    // cdb answers a move with an address and an instruction, and neither says
    // whether the program is still there. So it is asked - and only if it is
    // still running is there any point asking where.
    if (kind_ == DebuggerCdb) {
        std::string event = ask(".lastevent");
        said += "\n" + event;
        if (event.find("Exit process") == std::string::npos) said += "\n" + ask("ln");
    }

    Stop stop = readStop(kind_, said);
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
    // cdb has already started the program: it loads it and stops at the
    // loader's own breakpoint, which is where the breakpoints were set. So
    // running it is the same word as carrying on.
    if (kind_ == DebuggerCdb) return afterMoving("g");

    // gdb takes the redirection on the command; lldb was told once, in the
    // preamble, and would not understand it here.
    return afterMoving(kind_ == DebuggerGdb ? "run < /dev/null" : "run");
}

Stop Debugger::resume() { return afterMoving(kind_ == DebuggerCdb ? "g" : "continue"); }
Stop Debugger::stepOver() { return afterMoving(kind_ == DebuggerCdb ? "p" : "next"); }
Stop Debugger::stepInto() { return afterMoving(kind_ == DebuggerCdb ? "t" : "step"); }
Stop Debugger::stepOut() { return afterMoving(kind_ == DebuggerCdb ? "gu" : "finish"); }

std::vector<Variable> Debugger::locals() {
    if (!running()) return std::vector<Variable>();

    // lldb's `frame variable` is everything in scope, arguments included. gdb
    // keeps the two apart and `info locals` leaves the arguments out, so both
    // are asked for - an argument is exactly the thing you want to see when
    // you have just stepped into a function.
    if (kind_ == DebuggerCdb) return readVariables(kind_, ask("dv"));
    if (kind_ != DebuggerGdb) return readVariables(kind_, ask("frame variable"));

    std::vector<Variable> found = readVariables(kind_, ask("info args"));
    std::vector<Variable> locals = readVariables(kind_, ask("info locals"));
    for (size_t i = 0; i < locals.size(); ++i) found.push_back(locals[i]);
    return found;
}

}  // namespace editor
