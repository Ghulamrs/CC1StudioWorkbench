#ifndef EDITOR_PROCESS_H
#define EDITOR_PROCESS_H

#include <string>

namespace editor {

// A child process this one can talk to, rather than only listen to.
//
// Everything else here runs a program with popen, which is one pipe and one
// direction: say the whole command up front, read until it ends. A debugger is
// not that shape. It is a conversation - set a breakpoint, run, ask where it
// stopped, ask what the variables are - and each answer decides the next
// question. So this is two pipes and a live child.
//
// The child is started through the shell, like every other command here, so
// that a program named with a path holding spaces is quoted the same way it is
// everywhere else.
//
// Its error output is joined to its ordinary output, because a debugger says
// useful things on both and the editor shows one console.
class Process {
public:
    Process();
    ~Process();

    // True when the child started. Nothing else here does anything useful
    // until it has.
    bool start(const std::string& command);
    bool running() const { return running_; }

    // A line to the child, with the newline added. False when the child has
    // gone - which is how a debugger that quit is noticed.
    bool say(const std::string& line);

    // Reads until `marker` has been seen, and returns everything before it.
    // Waiting on a prompt is not enough: the program being debugged writes
    // down the same pipe, and its output can land in the middle of one. A
    // marker the editor asked for and nothing else would print is the only
    // thing that reliably means "the answer is complete".
    //
    // `found` says whether it was, so that a debugger that died mid-answer is
    // told apart from one that answered nothing.
    //
    // It gives up after `timeoutMs` and says it did not find it, rather than
    // waiting for a child that is never going to answer. That is not a
    // hypothetical: a program whose output is buffered until it exits says
    // nothing at all until it does, and an editor waiting on one is an editor
    // that has hung with no way back. Callers that resume a program under a
    // debugger should allow longer than callers that ask it a question.
    std::string readUntil(const std::string& marker, bool* found = 0,
                          int timeoutMs = 30000);

    // Asks the child to leave, and stops waiting if it will not. Called by the
    // destructor, so a Process that goes out of scope leaves nothing running.
    void stop();

private:
    Process(const Process&);
    Process& operator=(const Process&);

    // A pipe is a small int on one machine and a pointer on the other, and a
    // child is a pid or a handle. None of that belongs in a header the whole
    // editor includes, so it is behind this.
    struct Held;
    Held* held_;

    bool running_;
    std::string pending_;   // read from the child but not yet asked for
};

}  // namespace editor

#endif
