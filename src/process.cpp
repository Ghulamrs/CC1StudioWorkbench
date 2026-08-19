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

struct Process::Held {
    HANDLE toChild;
    HANDLE fromChild;
    HANDLE child;

    Held() : toChild(NULL), fromChild(NULL), child(NULL) {}
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
    std::string line = "cmd /c " + command;
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

bool Process::say(const std::string& line) {
    if (!running_) return false;
    std::string out = line + "\n";
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
