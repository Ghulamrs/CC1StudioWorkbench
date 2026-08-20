// The Windows half of the terminal. It is short for one reason: Windows 10 and
// later speak the same escape sequences as a Unix terminal once the console is
// asked to, so the drawing, the key decoding and the status bar are the shared
// code and only the raw-mode switch is different.
//
//   ENABLE_VIRTUAL_TERMINAL_PROCESSING - the console reads the escape
//     sequences the editor writes instead of printing them.
//   ENABLE_VIRTUAL_TERMINAL_INPUT      - the console sends arrow keys and the
//     rest as those same sequences instead of as its own key records.

#include "terminal.h"

#include <windows.h>

namespace editor {

Terminal::Terminal()
    : in_(0), out_(0), inMode_(0), outMode_(0), codePage_(0), raw_(false), eof_(false) {
    // The screen is written in UTF-8 - the lines the panes are drawn with are
    // three bytes each, and so is anything in a file that is not ASCII. A
    // console left on the machine's own code page shows those bytes as
    // whatever that page has in those places, which is how a box turns into
    // Latin-1 rubbish. The page it was on is put back on the way out.
    codePage_ = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);

    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hIn == INVALID_HANDLE_VALUE || hOut == INVALID_HANDLE_VALUE) return;

    DWORD inMode = 0, outMode = 0;
    if (!GetConsoleMode(hIn, &inMode)) return;   // not a console: a pipe, say
    if (!GetConsoleMode(hOut, &outMode)) return;

    in_ = hIn;
    out_ = hOut;
    inMode_ = inMode;
    outMode_ = outMode;

    DWORD wantOut = outMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT;
    if (!SetConsoleMode(hOut, wantOut)) return;  // before Windows 10, and rightly refused

    // Off: echo, waiting for a whole line, Ctrl-C as a signal, and quick-edit -
    // which would otherwise let a stray click freeze the screen mid-build.
    // EXTENDED_FLAGS has to be set for QUICK_EDIT to be cleared at all.
    DWORD wantIn = inMode;
    wantIn &= ~(DWORD)(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT |
                       ENABLE_MOUSE_INPUT | ENABLE_QUICK_EDIT_MODE);
    wantIn |= ENABLE_EXTENDED_FLAGS | ENABLE_VIRTUAL_TERMINAL_INPUT;
    if (!SetConsoleMode(hIn, wantIn)) {
        SetConsoleMode(hOut, outMode);
        return;
    }

    raw_ = true;
}

Terminal::~Terminal() {
    if (codePage_ != 0) SetConsoleOutputCP(codePage_);
    if (!raw_) return;
    SetConsoleMode((HANDLE)in_, inMode_);
    SetConsoleMode((HANDLE)out_, outMode_);
}

void Terminal::size(int& rows, int& cols) const {
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!out_ || !GetConsoleScreenBufferInfo((HANDLE)out_, &info)) {
        rows = 24;
        cols = 80;
        return;
    }
    // The window, not the buffer. The buffer is usually far taller, and drawing
    // to its height would scroll everything the editor just wrote off the top.
    cols = info.srWindow.Right - info.srWindow.Left + 1;
    rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    if (cols <= 0) cols = 80;
    if (rows <= 0) rows = 24;
}

void Terminal::write(const std::string& s) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;

    size_t sent = 0;
    while (sent < s.size()) {
        DWORD wrote = 0;
        if (!WriteFile(hOut, s.data() + sent, (DWORD)(s.size() - sent), &wrote, NULL))
            return;
        if (wrote == 0) return;
        sent += wrote;
    }
}

bool Terminal::readByte(char& c) const {
    HANDLE hIn = in_ ? (HANDLE)in_ : GetStdHandle(STD_INPUT_HANDLE);
    if (hIn == INVALID_HANDLE_VALUE) return false;

    // A tenth of a second, matching the VTIME the other machine is set to. It
    // is what lets a pressed Escape be told from the start of an arrow key.
    if (WaitForSingleObject(hIn, 100) != WAIT_OBJECT_0) return false;

    if (raw_) {
        // A signalled console handle is not always a character: a resized
        // window and a released key both wake it, and ReadFile would sit there
        // waiting for something it will never be given. Look first, and throw
        // away anything that is not a key going down with a character on it.
        INPUT_RECORD record;
        DWORD seen = 0;
        if (!PeekConsoleInput(hIn, &record, 1, &seen) || seen == 0) return false;
        if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown ||
            record.Event.KeyEvent.uChar.AsciiChar == 0) {
            ReadConsoleInput(hIn, &record, 1, &seen);
            return false;
        }
    }

    DWORD got = 0;
    if (!ReadFile(hIn, &c, 1, &got, NULL)) {
        if (!raw_) eof_ = true;   // a pipe that has been closed
        return false;
    }
    if (got == 0 && !raw_) eof_ = true;
    return got == 1;
}

}  // namespace editor
