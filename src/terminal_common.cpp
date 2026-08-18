// The key decoding, shared by both machines. A terminal on either sends the
// same bytes: an ordinary key as itself, and everything else as an escape
// sequence. Only the getting of a byte is platform work, and that is readByte.
//
// The sequences all have one shape - ESC [ parameters final - so they are read
// that way rather than as a list of special cases. It is what lets shift+arrow
// be understood: it is the arrow's own sequence with a modifier parameter in
// it, and nothing else about it is new.

#include "terminal.h"

#include <cstdlib>

namespace editor {

namespace {

// The modifier parameter a terminal sends: 1 plus a bitmask, so shift is 2,
// control is 5, and control with shift is 6.
bool hasShift(int modifier) {
    if (modifier < 2) return false;
    return ((modifier - 1) & 1) != 0;
}

int shiftedForm(int key) {
    switch (key) {
        case KEY_ARROW_LEFT:  return KEY_SHIFT_LEFT;
        case KEY_ARROW_RIGHT: return KEY_SHIFT_RIGHT;
        case KEY_ARROW_UP:    return KEY_SHIFT_UP;
        case KEY_ARROW_DOWN:  return KEY_SHIFT_DOWN;
        case KEY_HOME:        return KEY_SHIFT_HOME;
        case KEY_END:         return KEY_SHIFT_END;
        case KEY_PAGE_UP:     return KEY_SHIFT_PAGE_UP;
        case KEY_PAGE_DOWN:   return KEY_SHIFT_PAGE_DOWN;
        default:              return key;
    }
}

// '1;2' is two parameters. Missing ones are 1, which is what a terminal means
// by leaving them out.
void parameters(const std::string& text, int& first, int& second) {
    first = 1;
    second = 1;
    if (text.empty()) return;

    size_t semi = text.find(';');
    first = std::atoi(text.c_str());
    if (first == 0) first = 1;
    if (semi != std::string::npos) {
        second = std::atoi(text.c_str() + semi + 1);
        if (second == 0) second = 1;
    }
}

int fromLetter(char letter) {
    switch (letter) {
        case 'A': return KEY_ARROW_UP;
        case 'B': return KEY_ARROW_DOWN;
        case 'C': return KEY_ARROW_RIGHT;
        case 'D': return KEY_ARROW_LEFT;
        case 'H': return KEY_HOME;
        case 'F': return KEY_END;
        case 'P': return KEY_F1;
        case 'Q': return KEY_F2;
        case 'R': return KEY_F3;
        default:  return '\x1b';
    }
}

int fromNumber(int number) {
    switch (number) {
        case 1:  return KEY_HOME;
        case 3:  return KEY_DELETE;
        case 4:  return KEY_END;
        case 5:  return KEY_PAGE_UP;
        case 6:  return KEY_PAGE_DOWN;
        case 7:  return KEY_HOME;
        case 8:  return KEY_END;
        case 11: return KEY_F1;
        case 12: return KEY_F2;
        case 13: return KEY_F3;
        case 21: return KEY_F10;
        default: return '\x1b';
    }
}

}  // namespace

bool isShiftedMove(int key) {
    return key >= KEY_SHIFT_LEFT && key <= KEY_SHIFT_PAGE_DOWN;
}

int unshifted(int key) {
    switch (key) {
        case KEY_SHIFT_LEFT:      return KEY_ARROW_LEFT;
        case KEY_SHIFT_RIGHT:     return KEY_ARROW_RIGHT;
        case KEY_SHIFT_UP:        return KEY_ARROW_UP;
        case KEY_SHIFT_DOWN:      return KEY_ARROW_DOWN;
        case KEY_SHIFT_HOME:      return KEY_HOME;
        case KEY_SHIFT_END:       return KEY_END;
        case KEY_SHIFT_PAGE_UP:   return KEY_PAGE_UP;
        case KEY_SHIFT_PAGE_DOWN: return KEY_PAGE_DOWN;
        default:                  return key;
    }
}

int Terminal::readKey() const {
    char c = 0;
    if (!readByte(c)) return KEY_NONE;
    if (c != '\x1b') return static_cast<unsigned char>(c);

    // An Escape with nothing behind it is the Escape key. An Escape with more
    // bytes behind it is the terminal spelling out a key that has no character.
    char next = 0;
    if (!readByte(next)) return '\x1b';

    // The other spelling of the function keys, used in application-keypad mode.
    if (next == 'O') {
        char letter = 0;
        if (!readByte(letter)) return '\x1b';
        return fromLetter(letter);
    }

    if (next != '[') return '\x1b';

    // ESC [ parameters final. The parameters are digits and semicolons; the
    // first byte that is neither ends the sequence and says what it was.
    std::string params;
    char final = 0;
    for (int i = 0; i < 16; ++i) {
        char b = 0;
        if (!readByte(b)) return '\x1b';
        if ((b >= '0' && b <= '9') || b == ';') {
            params += b;
            continue;
        }
        final = b;
        break;
    }
    if (final == 0) return '\x1b';

    int first = 1, second = 1;
    parameters(params, first, second);

    int key;
    if (final == '~') {
        key = fromNumber(first);
    } else {
        key = fromLetter(final);
    }
    if (key == '\x1b') return '\x1b';

    return hasShift(second) ? shiftedForm(key) : key;
}

}  // namespace editor
