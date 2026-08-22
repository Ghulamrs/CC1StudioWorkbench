#include "channel.h"

#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace shalimar {

#ifdef _WIN32

struct Channel::Held {
    HANDLE toChild = 0;
    HANDLE fromError = 0;
    HANDLE fromOutput = 0;
    HANDLE process = 0;
};

namespace {

// A pipe the child inherits one end of and this keeps the other.
bool makePipe(HANDLE &readEnd, HANDLE &writeEnd, bool childReads) {
    SECURITY_ATTRIBUTES inheritable;
    inheritable.nLength = sizeof inheritable;
    inheritable.lpSecurityDescriptor = 0;
    inheritable.bInheritHandle = TRUE;
    if (!CreatePipe(&readEnd, &writeEnd, &inheritable, 0)) return false;
    // Only the end the child uses is inherited; the other would leave the
    // child holding a copy of our side and a read that never sees end of file.
    SetHandleInformation(childReads ? writeEnd : readEnd, HANDLE_FLAG_INHERIT, 0);
    return true;
}

std::string drain(HANDLE pipe) {
    std::string out;
    for (;;) {
        DWORD waiting = 0;
        if (!PeekNamedPipe(pipe, 0, 0, 0, &waiting, 0) || waiting == 0) break;
        char buffer[4096];
        DWORD got = 0;
        DWORD want = waiting < sizeof buffer ? waiting : sizeof buffer;
        if (!ReadFile(pipe, buffer, want, &got, 0) || got == 0) break;
        out.append(buffer, got);
    }
    return out;
}

}  // namespace

bool Channel::start(const std::string& command, const std::string& environment) {
    stop();
    held_ = new Held();

    HANDLE childIn = 0, childErr = 0, childOut = 0;
    if (!makePipe(childIn, held_->toChild, true)) return false;
    if (!makePipe(held_->fromError, childErr, false)) return false;
    if (!makePipe(held_->fromOutput, childOut, false)) return false;

    STARTUPINFOA startup;
    std::memset(&startup, 0, sizeof startup);
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = childIn;
    startup.hStdOutput = childOut;
    startup.hStdError = childErr;

    // The environment is this process's with one line added, which is what
    // GetEnvironmentStrings gives and CreateProcess wants: name=value pairs
    // one after another, the lot ended by a second null.
    std::vector<char> block;
    LPCH inherited = GetEnvironmentStrings();
    for (LPCH at = inherited; *at; ) {
        size_t n = std::strlen(at);
        block.insert(block.end(), at, at + n + 1);
        at += n + 1;
    }
    FreeEnvironmentStrings(inherited);
    block.insert(block.end(), environment.begin(), environment.end());
    block.push_back('\0');
    block.push_back('\0');

    std::string line = command;
    PROCESS_INFORMATION made;
    std::memset(&made, 0, sizeof made);
    BOOL ok = CreateProcessA(0, &line[0], 0, 0, TRUE, CREATE_NO_WINDOW, &block[0], 0,
                             &startup, &made);
    CloseHandle(childIn);
    CloseHandle(childErr);
    CloseHandle(childOut);
    if (!ok) { stop(); return false; }

    CloseHandle(made.hThread);
    held_->process = made.hProcess;
    running_ = true;
    return true;
}

bool Channel::say(const std::string& line) {
    if (!running_ || !held_) return false;
    std::string text = line + "\n";
    DWORD written = 0;
    return WriteFile(held_->toChild, text.c_str(), static_cast<DWORD>(text.size()),
                     &written, 0) != 0;
}

std::string Channel::hear(int timeoutMs, bool* alive) {
    if (alive) *alive = running_;
    if (!running_ || !held_) return std::string();

    for (int waited = 0; ; waited += 10) {
        size_t end = pendingError_.find('\n');
        if (end != std::string::npos) {
            std::string line = pendingError_.substr(0, end);
            pendingError_.erase(0, end + 1);
            if (!line.empty() && line[line.size() - 1] == '\r') line.resize(line.size() - 1);
            return line;
        }
        pendingOutput_ += drain(held_->fromOutput);
        pendingError_ += drain(held_->fromError);
        if (pendingError_.find('\n') != std::string::npos) continue;

        if (WaitForSingleObject(held_->process, 0) == WAIT_OBJECT_0) {
            pendingOutput_ += drain(held_->fromOutput);
            pendingError_ += drain(held_->fromError);
            if (pendingError_.find('\n') == std::string::npos) {
                running_ = false;
                if (alive) *alive = false;
                return std::string();
            }
            continue;
        }
        if (waited >= timeoutMs) return std::string();
        Sleep(10);
    }
}

std::string Channel::printed() {
    if (held_ && running_) pendingOutput_ += drain(held_->fromOutput);
    std::string out;
    out.swap(pendingOutput_);
    return out;
}

void Channel::stop() {
    if (!held_) return;
    if (held_->process) {
        TerminateProcess(held_->process, 1);
        WaitForSingleObject(held_->process, 2000);
        CloseHandle(held_->process);
    }
    if (held_->toChild) CloseHandle(held_->toChild);
    if (held_->fromError) CloseHandle(held_->fromError);
    if (held_->fromOutput) CloseHandle(held_->fromOutput);
    delete held_;
    held_ = 0;
    running_ = false;
}

#else

struct Channel::Held {
    int toChild = -1;
    int fromError = -1;
    int fromOutput = -1;
    pid_t child = -1;
};

namespace {

std::string drain(int fd) {
    std::string out;
    for (;;) {
        char buffer[4096];
        ssize_t got = ::read(fd, buffer, sizeof buffer);
        if (got > 0) { out.append(buffer, static_cast<size_t>(got)); continue; }
        break;   // nothing waiting, or the far end has gone
    }
    return out;
}

}  // namespace

bool Channel::start(const std::string& command, const std::string& environment) {
    stop();
    held_ = new Held();

    int in[2], err[2], out[2];
    if (::pipe(in) != 0 || ::pipe(err) != 0 || ::pipe(out) != 0) { stop(); return false; }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, in[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, out[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, err[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, in[1]);
    posix_spawn_file_actions_addclose(&actions, out[0]);
    posix_spawn_file_actions_addclose(&actions, err[0]);

    // Through the shell, like every other command the editor runs, so that a
    // program named with a path holding spaces is quoted the same way.
    std::string line = environment.empty() ? command
                                           : "export " + environment + "; " + command;
    const char *argv[] = {"/bin/sh", "-c", line.c_str(), 0};

    pid_t made = -1;
    int trouble = posix_spawn(&made, "/bin/sh", &actions, 0,
                              const_cast<char *const *>(argv), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(in[0]);
    ::close(out[1]);
    ::close(err[1]);
    if (trouble != 0) {
        ::close(in[1]); ::close(out[0]); ::close(err[0]);
        stop();
        return false;
    }

    // Read without blocking, so that asking for the program's printing never
    // waits for printing that is not coming.
    ::fcntl(out[0], F_SETFL, O_NONBLOCK);
    ::fcntl(err[0], F_SETFL, O_NONBLOCK);

    held_->toChild = in[1];
    held_->fromOutput = out[0];
    held_->fromError = err[0];
    held_->child = made;
    running_ = true;
    return true;
}

bool Channel::say(const std::string& line) {
    if (!running_ || !held_) return false;
    std::string text = line + "\n";
    return ::write(held_->toChild, text.c_str(), text.size()) == static_cast<ssize_t>(text.size());
}

std::string Channel::hear(int timeoutMs, bool* alive) {
    if (alive) *alive = running_;
    if (!running_ || !held_) return std::string();

    for (int waited = 0; ; waited += 10) {
        size_t end = pendingError_.find('\n');
        if (end != std::string::npos) {
            std::string line = pendingError_.substr(0, end);
            pendingError_.erase(0, end + 1);
            if (!line.empty() && line[line.size() - 1] == '\r') line.resize(line.size() - 1);
            return line;
        }
        pendingOutput_ += drain(held_->fromOutput);
        pendingError_ += drain(held_->fromError);
        if (pendingError_.find('\n') != std::string::npos) continue;

        int state = 0;
        if (::waitpid(held_->child, &state, WNOHANG) == held_->child) {
            pendingOutput_ += drain(held_->fromOutput);
            pendingError_ += drain(held_->fromError);
            held_->child = -1;
            if (pendingError_.find('\n') == std::string::npos) {
                running_ = false;
                if (alive) *alive = false;
                return std::string();
            }
            continue;
        }
        if (waited >= timeoutMs) return std::string();

        struct pollfd watching;
        watching.fd = held_->fromError;
        watching.events = POLLIN;
        ::poll(&watching, 1, 10);
    }
}

std::string Channel::printed() {
    if (held_ && held_->fromOutput >= 0) pendingOutput_ += drain(held_->fromOutput);
    std::string out;
    out.swap(pendingOutput_);
    return out;
}

void Channel::stop() {
    if (!held_) return;
    if (held_->child > 0) {
        ::kill(held_->child, SIGKILL);
        int state = 0;
        ::waitpid(held_->child, &state, 0);
    }
    if (held_->toChild >= 0) ::close(held_->toChild);
    if (held_->fromError >= 0) ::close(held_->fromError);
    if (held_->fromOutput >= 0) ::close(held_->fromOutput);
    delete held_;
    held_ = 0;
    running_ = false;
}

#endif

Channel::Channel() : held_(0), running_(false) {}
Channel::~Channel() { stop(); }

}  // namespace shalimar
