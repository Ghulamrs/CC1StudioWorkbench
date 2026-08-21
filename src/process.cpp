#include "process.h"

#include <cstdio>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace editor {

#ifdef _WIN32

// The pseudo-console entry points are asked for by name rather than linked
// against, so that this still builds with a toolchain whose headers predate
// them - and answers "no console" at run time on a Windows that has none.
// PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE is a number, and is one here for the
// same reason.
#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

typedef HRESULT (WINAPI *MakeConsole)(COORD, HANDLE, HANDLE, DWORD, void**);
typedef void (WINAPI *DropConsole)(void*);

MakeConsole makeConsole() {
    HMODULE kernel = GetModuleHandleA("kernel32.dll");
    return kernel ? (MakeConsole)GetProcAddress(kernel, "CreatePseudoConsole") : 0;
}

DropConsole dropConsole() {
    HMODULE kernel = GetModuleHandleA("kernel32.dll");
    return kernel ? (DropConsole)GetProcAddress(kernel, "ClosePseudoConsole") : 0;
}

struct Process::Held {
    HANDLE toChild;
    HANDLE fromChild;
    HANDLE child;
    void* console;      // the pseudo-console, when the child was given one

    Held() : toChild(NULL), fromChild(NULL), child(NULL), console(NULL) {}
};

#else

struct Process::Held {
    int toChild;
    int fromChild;
    pid_t child;

    Held() : toChild(-1), fromChild(-1), child(-1) {}
};

#endif

Process::Process() : held_(new Held()), running_(false) {}

Process::~Process() {
    stop();
    delete held_;
}

#ifdef _WIN32

bool Process::start(const std::string& command) {
    if (running_) return false;

    // The child must inherit its ends of the pipes and this process must not
    // hold on to them, or a child that exits leaves a read that never returns.
    SECURITY_ATTRIBUTES inherit;
    inherit.nLength = sizeof inherit;
    inherit.lpSecurityDescriptor = NULL;
    inherit.bInheritHandle = TRUE;

    HANDLE childReads = NULL, weWrite = NULL, weRead = NULL, childWrites = NULL;
    if (!CreatePipe(&childReads, &weWrite, &inherit, 0)) return false;
    if (!CreatePipe(&weRead, &childWrites, &inherit, 0)) {
        CloseHandle(childReads);
        CloseHandle(weWrite);
        return false;
    }
    SetHandleInformation(weWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(weRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA startup;
    std::memset(&startup, 0, sizeof startup);
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = childReads;
    startup.hStdOutput = childWrites;
    startup.hStdError = childWrites;   // one console, so one stream

    // Through cmd, like every other command here, and writable because
    // CreateProcess is allowed to modify what it is given.
    //
    // The extra pair of quotes is the same one compile.cpp needs and for the
    // same reason: cmd removes the first and last quote when a command has
    // both a quoted program and quoted arguments, which every command here
    // does. Without it the debugger was never reached - cdb lives under
    // "Program Files (x86)", so its path is quoted, and the program it is
    // given is quoted too.
    std::string line = "cmd /c \"" + command + "\"";
    std::vector<char> writable(line.begin(), line.end());
    writable.push_back('\0');

    PROCESS_INFORMATION made;
    std::memset(&made, 0, sizeof made);
    BOOL ok = CreateProcessA(NULL, &writable[0], NULL, NULL, TRUE, CREATE_NO_WINDOW,
                             NULL, NULL, &startup, &made);

    CloseHandle(childReads);
    CloseHandle(childWrites);
    if (!ok) {
        CloseHandle(weWrite);
        CloseHandle(weRead);
        return false;
    }
    CloseHandle(made.hThread);

    held_->toChild = weWrite;
    held_->fromChild = weRead;
    held_->child = made.hProcess;
    running_ = true;
    return true;
}

bool Process::startOnConsole(const std::string& command) {
    if (running_) return false;

    MakeConsole make = makeConsole();
    if (!make) return false;

    // The console owns one end of each pipe and this process the other. They
    // are not inheritable: what the child gets is the console, not these.
    HANDLE consoleReads = NULL, weWrite = NULL, weRead = NULL, consoleWrites = NULL;
    if (!CreatePipe(&consoleReads, &weWrite, NULL, 0)) return false;
    if (!CreatePipe(&weRead, &consoleWrites, NULL, 0)) {
        CloseHandle(consoleReads);
        CloseHandle(weWrite);
        return false;
    }

    // Wide on purpose. A console wraps what is written to it at its own width,
    // and a wrapped line is a line the stop-reading cannot parse - the same
    // fault "set width unlimited" exists to prevent in gdb, arriving here by a
    // different road.
    COORD size;
    size.X = 500;
    size.Y = 50;
    void* console = NULL;
    if (FAILED(make(size, consoleReads, consoleWrites, 0, &console))) {
        CloseHandle(consoleReads);
        CloseHandle(consoleWrites);
        CloseHandle(weWrite);
        CloseHandle(weRead);
        return false;
    }

    STARTUPINFOEXA startup;
    std::memset(&startup, 0, sizeof startup);
    startup.StartupInfo.cb = sizeof startup;

    SIZE_T room = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &room);
    std::vector<char> attributes(room);
    startup.lpAttributeList =
        reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(room ? &attributes[0] : 0);
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &room) ||
        !UpdateProcThreadAttribute(startup.lpAttributeList, 0,
                                   PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   console, sizeof console, NULL, NULL)) {
        DropConsole drop = dropConsole();
        if (drop) drop(console);
        CloseHandle(consoleReads);
        CloseHandle(consoleWrites);
        CloseHandle(weWrite);
        CloseHandle(weRead);
        return false;
    }

    // Straight to the program, with no cmd in front of it. start() goes
    // through cmd because a pipe-fed child wants its quoting rules; a console
    // does not need them - CreateProcess takes a quoted program itself - and
    // cmd in the middle of a pseudo-console is a second console client between
    // the debugger and the terminal, which is one too many.
    std::vector<char> writable(command.begin(), command.end());
    writable.push_back('\0');

    // Even with inheritance off, a child is handed this process's own standard
    // handles through its process parameters - and this process's are a pipe,
    // because the editor is itself started from one. The child then writes to
    // that pipe instead of to the console it was given, which is not a subtle
    // failure: the first version of this wrote the program's output into the
    // editor's own stdout and nothing came back at all. Cleared across the
    // call, so the console is the only thing there is to write to.
    HANDLE keepIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE keepOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE keepErr = GetStdHandle(STD_ERROR_HANDLE);
    SetStdHandle(STD_INPUT_HANDLE, NULL);
    SetStdHandle(STD_OUTPUT_HANDLE, NULL);
    SetStdHandle(STD_ERROR_HANDLE, NULL);

    PROCESS_INFORMATION made;
    std::memset(&made, 0, sizeof made);
    // No CREATE_NO_WINDOW here, though start() uses it. That flag says "a
    // console program with no console", and it wins: given both, the child got
    // no console at all and said nothing whatever - not one byte, which is
    // what sent this looking in the wrong place first. A pseudo-console shows
    // no window anyway; there is nothing for the flag to prevent.
    BOOL ok = CreateProcessA(NULL, &writable[0], NULL, NULL, FALSE,
                             EXTENDED_STARTUPINFO_PRESENT,
                             NULL, NULL, &startup.StartupInfo, &made);

    SetStdHandle(STD_INPUT_HANDLE, keepIn);
    SetStdHandle(STD_OUTPUT_HANDLE, keepOut);
    SetStdHandle(STD_ERROR_HANDLE, keepErr);

    DeleteProcThreadAttributeList(startup.lpAttributeList);
    CloseHandle(consoleReads);
    CloseHandle(consoleWrites);

    if (!ok) {
        DropConsole drop = dropConsole();
        if (drop) drop(console);
        CloseHandle(weWrite);
        CloseHandle(weRead);
        return false;
    }
    CloseHandle(made.hThread);

    held_->toChild = weWrite;
    held_->fromChild = weRead;
    held_->child = made.hProcess;
    held_->console = console;
    running_ = true;
    return true;
}

bool Process::say(const std::string& line) {
    if (!running_) return false;
    // A console is a terminal: it is reading keystrokes, and the key that ends
    // a line there is Return. A bare newline is not that key, so a child on a
    // console sat waiting for the rest of a line that had already been sent -
    // which reads as a debugger that would not start.
    std::string out = line + (held_->console ? "\r\n" : "\n");
    DWORD written = 0;
    if (!WriteFile(held_->toChild, out.data(), static_cast<DWORD>(out.size()), &written, NULL))
        return false;
    return written == out.size();
}

void Process::stop() {
    if (!running_) return;
    running_ = false;

    if (held_->toChild) { CloseHandle(held_->toChild); held_->toChild = NULL; }

    // A moment to leave of its own accord, since a debugger told to quit has
    // its own child to take with it.
    if (WaitForSingleObject(held_->child, 2000) == WAIT_TIMEOUT)
        TerminateProcess(held_->child, 1);

    // The console holds the write end itself, so while it is open a read has
    // something to wait for however dead the child is.
    if (held_->console) {
        DropConsole drop = dropConsole();
        if (drop) drop(held_->console);
        held_->console = NULL;
    }

    if (held_->fromChild) { CloseHandle(held_->fromChild); held_->fromChild = NULL; }
    CloseHandle(held_->child);
    held_->child = NULL;
}

namespace {

// Whether anything can be read without waiting. A pipe cannot be handed to
// select here, so it is asked how much it is holding.
bool waitingToBeRead(HANDLE from) {
    DWORD ready = 0;
    if (!PeekNamedPipe(from, NULL, 0, NULL, &ready, NULL)) return true;  // gone: let the read say so
    return ready > 0;
}

// Read: 1 said something, 0 said nothing in time, -1 has gone for good.
int readSome(void* from, char* into, size_t room, size_t& got, int timeoutMs) {
    HANDLE pipe = static_cast<HANDLE>(from);
    for (int waited = 0; waited < timeoutMs; waited += 10) {
        if (waitingToBeRead(pipe)) break;
        Sleep(10);
        if (waited + 10 >= timeoutMs) return 0;
    }

    DWORD read = 0;
    if (!ReadFile(pipe, into, static_cast<DWORD>(room), &read, NULL)) return -1;
    got = read;
    return read > 0 ? 1 : -1;
}

}  // namespace

#else

// Nothing to do here: lldb gives its program a pseudo-terminal of its own, and
// gdb is told to run it through stdbuf, so neither is buffered the way a
// Windows program writing down a pipe is. Saying so plainly beats a second way
// of starting a child that no caller on this machine would ever want.
bool Process::startOnConsole(const std::string&) { return false; }

bool Process::start(const std::string& command) {
    if (running_) return false;

    int toChild[2], fromChild[2];
    if (pipe(toChild) != 0) return false;
    if (pipe(fromChild) != 0) {
        close(toChild[0]);
        close(toChild[1]);
        return false;
    }

    pid_t child = fork();
    if (child < 0) {
        close(toChild[0]); close(toChild[1]);
        close(fromChild[0]); close(fromChild[1]);
        return false;
    }

    if (child == 0) {
        // The child. Its ends become its standard streams and every other
        // descriptor it inherited is closed, so that the parent closing its
        // end is something the child can actually see.
        dup2(toChild[0], STDIN_FILENO);
        dup2(fromChild[1], STDOUT_FILENO);
        dup2(fromChild[1], STDERR_FILENO);   // one console, so one stream
        close(toChild[0]); close(toChild[1]);
        close(fromChild[0]); close(fromChild[1]);

        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(0));
        _exit(127);   // only reached when the shell itself could not be run
    }

    close(toChild[0]);
    close(fromChild[1]);

    held_->toChild = toChild[1];
    held_->fromChild = fromChild[0];
    held_->child = child;
    running_ = true;
    return true;
}

bool Process::say(const std::string& line) {
    if (!running_) return false;

    std::string out = line + "\n";
    size_t written = 0;
    while (written < out.size()) {
        ssize_t went = write(held_->toChild, out.data() + written, out.size() - written);
        if (went <= 0) {
            if (went < 0 && errno == EINTR) continue;
            return false;
        }
        written += static_cast<size_t>(went);
    }
    return true;
}

void Process::stop() {
    if (!running_) return;
    running_ = false;

    // Closing its input is how a debugger is asked to leave: the same thing
    // that reaching the end of a script does to it.
    if (held_->toChild >= 0) { close(held_->toChild); held_->toChild = -1; }

    for (int waited = 0; waited < 200; ++waited) {
        int status = 0;
        pid_t done = waitpid(held_->child, &status, WNOHANG);
        if (done == held_->child || done < 0) break;
        usleep(10 * 1000);
        if (waited == 199) {
            kill(held_->child, SIGKILL);
            waitpid(held_->child, &status, 0);
        }
    }

    if (held_->fromChild >= 0) { close(held_->fromChild); held_->fromChild = -1; }
    held_->child = -1;
}

namespace {

// Read: 1 said something, 0 said nothing in time, -1 has gone for good. The
// difference is the whole point of the timeout: a debugger that is thinking is
// left alone, and one that has died is noticed.
int readSome(int from, char* into, size_t room, size_t& got, int timeoutMs) {
    for (;;) {
        struct pollfd waiting;
        waiting.fd = from;
        waiting.events = POLLIN;
        waiting.revents = 0;

        int ready = poll(&waiting, 1, timeoutMs);
        if (ready == 0) return 0;                       // quiet, but still there
        if (ready < 0) {
            if (errno == EINTR) continue;
            return -1;
        }

        ssize_t read = ::read(from, into, room);
        if (read > 0) { got = static_cast<size_t>(read); return 1; }
        if (read == 0) return -1;                       // the far end closed
        if (errno == EINTR) continue;
        return -1;
    }
}

}  // namespace

#endif

std::string Process::readUntil(const std::string& marker, bool* found, int timeoutMs) {
    if (found) *found = false;
    if (marker.empty() || timeoutMs <= 0) return std::string();

    for (;;) {
        size_t at = pending_.find(marker);
        if (at != std::string::npos) {
            std::string answer = pending_.substr(0, at);
            pending_.erase(0, at + marker.size());
            if (found) *found = true;
            return answer;
        }
        if (!running_) break;

        char chunk[1024];
        size_t got = 0;
        int said = readSome(held_->fromChild, chunk, sizeof chunk, got, timeoutMs);
        if (said < 0) {
            running_ = false;   // gone, and there is nothing more coming
            break;
        }
        if (said == 0) break;   // quiet: still there, and the caller's to judge
        pending_.append(chunk, got);
    }

    std::string rest = pending_;
    pending_.clear();
    return rest;
}

}  // namespace editor
