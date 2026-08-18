// The key decoding, shared by both machines. A terminal on either sends the
// same bytes: an ordinary key as itself, and everything else as an escape
// sequence. Only the getting of a byte is platform work, and that is readByte.

#include "terminal.h"

namespace editor {

int Terminal::readKey() const {
    char c = 0;
    if (!readByte(c)) return KEY_NONE;
    if (c != '\x1b') return static_cast<unsigned char>(c);

    // An Escape with nothing behind it is the Escape key. An Escape with more
    // bytes behind it is the terminal spelling out a key that has no character.
    char seq[3];
    if (!readByte(seq[0])) return '\x1b';
    if (!readByte(seq[1])) return '\x1b';

    if (seq[0] == '[') {
        if (seq[1] >= '0' && seq[1] <= '9') {
            if (!readByte(seq[2])) return '\x1b';
            // F10 arrives as '[21~' - two digits, so a third byte is needed
            // before the tilde. Read it here rather than in the one-digit path.
            if (seq[1] == '1' || seq[1] == '2') {
                if (seq[2] >= '0' && seq[2] <= '9') {
                    char tilde = 0;
                    if (!readByte(tilde) || tilde != '~') return '\x1b';
                    if (seq[1] == '2' && seq[2] == '1') return KEY_F10;
                    if (seq[1] == '1' && seq[2] == '1') return KEY_F1;
                    if (seq[1] == '1' && seq[2] == '2') return KEY_F2;
                    if (seq[1] == '1' && seq[2] == '3') return KEY_F3;
                    return '\x1b';
                }
            }
            if (seq[2] == '~') {
                switch (seq[1]) {
                    case '1': return KEY_HOME;
                    case '3': return KEY_DELETE;
                    case '4': return KEY_END;
                    case '5': return KEY_PAGE_UP;
                    case '6': return KEY_PAGE_DOWN;
                    case '7': return KEY_HOME;
                    case '8': return KEY_END;
                    default:  return '\x1b';
                }
            }
            return '\x1b';
        }
        switch (seq[1]) {
            case 'A': return KEY_ARROW_UP;
            case 'B': return KEY_ARROW_DOWN;
            case 'C': return KEY_ARROW_RIGHT;
            case 'D': return KEY_ARROW_LEFT;
            case 'H': return KEY_HOME;
            case 'F': return KEY_END;
            default:  return '\x1b';
        }
    }

    // Terminals in application-keypad mode send Home and End this way instead.
    if (seq[0] == 'O') {
        switch (seq[1]) {
            case 'H': return KEY_HOME;
            case 'F': return KEY_END;
            case 'P': return KEY_F1;   // the other spelling of the F keys
            case 'Q': return KEY_F2;
            case 'R': return KEY_F3;
            default:  return '\x1b';
        }
    }

    return '\x1b';
}

}  // namespace editor
