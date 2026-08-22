#include "debugger.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

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
std::string markerCommand(DebuggerKind kind) {
    if (kind == DebuggerGdb) return "echo <<ed1-done>>\\n";
    if (kind == DebuggerCdb) {
        // Assembled from a character code, so that the marker is not in the
        // command. cdb by itself echoes nothing and ".echo <<ed1-done>>" was
        // enough - but on a console it is the console that echoes, and the
        // echo arrived before the answer and was read as it. That is the same
        // fault the lldb spelling above exists to avoid, reaching cdb by a
        // different road once it was given a console to be unbuffered on.
        return ".printf \"<<ed1%cdone>>\\n\", 0x2d";
    }
    return "script print(\"<<ed1\" + \"-done>>\")";
}

void sayMarker(Process& child, DebuggerKind kind) {
    child.say(markerCommand(kind));
}

// Whether a program of this name is on PATH. Asked rather than assumed,
// because what depends on the answer is a command that would otherwise stop
// the debugger from starting at all.
bool onPath(const std::string& name) {
#ifdef _WIN32
    (void)name;
    return false;
#else
    const char* where = std::getenv("PATH");
    if (!where) return false;
    std::string all = where;
    size_t from = 0;
    while (from <= all.size()) {
        size_t end = all.find(':', from);
        std::string dir = all.substr(from, end == std::string::npos ? std::string::npos : end - from);
        if (!dir.empty() && path::exists(path::join(dir, name))) return true;
        if (end == std::string::npos) break;
        from = end + 1;
    }
    return false;
#endif
}

// Said once, before anything else, to make the thing driveable.
std::vector<std::string> preamble(DebuggerKind kind) {
    std::vector<std::string> said;
    if (kind == DebuggerGdb) {
        said.push_back("set confirm off");
        // Without this gdb stops every screenful to ask, and the answer it is
        // waiting for never comes down a pipe.
        said.push_back("set pagination off");
        // And without this it folds a long line, which is not cosmetic here:
        // where it stopped is read from "function (args) at file:line", and a
        // long enough path makes gdb put the "at file:line" half on a line of
        // its own. What is then read is a stop with no function in it - and
        // only for projects whose path is long, which is why every suite here
        // was green while the editor showed a blank function name on the box
        // and not on the Mac.
        said.push_back("set width unlimited");
        said.push_back("set breakpoint pending on");
        // What the program prints has to arrive when it prints it, and under
        // gdb it does not: the program inherits gdb's own stdout, which is
        // this editor's pipe, so the C runtime full-buffers it and everything
        // it printed turns up at once when it exits. Stepping over a printf
        // then shows nothing, which is most of what stepping is for.
        //
        // stdbuf runs it with that buffering turned off. It is asked for by
        // name first because a wrapper that is not there is not a degraded
        // feature but a debugger that will not start: gdb reports the failure
        // from inside the program's own startup. Without it the output still
        // arrives, at the end, exactly as it did before.
        //
        // lldb needs none of this - it gives the program a pseudo-terminal, so
        // the same program is line-buffered there and prints as it goes. That
        // was measured on all three rather than reasoned about.
        if (onPath("stdbuf")) said.push_back("set exec-wrapper stdbuf -o0 -e0");
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
            // "0:000> " when it is about to say something, and "0:000>" when
            // what follows was typed at it - a console puts the echo hard
            // against the prompt. Both are the prompt.
            if (j == i + 1 || out.compare(j, 1, ">") != 0) break;
            at = j + 1;
            if (at < out.size() && out[at] == ' ') ++at;
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

bool startsWith(const std::string& text, const char* prefix) {
    const size_t n = std::strlen(prefix);
    return text.size() >= n && text.compare(0, n, prefix) == 0;
}

bool endsWith(const std::string& text, char c) {
    return !text.empty() && text[text.size() - 1] == c;
}

// A prompt at the start of the line, and what it was.
DebuggerKind promptOn(const std::string& line) {
    if (startsWith(line, "(gdb)")) return DebuggerGdb;
    if (startsWith(line, "(lldb)")) return DebuggerLldb;
    size_t i = 0;
    while (i < line.size() && line[i] >= '0' && line[i] <= '9') ++i;
    if (i == 0 || i >= line.size() || line[i] != ':') return DebuggerNone;
    size_t j = i + 1;
    while (j < line.size() && std::isxdigit(static_cast<unsigned char>(line[j]))) ++j;
    if (j == i + 1 || line.compare(j, 1, ">") != 0) return DebuggerNone;
    return DebuggerCdb;
}

// A source line as a debugger echoes it: a number, then the line itself after
// a tab. gdb writes "8\tx = x + 1;" and lldb "   8   \tx = x + 1;", marking
// the one it is on with "-> ". The tab is required rather than assumed, so
// that a program printing "8 apples" keeps its output.
bool sourceEcho(const std::string& line) {
    std::string rest = trimmed(line);
    if (startsWith(rest, "-> ")) rest = trimmed(rest.substr(3));
    size_t i = 0;
    while (i < rest.size() && rest[i] >= '0' && rest[i] <= '9') ++i;
    if (i == 0) return false;
    while (i < rest.size() && rest[i] == ' ') ++i;
    return i < rest.size() && rest[i] == '\t';
}

bool lldbOwn(const std::string& line) {
    const std::string bare = trimmed(line);
    if (startsWith(bare, "* thread #")) return true;
    if (startsWith(bare, "frame #")) return true;
    if (bare == "^") return true;                       // under the source echo
    if (startsWith(line, "Process ") || startsWith(line, "Target ")) return true;
    return false;
}

bool gdbOwn(const std::string& line) {
    if (startsWith(line, "Starting program:")) return true;
    if (startsWith(line, "Continuing.")) return true;
    if (startsWith(line, "Breakpoint ")) return true;   // "Breakpoint 1, main () at f:8"
    if (startsWith(line, "[")) return true;             // threads, and the exit report
    if (startsWith(line, "Using host ")) return true;
    if (startsWith(line, "Reading symbols")) return true;
    // Its stock complaint about debuginfo, which is about the machine rather
    // than about the program and turns up at every stop on some of them.
    if (startsWith(line, "Missing ") &&
        (line.find("debuginfo") != std::string::npos || line.find("try:") != std::string::npos))
        return true;
    return false;
}

// What this editor says to cdb. A console echoes what is typed at it, and an
// echo is not the program talking - so these come out of what the console is
// shown. They are listed rather than guessed at because they are ours: every
// one of them is sent from this file or from Debugger's moves.
//
// A program that prints one of these words alone loses that line. They are
// short and the loss is real, which is why the list is exactly what is sent
// and not a shape that might match more.
bool ourCommand(const std::string& bare) {
    static const char* const said[] = {
        "g", "p", "t", "gu", "k", "q", "ln", "dv", "l+t", "n 10",
        ".lines -e", ".lastevent", ".echo", ".printf"
    };
    for (size_t i = 0; i < sizeof said / sizeof said[0]; ++i)
        if (bare == said[i]) return true;
    return startsWith(bare, "bp ") || startsWith(bare, "bu ") ||
           startsWith(bare, "bc ") || startsWith(bare, ".printf ");
}

bool cdbOwn(const std::string& line) {
    const std::string bare = trimmed(line);
    if (ourCommand(bare)) return true;
    if (startsWith(bare, "Breakpoint ")) return true;
    if (startsWith(bare, "Last event:")) return true;
    if (startsWith(bare, "debugger time:")) return true;
    if (startsWith(bare, "ModLoad:")) return true;
    // "ed1_run_4116!main+0x2a:" - where it is, named by symbol.
    if (bare.find('!') != std::string::npos && endsWith(bare, ':')) return true;
    // "(00007ff6`...)   ed1_run!main+0x2a   |  (...)" - the frame either side.
    if (startsWith(bare, "(") && bare.find('!') != std::string::npos) return true;
    // "00007ff6`08a8718a 8b442420  mov  eax,..." - an instruction, which is
    // known by the backtick in the address cdb writes and nothing else does.
    if (bare.find('`') != std::string::npos &&
        std::isxdigit(static_cast<unsigned char>(bare[0]))) return true;
    // "C:\...\talker.cpp(8)" - the source position, on its own line.
    if (endsWith(bare, ')') &&
        (bare.find(".c(") != std::string::npos || bare.find(".cpp(") != std::string::npos ||
         bare.find(".h(") != std::string::npos)) return true;
    return false;
}

}  // namespace

bool dbg_stoppedWithNoSource(const std::string& said) {
    return said.find("stop reason") != std::string::npos ||
           said.find("frame #0") != std::string::npos ||
           said.find("#0  0x") != std::string::npos;
}

std::string dbg_programOutput(DebuggerKind kind, const std::string& said) {
    const std::vector<std::string> all = lines(said);
    std::string out;

    for (size_t i = 0; i < all.size(); ++i) {
        std::string line = all[i];

        // Only lldb's prompt takes its line with it. lldb echoes every
        // command it is given down a pipe, so what follows its prompt is that
        // echo - the same fact sayMarker is built around. gdb echoes nothing,
        // so after its prompt comes either a message of its own, which gdbOwn
        // knows, or the program's output; and cdb is the same. Treating gdb's
        // prompt line as gdb's own threw away "(gdb) MARKER-TWO 2" - which is
        // what a program's line looks like the moment it is not buffered.
        const DebuggerKind prompt = promptOn(line);
        if (prompt == DebuggerLldb) continue;
        if (prompt != DebuggerNone) line = withoutPrompt(line);

        if (trimmed(line).empty()) continue;
        if (line.find("<<ed1") != std::string::npos) continue;   // the marker, and asking for it
        if (sourceEcho(line)) continue;

        if (kind == DebuggerLldb && lldbOwn(line)) continue;
        if (kind == DebuggerGdb && gdbOwn(line)) continue;
        if (kind == DebuggerCdb && cdbOwn(line)) continue;

        out += line;
        out += "\n";
    }
    return out;
}

// A console writes escape sequences as well as text - CSI to move the cursor
// and set colour, OSC to name the window. None of them are the debugger's
// words, and left in they reach the stop-reading as though they were.
std::string dbg_withoutEscapes(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\x1b') { out += text[i]; continue; }
        if (i + 1 >= text.size()) break;
        const char kind = text[i + 1];
        if (kind == '[') {                       // CSI: ends at its final byte
            size_t at = i + 2;
            while (at < text.size() && !(text[at] >= '@' && text[at] <= '~')) ++at;
            i = at;
        } else if (kind == ']') {                // OSC: ends at BEL, or ESC-backslash
            size_t at = i + 2;
            while (at < text.size() && text[at] != '\x07' &&
                   !(text[at] == '\x1b' && at + 1 < text.size() && text[at + 1] == '\\')) ++at;
            if (at < text.size() && text[at] == '\x1b') ++at;
            i = at;
        } else {
            ++i;                                  // a two-character sequence
        }
    }
    return out;
}

// A console echoes what is typed at it, so an answer begins with the question.
// Taken out by name rather than by pattern: what was said is known here, and
// only the first line matching it is the echo.
std::string dbg_withoutEcho(const std::string& said, const std::string& asked,
                            const std::string& marker) {
    std::vector<std::string> all = lines(said);
    bool droppedAsked = false, droppedMarker = false;
    std::string out;
    for (size_t i = 0; i < all.size(); ++i) {
        const std::string bare = trimmed(all[i]);
        if (!droppedAsked && bare == trimmed(asked)) { droppedAsked = true; continue; }
        if (!droppedMarker && bare == trimmed(marker)) { droppedMarker = true; continue; }
        out += all[i];
        out += "\n";
    }
    return out;
}

DebuggerKind dbg_here() {
#if defined(_WIN32)
    return DebuggerNone;
#elif defined(__APPLE__)
    return DebuggerLldb;
#else
    return DebuggerGdb;
#endif
}

const char* dbg_name(DebuggerKind kind) {
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
const char* dbg_program(DebuggerKind kind) {
    if (kind != DebuggerCdb) return dbg_name(kind);

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

DebuggerKind dbg_for(ToolchainKind kind, const std::string& arch) {
    // Nothing to read is the first way to have no debugger.
    if (!emitsDebugInfo(kind, arch)) return DebuggerNone;

    // cl writes CodeView into a .pdb, and what reads a .pdb is cdb - Microsoft's
    // own console debugger, which comes with the Windows SDK's debugging tools
    // and is not installed by default. It is driven the same way as the other
    // two and is looked for rather than assumed.
    if (kind == ToolMsvc)
        return path::exists(dbg_program(DebuggerCdb)) ? DebuggerCdb : DebuggerNone;

    return dbg_here();
}

bool dbg_stopsItself(ToolchainKind kind) { return kind == ToolShc; }

DebugPlan dbg_planFor(const Toolchain& tool, const std::vector<Part>& parts,
                      const std::string& arch) {
    DebugPlan plan;
    if (parts.empty()) return plan;

    plan.kind = toolchainOf(tool, parts[0]);
    for (size_t i = 0; i < parts.size(); ++i) {
        ToolchainKind each = toolchainOf(tool, parts[i]);
        DebuggerKind theirs = dbg_for(each, arch);
        if (theirs == DebuggerNone) {
            // Named by its group, which is what somebody reading the console
            // can act on. A part with no group is one compiler's whole share,
            // so the compiler's name is the honest thing to call it.
            plan.blind.push_back(parts[i].group.empty() ? std::string(toolchainName(each))
                                                        : parts[i].group);
            continue;
        }
        if (plan.engine == DebuggerNone) { plan.engine = theirs; plan.kind = each; }
    }

    // Asked after that loop and before anything refuses, because both of those
    // are about a debugger and there is none here to have or to lack.
    plan.stopsItself = dbg_stopsItself(toolchainOf(tool, parts[0]));
    if (plan.stopsItself) {
        plan.kind = ToolShc;
        // The loop above put every Shalimar group in `blind`, since dbg_for
        // rightly answers none for shc - and then the program stops in them.
        // "carries no debug information, the debugger cannot stop in it"
        // printed over a program that is about to stop is worse than saying
        // nothing. It carries none and stops anyway; that is the language.
        plan.blind.clear();
    }
    return plan;
}

std::string dbg_whyNot(ToolchainKind kind, const std::string& arch) {
    if (dbg_for(kind, arch) != DebuggerNone) return std::string();

    // Nothing is missing here, so nothing is named as missing. shc reaches
    // this only from somewhere that asked in the wrong order - dbg_stopsItself
    // comes first - and the answer it used to get was the sentence below about
    // cc1 and MASM, which names a compiler that had nothing to do with it.
    if (dbg_stopsItself(kind))
        return "a Shalimar program stops itself - it needs no debugger at all";
    if (kind == ToolMsvc)
        return "cl writes a .pdb and cdb reads one, but cdb is not installed - "
               "add Debugging Tools for Windows";
    if (!emitsDebugInfo(kind, arch))
        return "cc1 generates MASM for " + arch + ", which carries no line table";
    return std::string("no ") + dbg_name(dbg_here()) + " on this machine";
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
namespace {

// One line of lldb's or gdb's, read as a place: a function, and the file and
// line it is standing on. "main () at s.c:11" is that line whether it is where
// the program halted or a frame that is waiting for a call to come back, so
// there is one reader for it rather than one per caller.
//
// False when the line does not say where, which is most lines and also a frame
// with no source behind it - "dyld`start + 1903".
bool placeIn(const std::string& line, StackFrame* frame) {
    size_t at = line.rfind(" at ");
    if (at == std::string::npos) return false;

    // Where, after the " at ": the file is what follows up to a colon, read
    // from the right, since a Windows path holds one that is not a separator.
    std::string where = trimmed(line.substr(at + 4));
    size_t colon = where.find_last_of(':');
    if (colon == std::string::npos) return false;

    std::string tail = where.substr(colon + 1);
    std::string head = where.substr(0, colon);
    if (!digits(tail)) return false;

    // lldb gives file:line:column and gdb gives file:line, so a second colon
    // with a number after it means the first number was the column.
    size_t second = head.find_last_of(':');
    if (second != std::string::npos && digits(head.substr(second + 1))) {
        tail = head.substr(second + 1);
        head = head.substr(0, second);
    }

    frame->file = head;
    frame->line = number(tail);

    // The function is before the " at ", after the last backtick that lldb
    // puts between the program and the name, or before the "()" gdb puts
    // after it.
    std::string front = line.substr(0, at);
    size_t tick = front.find_last_of('`');
    if (tick != std::string::npos) front = front.substr(tick + 1);

    // gdb writes "main ()" and lldb writes "twice(n=1)": the name is what is
    // before the bracket either way, and the arguments are not part of it -
    // they are in the variables, spelled properly.
    //
    // This has to come before the comma below, and did not, which cost the
    // name of every function taking more than one argument: lldb prints
    // "sums`addUp(a=2, b=40)", the comma inside the arguments was the last one
    // on the line, and what came back was "b=40)". One argument or none has no
    // comma, so every test here had passed.
    size_t bracket = front.find('(');
    if (bracket != std::string::npos) front = front.substr(0, bracket);

    // gdb puts what it was doing before the name: "Breakpoint 1, main ()".
    size_t comma = front.find_last_of(',');
    if (comma != std::string::npos) front = front.substr(comma + 1);

    // and names the address before it when it did not stop at the start of a
    // line: "0x00000000004011b3 in main ()".
    size_t in = front.rfind(" in ");
    if (in != std::string::npos) front = front.substr(in + 4);

    frame->function = trimmed(front);
    return true;
}

}  // namespace

Stop dbg_readCdbStop(const std::string& said) {
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

Stop dbg_readStop(DebuggerKind kind, const std::string& said) {
    if (kind == DebuggerCdb) return dbg_readCdbStop(said);

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

        StackFrame here;
        if (!placeIn(line, &here)) continue;

        stop.file = here.file;
        stop.line = here.line;
        stop.function = here.function;
        stop.stopped = true;
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

namespace {

// The address on the front of lldb's own frame line:
//
//   frame #0: 0x00000001000004a0 m`addUp(a=1, b=-253525928) at sum.c:4:5
//
// Zero when the transcript has no such line, which is every line gdb and cdb
// write and most of lldb's.
unsigned long long lldbFrameAddress(const std::string& said) {
    std::vector<std::string> all = lines(said);
    for (size_t i = 0; i < all.size(); ++i) {
        std::string line = withoutPrompt(all[i]);
        size_t frame = line.find("frame #0:");
        if (frame == std::string::npos) continue;
        size_t hex = line.find("0x", frame);
        if (hex == std::string::npos) continue;
        return std::strtoull(line.c_str() + hex, 0, 16);
    }
    return 0;
}

}  // namespace

bool dbg_wentNowhere(DebuggerKind kind, const Stop& before, const Stop& after) {
    // gdb and cdb both step by line and answer once. Only lldb needs this,
    // and asking it of the others would be a rule with nothing to apply to.
    if (kind != DebuggerLldb) return false;

    // A program that has ended has been somewhere, and a step that could not
    // be read at all is not a step that went nowhere - it is a debugger to be
    // reported rather than one to be asked again.
    if (!before.stopped || !after.stopped || after.exited) return false;
    if (after.file.empty() || after.function.empty() || after.line == 0) return false;

    if (after.file != before.file || after.line != before.line) return false;
    if (after.function != before.function) return false;

    // A breakpoint the person set is always a real stop, wherever it is. This
    // costs nothing in the case being fixed - a step that shuffled along says
    // "stop reason = step over" - and it is what stops a breakpoint on the
    // stepped line from being stepped straight past.
    if (after.said.find("stop reason = breakpoint") != std::string::npos) return false;

    // The same place, then - so what is left is whether the program moved
    // forward inside it. Forward and not merely elsewhere: a step into a
    // recursive call is the same line of the same function, and its address
    // goes back to the callee's prologue, which is a real arrival.
    unsigned long long was = lldbFrameAddress(before.said);
    unsigned long long now = lldbFrameAddress(after.said);
    return was != 0 && now > was;
}

// lldb:  (int) total = 0
// gdb:   total = 0
std::vector<Variable> dbg_readVariables(DebuggerKind kind, const std::string& said) {
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

// What the frame numbering on the front of a backtrace line is, and what is
// left when it comes off.
//
//   lldb:  * frame #0: 0x00000001000037bc stepped`twice(n=1) at stepped.c:3:9
//   gdb:   #1  0x00000000004011b3 in main () at stepped.c:11
//
// Empty when the line is not a frame at all - the "* thread #1, stop reason =
// ..." heading lldb puts above its stack, and gdb's blank lines.
std::string afterFrameNumber(DebuggerKind kind, const std::string& line) {
    if (kind == DebuggerLldb) {
        size_t frame = line.find("frame #");
        if (frame == std::string::npos) return std::string();
        size_t colon = line.find(':', frame);
        if (colon == std::string::npos) return std::string();
        return trimmed(line.substr(colon + 1));
    }

    if (line.empty() || line[0] != '#') return std::string();
    size_t i = 1;
    while (i < line.size() && line[i] >= '0' && line[i] <= '9') ++i;
    if (i == 1) return std::string();
    return trimmed(line.substr(i));
}

// cdb's stack is a table, and the source is in brackets at the end of the row
// because .lines -e was asked for when it started:
//
//   Child-SP          RetAddr               Call Site
//   000000f5`5e4ffbd8 00007ff6`9bb77190     counted!twice+0x4 [C:\work\counted.cpp @ 3]
//   (Inline Function) --------`-------- counted!invoke_main+0x22 [exe_common.inl @ 78]
//   000000f5`5e4ffc60 00007ffb`d63aad6c     KERNEL32!BaseThreadInitThunk+0x17
//
// What is read is the bracket and the call site immediately before it, rather
// than the columns counted from the left. `k` numbers no frames - `kn` does,
// and a reader written against `kn`'s "00 " and "01 " finds nothing in `k`'s
// output at all, which is what the first version of this did on the one
// machine that has cdb. The row for an inlined call has dashes where the
// stack pointer would be and is a frame like any other, which is the second
// reason not to read from the left.
//
// A row with no bracket named no source: the heading, and the CRT and kernel
// frames under main.
bool cdbFrameIn(const std::string& line, StackFrame* frame) {
    size_t open = line.rfind('[');
    size_t close = line.rfind(']');
    if (open == std::string::npos || close == std::string::npos || close < open) return false;

    // "C:\work\counted.cpp @ 3" - the at-sign is cdb's own separator here, and
    // the colon after the drive letter is not one.
    std::string where = line.substr(open + 1, close - open - 1);
    size_t sign = where.rfind(" @ ");
    if (sign == std::string::npos) return false;
    std::string tail = trimmed(where.substr(sign + 3));
    if (!digits(tail)) return false;

    frame->file = trimmed(where.substr(0, sign));
    frame->line = number(tail);

    // "counted!twice+0x4": the last column before the source, then the module,
    // then the name, then how far into it.
    std::string call = trimmed(line.substr(0, open));
    size_t space = call.find_last_of(" \t");
    if (space != std::string::npos) call = call.substr(space + 1);
    size_t bang = call.find('!');
    if (bang == std::string::npos) return false;
    call = call.substr(bang + 1);
    size_t end = call.find_first_of("+ \t(");
    frame->function = trimmed(end == std::string::npos ? call : call.substr(0, end));
    return true;
}

std::vector<StackFrame> dbg_readFrames(DebuggerKind kind, const std::string& said) {
    std::vector<StackFrame> found;
    std::vector<std::string> all = lines(said);

    for (size_t i = 0; i < all.size(); ++i) {
        std::string line = trimmed(withoutPrompt(all[i]));
        if (line.empty() || line == kMarker) continue;

        StackFrame frame;
        if (kind == DebuggerCdb) {
            if (!cdbFrameIn(line, &frame)) continue;
        } else {
            std::string rest = afterFrameNumber(kind, line);
            if (rest.empty() || !placeIn(rest, &frame)) continue;
        }

        found.push_back(frame);
        // And no further: see the header for why the stack ends at main.
        if (frame.function == "main") break;
    }
    return found;
}

std::string dbg_frameLine(const StackFrame& frame, bool looking) {
    // std::to_string rather than the number() above it, which reads a number
    // out of a string and is the traffic going the other way.
    return std::string(looking ? "> " : "  ") + frame.function + "   " +
           path::filename(frame.file) + ":" + std::to_string(frame.line);
}

size_t dbg_frameOnLine(const std::vector<StackFrame>& stack, const std::string& line) {
    const std::string bare = trimmed(line);
    for (size_t i = 0; i < stack.size(); ++i) {
        // Both spellings, because the one being looked at is written with its
        // mark and is the one most likely to be pressed enter on again.
        if (trimmed(dbg_frameLine(stack[i], false)) == bare) return i;
        if (trimmed(dbg_frameLine(stack[i], true)) == bare) return i;
    }
    return stack.size();
}

std::string dbg_variableLine(const Variable& variable) {
    std::string said = "  " + variable.name + " = " + variable.value;
    if (!variable.type.empty()) said += "   [" + variable.type + "]";
    return said;
}

size_t dbg_variableOnLine(const std::vector<Variable>& locals, const std::string& line) {
    const std::string bare = trimmed(line);
    for (size_t i = 0; i < locals.size(); ++i)
        if (trimmed(dbg_variableLine(locals[i])) == bare) return i;
    return locals.size();
}

namespace {

// What a debugger says when it will not do what it was asked, and nothing when
// it did. The words rather than the first line it printed: lldb puts a caret
// under the offending word and the words themselves on the line after it, so a
// reader that took the first line came back with "^".
//
// Its own complaint is what the caller shows, because it names the mistake -
// "use of undeclared identifier 'nosuch'" - better than anything invented
// here. lldb's "error: " in front of it comes off, so that all three read
// alike; gdb and cdb print no such prefix.
std::string complaintIn(const std::string& answer) {
    const char* const words[] = {
        "error",               // lldb
        "No symbol",           // gdb, and cdb when the name is not there
        "not an lvalue",
        "Couldn't",
        "cannot be",
        "Syntax error",        // cdb
        "Type conflict",
        "Bad register error"
    };

    std::vector<std::string> all = lines(answer);
    for (size_t i = 0; i < all.size(); ++i) {
        std::string line = trimmed(withoutPrompt(all[i]));
        for (size_t w = 0; w < sizeof words / sizeof words[0]; ++w) {
            if (line.find(words[w]) == std::string::npos) continue;
            // lldb writes the caret and the words on one line when an
            // expression has more than one thing wrong with it -
            // "|       error: use of undeclared identifier 'i'" - so the
            // message starts wherever "error: " does, not only at the front.
            size_t at = line.find("error: ");
            if (at != std::string::npos) line = line.substr(at + 7);
            return line;
        }
    }
    return std::string();
}

}  // namespace

std::string dbg_watchLine(const Watch& watch) {
    return "  " + watch.expression + " = " + (watch.ok ? watch.value : "[" + watch.value + "]");
}

size_t dbg_watchOnLine(const std::vector<Watch>& watches, const std::string& line) {
    const std::string bare = trimmed(line);
    for (size_t i = 0; i < watches.size(); ++i)
        if (trimmed(dbg_watchLine(watches[i])) == bare) return i;
    return watches.size();
}

std::string dbg_readValue(DebuggerKind kind, const std::string& said) {
    std::vector<std::string> all = lines(said);

    for (size_t i = 0; i < all.size(); ++i) {
        const std::string line = trimmed(withoutPrompt(all[i]));
        if (line.empty() || line == kMarker) continue;

        if (kind == DebuggerCdb) {
            // "int 0n12", and "char * 0x00007ff6`44e8b000" - the type is what
            // comes before the last space, whatever it is made of, and the
            // value is what comes after it. A line with no space in it is not
            // an answer of this shape.
            //
            // The question is not the answer: "?? total" is what a console
            // echoes back before cdb says anything, and its last word is the
            // expression. Nothing cdb answers with begins with a question
            // mark, so that is what tells them apart.
            if (line[0] == '?') continue;

            size_t space = line.find_last_of(' ');
            if (space == std::string::npos || space + 1 >= line.size()) continue;
            std::string value = line.substr(space + 1);
            if (value.compare(0, 2, "0n") == 0) value = value.substr(2);

            // And an answer has a number in it somewhere - a decimal, a hex
            // address, a character code. A line of words is prose.
            if (value.find_first_of("0123456789") == std::string::npos) continue;
            return value;
        }

        // lldb answers "(int) $0 = 12" and gdb "$1 = 12". The $ is what tells
        // the answer from the echo of the command that asked for it, which
        // lldb prints first when its input is a pipe - and which would
        // otherwise be read as the answer whenever the expression had an = in
        // it.
        size_t dollar = line.find('$');
        if (dollar == std::string::npos) continue;
        size_t equals = line.find(" = ", dollar);
        if (equals == std::string::npos) continue;
        return trimmed(line.substr(equals + 3));
    }
    return std::string();
}

std::string dbg_stopLine(const std::string& file, size_t line,
                         const std::string& function) {
    return "stopped at " + path::filename(file) + ":" + std::to_string(line) +
           (function.empty() ? std::string() : " in " + function);
}

std::string dbg_lookingAt(const StackFrame& frame) {
    std::string where = path::filename(frame.file) + ":" + std::to_string(frame.line);
    if (frame.function.empty()) return "the variables are those of " + where;
    return "the variables are " + frame.function + "'s, at " + where;
}

// ---- the conversation ------------------------------------------------------

Debugger::Debugger() : kind_(DebuggerNone), onConsole_(false) {}
Debugger::~Debugger() { stop(); }

bool Debugger::start(DebuggerKind kind, const std::string& executable,
                     const std::string& program) {
    stop();

    kind_ = kind;
    if (kind_ == DebuggerNone) return false;

    executable_ = executable;
    std::string run = program.empty() ? dbg_program(kind_) : program;

    std::string command = quoted(run) + " " + quoted(executable);
    if (kind_ == DebuggerCdb) {
        // -y keeps it to the .pdb beside the program. Left alone it asks
        // Microsoft's symbol server about every system library it loads, over
        // the network, before saying anything at all.
        command = quoted(run) + " -y " + quoted(path::parent(executable)) +
                  " " + quoted(executable);
    }

    // cdb gets a console rather than a pipe, because the console is what its
    // program inherits: on a pipe the program's runtime full-buffers what it
    // prints and nothing arrives until it exits, so stepping over a line that
    // prints shows nothing. Measured on all three machines - lldb hands its
    // program a pseudo-terminal already and gdb is told to use stdbuf, so this
    // is Windows's share of the same fix.
    //
    // If there is no console to be had - an older Windows, or anything else -
    // it falls back to the pipe it always used, and the output arrives at the
    // end as it did before. Being unable to unbuffer is not a reason to be
    // unable to debug.
    onConsole_ = false;
    if (kind_ == DebuggerCdb && child_.startOnConsole(command)) {
        onConsole_ = true;
    } else if (!child_.start(command)) {
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

    // What a console adds, and only a console: the escape sequences it writes,
    // and its echo of what was just typed at it. Both would otherwise reach
    // the stop-reading and the console as though the debugger had said them.
    if (onConsole_) said = dbg_withoutEcho(dbg_withoutEscapes(said), command, markerCommand(kind_));
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

    Stop stop = dbg_readStop(kind_, said);
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

    // And the watches, wherever it has got to. Here rather than in the front
    // ends, because every way of moving arrives here and a watch that only
    // some of them refreshed would be worse than no watch at all.
    readWatches();
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

Stop Debugger::afterStepping(const std::string& command) {
    Stop before = last_;
    Stop stop = afterMoving(command);
    std::string said = stop.said;

    // Bounded, so that a line table stranger than the ones this was written
    // against cannot turn one press of F7 into a program that never comes
    // back. Two goes are what a return statement takes on arm64-darwin and
    // three what a one-line function does; sixteen is room and not a guess at
    // the number.
    for (int again = 0; again < 16 && dbg_wentNowhere(kind_, before, stop); ++again) {
        before = stop;
        stop = afterMoving(command);
        // What the program printed on the way is the program's, whether or
        // not the step it printed during is one the editor is going to show.
        // Without this a printf stepped over vanished on the Mac, which is
        // the fault dbg_programOutput exists to prevent.
        said += "\n" + stop.said;
    }

    stop.said = said;
    return stop;
}

Stop Debugger::resume() { return afterMoving(kind_ == DebuggerCdb ? "g" : "continue"); }
Stop Debugger::stepOver() { return afterStepping(kind_ == DebuggerCdb ? "p" : "next"); }
Stop Debugger::stepInto() { return afterStepping(kind_ == DebuggerCdb ? "t" : "step"); }
Stop Debugger::stepOut() { return afterMoving(kind_ == DebuggerCdb ? "gu" : "finish"); }

std::vector<Variable> Debugger::locals() {
    if (!running()) return std::vector<Variable>();

    // lldb's `frame variable` is everything in scope, arguments included. gdb
    // keeps the two apart and `info locals` leaves the arguments out, so both
    // are asked for - an argument is exactly the thing you want to see when
    // you have just stepped into a function.
    if (kind_ == DebuggerCdb) return dbg_readVariables(kind_, ask("dv"));
    if (kind_ != DebuggerGdb) return dbg_readVariables(kind_, ask("frame variable"));

    std::vector<Variable> found = dbg_readVariables(kind_, ask("info args"));
    std::vector<Variable> locals = dbg_readVariables(kind_, ask("info locals"));
    for (size_t i = 0; i < locals.size(); ++i) found.push_back(locals[i]);
    return found;
}

std::string Debugger::evaluate(const std::string& expression, bool* ok) {
    if (ok) *ok = false;
    if (!running() || expression.empty()) return std::string();

    // cdb's ?? is the C++ evaluator it was given for setting a variable; gdb's
    // print and lldb's expression are the same two commands as there. All
    // three answer from the frame they are currently in, which is what makes a
    // watch mean what it should after Ctrl-Up.
    std::string answer;
    if (kind_ == DebuggerCdb) {
        answer = ask("?? " + expression);
    } else if (kind_ == DebuggerGdb) {
        answer = ask("print " + expression);
    } else {
        answer = ask("expression " + expression);
    }

    const std::string value = dbg_readValue(kind_, answer);
    if (!value.empty()) {
        if (ok) *ok = true;
        return value;
    }

    // No value in it, so it would not answer, and its own words are the answer
    // instead - the same bargain setVariable makes.
    const std::string why = complaintIn(answer);
    return why.empty() ? "no answer" : why;
}

void Debugger::addWatch(const std::string& expression) {
    if (expression.empty()) return;
    Watch watch;
    watch.expression = expression;
    watches_.push_back(watch);
    if (running()) watches_[watches_.size() - 1].value =
        evaluate(expression, &watches_[watches_.size() - 1].ok);
}

void Debugger::setWatch(size_t which, const std::string& expression) {
    if (which >= watches_.size()) return;
    if (expression.empty()) { removeWatch(which); return; }
    watches_[which].expression = expression;
    watches_[which].value.clear();
    watches_[which].ok = false;
    if (running()) watches_[which].value = evaluate(expression, &watches_[which].ok);
}

void Debugger::removeWatch(size_t which) {
    if (which >= watches_.size()) return;
    watches_.erase(watches_.begin() + static_cast<long>(which));
}

// Read again, all of them, wherever the program has got to. This is what the
// list is for, and it is called from the moves themselves rather than by the
// front ends: a watch that only some of the ways of moving refreshed would be
// worse than no watch at all.
void Debugger::readWatches() {
    for (size_t i = 0; i < watches_.size(); ++i) {
        if (!running()) {
            watches_[i].value = "not running";
            watches_[i].ok = false;
            continue;
        }
        watches_[i].value = evaluate(watches_[i].expression, &watches_[i].ok);
    }
}

bool Debugger::setVariable(const std::string& name, const std::string& value,
                           std::string* said) {
    if (said) said->clear();
    if (!running() || name.empty() || value.empty()) return false;

    // cdb's ?? is its C++ expression evaluator, which takes an assignment;
    // gdb's `set variable` exists so that `set` cannot be mistaken for one of
    // its own settings - `set width = 3` sets the width; and lldb's
    // `expression` is the general one, `expr` being the abbreviation of it.
    std::string answer;
    if (kind_ == DebuggerCdb) {
        answer = ask("?? " + name + " = " + value);
    } else if (kind_ == DebuggerGdb) {
        answer = ask("set variable " + name + " = " + value);
    } else {
        answer = ask("expression " + name + " = " + value);
    }
    // Refused, in its own words. What the three have in common is that none of
    // them says anything at all when it worked - gdb and cdb print nothing and
    // lldb prints the value it now holds - so what is looked for is the
    // complaint rather than the success.
    const std::string why = complaintIn(answer);
    if (!why.empty()) {
        if (said) *said = why;
        return false;
    }

    // A write is a move as far as a watch is concerned: "total + i" is a
    // different number afterwards, and nobody should have to step to see it.
    readWatches();
    return true;
}

bool Debugger::selectFrame(size_t which) {
    if (!running()) return false;

    const std::string number = std::to_string(which);
    std::string said;
    if (kind_ == DebuggerCdb) {
        // .frame, and the numbers are decimal because `n 10` was said when it
        // started.
        said = ask(".frame " + number);
    } else if (kind_ == DebuggerGdb) {
        said = ask("frame " + number);
    } else {
        said = ask("frame select " + number);
    }

    // Each of them refuses a frame that is not there in its own words, and all
    // three then leave the current frame where it was - which would show one
    // frame's variables under another's name. Better to say it did not work.
    if (said.find("error:") != std::string::npos) return false;
    if (said.find("No frame at level") != std::string::npos) return false;
    if (said.find("Invalid frame") != std::string::npos) return false;

    // An expression means what the frame it is asked in says it means, so a
    // watch on `total` answers main's total after this and not the one that is
    // out of scope where the program stopped.
    readWatches();
    return true;
}

std::vector<StackFrame> Debugger::frames() {
    if (!running()) return std::vector<StackFrame>();

    // All three will print the whole stack unasked; none of them needs telling
    // how much of it to print, because dbg_readFrames stops reading at main.
    if (kind_ == DebuggerCdb) return dbg_readFrames(kind_, ask("k"));
    if (kind_ == DebuggerGdb) return dbg_readFrames(kind_, ask("backtrace"));
    return dbg_readFrames(kind_, ask("thread backtrace"));
}

}  // namespace editor
