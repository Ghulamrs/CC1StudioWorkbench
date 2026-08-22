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

class Session {
public:
    // Starts the program with the session armed. `executable` is what the
    // editor built with --debug; a release build has no code for any of this
    // and will simply run.
    bool start(const std::string& executable);
    bool running() const { return channel_.running(); }
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
