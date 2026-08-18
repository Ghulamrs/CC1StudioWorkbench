// A small smart class, and something that exercises it.
//
// This is the file to open when trying the editor out. It is C++, which is the
// point: cc1 compiles C, so this one is built with the MSVC toolchain - Ctrl-K
// switches to cl, Ctrl-B builds, and the assembly tab fills with cl's listing.
//
// The class is deliberately plain. It owns one thing, hands it back when it
// goes out of scope, moves rather than copies, and has no template machinery
// beyond the one parameter it needs. The simple version first.

#include <cstdio>

namespace demo {

// Owns a single object and deletes it exactly once. Copying is refused rather
// than defined: two owners of one object is the bug this class exists to make
// impossible, so the compiler should say so rather than the destructor.
template <typename T>
class Owned {
public:
    Owned() : held_(0) {}
    explicit Owned(T* raw) : held_(raw) {}

    ~Owned() { delete held_; }

    Owned(const Owned&) = delete;
    Owned& operator=(const Owned&) = delete;

    // Moving is how ownership travels. The one moved from is left empty, which
    // is what stops its destructor from deleting what it no longer owns.
    Owned(Owned&& other) : held_(other.held_) { other.held_ = 0; }

    Owned& operator=(Owned&& other) {
        if (this != &other) {
            delete held_;
            held_ = other.held_;
            other.held_ = 0;
        }
        return *this;
    }

    T* get() const { return held_; }
    T& operator*() const { return *held_; }
    T* operator->() const { return held_; }
    bool has() const { return held_ != 0; }

    // Gives the object up without deleting it - for handing over to something
    // that wants to own it instead.
    T* release() {
        T* was = held_;
        held_ = 0;
        return was;
    }

    void reset(T* raw = 0) {
        if (raw == held_) return;
        delete held_;
        held_ = raw;
    }

private:
    T* held_;
};

// Something worth owning, which says when it is made and when it is let go, so
// that running this shows the class doing its job.
class Counter {
public:
    explicit Counter(int start) : value_(start) {
        std::printf("Counter(%d) made\n", value_);
    }

    ~Counter() { std::printf("Counter(%d) let go\n", value_); }

    void step() { ++value_; }
    int value() const { return value_; }

private:
    int value_;
};

}  // namespace demo

int main(void) {
    demo::Owned<demo::Counter> first(new demo::Counter(1));
    first->step();
    first->step();
    std::printf("first is %d\n", first->value());

    // Ownership moves; first is empty afterwards and deletes nothing.
    demo::Owned<demo::Counter> second(static_cast<demo::Owned<demo::Counter>&&>(first));
    std::printf("first holds something: %s\n", first.has() ? "yes" : "no");
    std::printf("second is %d\n", second->value());

    // Replacing what is held lets the old one go there and then.
    second.reset(new demo::Counter(100));
    std::printf("second is now %d\n", second->value());

    return 0;
}
