#include "terminal.h"

#include <sys/ioctl.h>
#include <unistd.h>

#include <cstdlib>

namespace editor {

Terminal::Terminal() : original_(), raw_(false), eof_(false) {
    if (!isatty(STDIN_FILENO)) return;
    if (tcgetattr(STDIN_FILENO, &original_) == -1) return;

    struct termios raw = original_;

    // Off: translating carriage return to newline, and software flow control -
    // otherwise Ctrl-M arrives as Ctrl-J, and Ctrl-S freezes the screen
    // instead of saving the file.
    raw.c_iflag &= ~(unsigned long)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    // Off: turning a newline into a carriage return and newline on the way
    // out. The editor decides where the cursor goes; nothing else should.
    raw.c_oflag &= ~(unsigned long)(OPOST);
    raw.c_cflag |= (unsigned long)(CS8);
    // Off: echo, line buffering, signals, and literal-next. Keys reach the
    // program one at a time and unprinted.
    raw.c_lflag &= ~(unsigned long)(ECHO | ICANON | IEXTEN | ISIG);

    // Return with whatever has arrived after a tenth of a second, even if that
    // is nothing. readKey() depends on this timeout to recognise a bare Escape.
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) return;
    raw_ = true;
}

Terminal::~Terminal() {
    if (raw_) tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_);
}

void Terminal::size(int& rows, int& cols) const {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        rows = 24;
        cols = 80;
        return;
    }
    rows = ws.ws_row;
    cols = ws.ws_col;
}

void Terminal::write(const std::string& s) {
    // A write to a terminal can stop short. Finish it rather than lose the
    // tail of a screen refresh.
    size_t sent = 0;
    while (sent < s.size()) {
        ssize_t n = ::write(STDOUT_FILENO, s.data() + sent, s.size() - sent);
        if (n <= 0) return;
        sent += static_cast<size_t>(n);
    }
}

bool Terminal::readByte(char& c) const {
    ssize_t n = ::read(STDIN_FILENO, &c, 1);
    // In raw mode a zero means the tenth-of-a-second timeout expired and the
    // person has not typed yet. Outside it, there is no timeout to expire, so
    // a zero is the end of the input.
    if (n == 0 && !raw_) eof_ = true;
    return n == 1;
}

}  // namespace editor
