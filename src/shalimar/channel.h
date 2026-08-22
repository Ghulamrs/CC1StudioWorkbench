// A child with its three streams kept apart.
//
// Not editor::Process, and the difference is the whole reason this exists.
// That one joins a child's error output to its ordinary output on purpose:
// a debugger says useful things on both and the editor shows one console.
// A Shalimar program under a session is the opposite case - the session talks
// on standard error and the program prints on standard output, and joining
// them would put a '#stop' in the middle of a line the program was half way
// through writing.
//
// So the two are read separately here. Everything else about it is smaller
// than Process: there is no console variant, because the child is not a
// debugger with a child of its own, and no marker discipline, because a
// protocol line is a line and arrives whole.
#pragma once

#include <string>

namespace shalimar {

class Channel {
public:
    Channel();
    ~Channel();

    // `environment` is added to the child's, as NAME=VALUE. One entry is all
    // this needs and one is all it takes.
    bool start(const std::string& command, const std::string& environment);
    bool running() const { return running_; }

    // A line to the child, with the newline added. False when it has gone.
    bool say(const std::string& line);

    // The next line the child wrote on standard error, waiting up to
    // `timeoutMs` for one. Empty when none came, which is a child that died
    // or one that is thinking for too long - `alive` tells them apart.
    std::string hear(int timeoutMs, bool* alive = 0);

    // Whatever the child has printed on standard output since this was last
    // asked. Never waits: the program's printing is not what anything here is
    // waiting for.
    std::string printed();

    void stop();

private:
    Channel(const Channel&);
    Channel& operator=(const Channel&);

    // A pipe is a small int on one machine and a handle on the other, and a
    // child is a pid or a handle. None of that belongs in a header.
    struct Held;
    Held* held_;
    bool running_;
    std::string pendingError_;    // read from the child but not yet a whole line
    std::string pendingOutput_;
};

}  // namespace shalimar
