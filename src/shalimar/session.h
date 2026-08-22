// Driving a Shalimar program that can stop.
//
// The other three debuggers this editor speaks to are programs of their own -
// gdb, lldb, cdb - and src/debugger.cpp holds that conversation. This is not
// one of those and does not touch it. A Shalimar program stops itself: the
// compiler already emits shm_line(unit, line) before every statement so that
// a runtime error can name where it happened, and a debug build offers that
// same position to a session inside the program. There is no debug format
// here, nothing to read a .pdb or a DWARF unit, and nothing to install.
//
// What this class does is talk to it: commands down its standard input,
// answers up its standard error, and the program's own printing left alone on
// standard output.
//
// The vocabulary is the editor's - Stop and StackFrame come from debugger.h -
// because "where the program is" is one idea and the editor draws it one way.
// Borrowing the words is not the same as sharing the machinery.
#pragma once

#include "../debugger.h"

#include "channel.h"

#include <map>
#include <string>
#include <vector>

namespace shalimar {

// What the editor says when something is asked of a Shalimar session that only
// a debugger could answer. The words live here rather than in either front end
// because both have to refuse in the same voice: the terminal half wrote them
// out and the window had no way to ask for them, which is exactly how two
// editors come to say two different things about one fact.
//
// None of them is an apology for a gap. A Shalimar program reports where it is
// and how deep it is, and that is the whole of what it knows about itself -
// see README.md.

// Where the variables would be, and why there are none anywhere.
const char* saysWhereOnly();

// Why there is nothing above the frame it is standing in.
const char* saysHowDeepOnly();

// Why a release build cannot be stopped, which is not the reason a release C
// build cannot: there is no -g here to have been left out. The key that
// changes it is each front end's own, so it is not part of this.
const char* releaseHasNoSession();

// A program that started and never said it was ready.
const char* didNotArm();

class Session {
public:
    // Starts the program with the session armed. `executable` is what the
    // editor built with --debug; a release build has no code for any of this
    // and will simply run.
    bool start(const std::string& executable);
    bool running() const { return channel_.running(); }

    // Whether the stop in hand is this session's - true from the moment it
    // starts until it is stopped, and so still true for the stop that says the
    // program ended. running() has gone false by then: the channel closes when
    // the program says #exit, which is what makes this a different question
    // rather than another spelling of the same one.
    //
    // Both front ends ask it about one thing: whose printing the console is
    // being handed. A program's last line arrives with its exit, and taking it
    // for a debugger's would run it through a filter built for gdb and lldb -
    // which drops a blank line, and anything shaped like a prompt.
    bool ownsTheStop() const { return channel_.running() || exited_; }

    void stop();

    // Before running, or while stopped. The file is matched against the names
    // the program said at startup - which is the only way to know them, since
    // a file's number depends on which files the compiler was given.
    bool breakAt(const std::string& file, size_t line);
    bool clearBreakpoints();

    editor::Stop run();
    editor::Stop resume();
    editor::Stop stepOver();
    editor::Stop stepInto();
    editor::Stop stepOut();

    // One frame: where the program is standing. The depth is known and the
    // names of the callers are not, which is honest rather than empty - a
    // caller's name would need a table the compiler does not emit yet.
    std::vector<editor::StackFrame> frames();

    // Whatever the program has printed since this was last asked.
    std::string printed() { return channel_.printed(); }

private:
    Channel channel_;
    // Unit numbers against file names, as the program itself reported them.
    std::map<int, std::string> files_;
    std::vector<std::pair<std::string, size_t> > wanted_;
    std::string program_;
    int unit_ = 0;
    size_t line_ = 0;
    int depth_ = 0;
    bool exited_ = false;
    int status_ = 0;

    // Reads until the program says where it is or that it has ended.
    editor::Stop listen(int timeoutMs);
    editor::Stop after(const std::string& command);
    int unitFor(const std::string& file) const;
    std::string fileFor(int unit) const;
    void sendWanted();
};

}  // namespace shalimar
