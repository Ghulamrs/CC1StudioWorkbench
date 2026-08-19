#ifndef EDITOR_TERMINAL_H
#define EDITOR_TERMINAL_H

#ifndef _WIN32
#include <termios.h>
#endif

#include <string>

namespace editor {

// Keys that arrive as an escape sequence rather than as a character. The
// numbers start above 255 so that an ordinary byte and a named key can both
// travel through the same int without either being mistaken for the other.
enum Key {
    KEY_NONE      = -1,   // nothing arrived before the read timed out
    KEY_BACKSPACE = 127,
    KEY_ARROW_LEFT = 1000,
    KEY_ARROW_RIGHT,
    KEY_ARROW_UP,
    KEY_ARROW_DOWN,
    KEY_DELETE,
    KEY_HOME,
    KEY_END,
    KEY_PAGE_UP,
    KEY_PAGE_DOWN,
    KEY_F1,
    KEY_F2,
    KEY_F3,
    KEY_F5,
    KEY_F10,

    // The same keys with shift held, which is how a selection is made. A
    // terminal sends these as the ordinary sequence with a modifier in it.
    KEY_SHIFT_LEFT,
    KEY_SHIFT_RIGHT,
    KEY_SHIFT_UP,
    KEY_SHIFT_DOWN,
    KEY_SHIFT_HOME,
    KEY_SHIFT_END,
    KEY_SHIFT_PAGE_UP,
    KEY_SHIFT_PAGE_DOWN
};

// Whether a key is the shifted form of a movement, and the plain key it shifts.
bool isShiftedMove(int key);
int unshifted(int key);

// What the terminal sends for a key held down with control. 'q' and 'Q' both
// come through as 17, which is why the editor's bindings are case-blind.
constexpr int ctrl(char c) { return c & 0x1f; }

// Puts the terminal into raw mode for as long as it exists, and puts it back
// on the way out. Everything that touches the real terminal lives here, so the
// rest of the editor can be read without knowing about termios at all.
class Terminal {
public:
    Terminal();
    ~Terminal();

    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    // The usable screen, in character cells. Falls back to 24x80 if the
    // terminal will not say - an editor drawing the wrong size is better than
    // an editor that refuses to start.
    void size(int& rows, int& cols) const;

    // Blocks for a tenth of a second, then gives up and returns KEY_NONE. The
    // wait is what lets an escape be told apart from an arrow key: an arrow
    // sends its remaining bytes immediately, a pressed Escape sends nothing.
    //
    // Implemented once, in terminal_common.cpp, for both machines. Only
    // readByte below differs between them: Windows 10 and later send the same
    // escape sequences as a Unix terminal once the console is asked to, so
    // decoding them twice would only be two things to keep in step.
    int readKey() const;

    static void write(const std::string& s);

    // True once input has run out. Only ever true when the editor is being
    // driven from a file or a pipe rather than by a person - a terminal never
    // closes - and it is what lets a scripted run stop instead of spinning.
    bool eof() const { return eof_; }

private:
    // One byte, or false if none arrived before the timeout.
    bool readByte(char& c) const;

#ifdef _WIN32
    // HANDLE and DWORD, spelled so that windows.h stays out of this header.
    void* in_;
    void* out_;
    unsigned long inMode_;
    unsigned long outMode_;
#else
    struct termios original_;
#endif
    bool raw_;
    mutable bool eof_;
};

}  // namespace editor

#endif
