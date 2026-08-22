// Drives the editor itself, the way a person does: keystrokes in, and what
// landed on the screen and on the disk checked afterwards.
//
// tests/test.cpp checks the pieces that never see a terminal. This checks the
// other half - editing, laying out, the menu, and the file commands - which
// until now had only ever been tried by hand, once, and never again. One
// program for both machines rather than a shell script and a PowerShell script
// that would drift apart.
//
//   usage: session [path-to-ed1] [path-to-cc1]

#include <cstdio>
#include <cstdlib>
#include "path.h"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>


// What this harness used of <filesystem>, which is C++17 and so not available
// here: a path that can be joined with /, and five operations. It is spelled
// out rather than imported, over src/path.cpp - the same code the editor uses,
// so a test that passes has exercised the thing being shipped.
namespace file {

struct path {
    std::string text;

    path() {}
    path(const char* from) : text(editor::path::withSlashes(from)) {}
    path(const std::string& from) : text(editor::path::withSlashes(from)) {}

    std::string string() const { return text; }
    path operator/(const std::string& leaf) const {
        return path(editor::path::join(text, leaf));
    }
};

inline bool exists(const path& where) { return editor::path::exists(where.text); }
inline bool remove(const path& where) { return editor::path::remove(where.text); }
inline bool remove_all(const path& where) { return editor::path::removeTree(where.text); }
inline bool create_directories(const path& where) {
    return editor::path::makeDirectories(where.text);
}
inline path temp_directory_path() { return path(editor::path::tempDir()); }

}  // namespace file

namespace {

int checks = 0;
int failures = 0;

void check(bool ok, const std::string& what) {
    ++checks;
    if (ok) return;
    ++failures;
    std::printf("  FAIL  %s\n", what.c_str());
}

void checkEqual(const std::string& got, const std::string& want, const std::string& what) {
    ++checks;
    if (got == want) return;
    ++failures;
    std::printf("  FAIL  %s\n        got  [%s]\n        want [%s]\n", what.c_str(),
                got.c_str(), want.c_str());
}

// Keys, spelled the way the terminal sends them.
const std::string kF5 = "\x1b[15~";
const std::string kF1 = "\x1b[11~";
const std::string kF6 = "\x1b[17~";
const std::string kF7 = "\x1b[18~";
const std::string kF8 = "\x1b[19~";
const std::string kF9 = "\x1b[20~";
// Control with an arrow: the arrow's own sequence with the modifier in it.
const std::string kCtrlUp = "\x1b[1;5A";
const std::string kCtrlDown = "\x1b[1;5B";
const std::string kF4 = "\x1bOS";
const std::string kF10 = "\x1b[21~";
const std::string kDown = "\x1b[B";
const std::string kRight = "\x1b[C";
const std::string kEnter = "\r";
// Shift with an arrow is the arrow's own sequence with a modifier in it.
const std::string kShiftRight = "\x1b[1;2C";
const std::string kShiftDown = "\x1b[1;2B";
const std::string kShiftEnd = "\x1b[1;2F";
std::string ctrl(char c) { return std::string(1, static_cast<char>(c & 0x1f)); }
std::string times(const std::string& key, int n) {
    std::string out;
    for (int i = 0; i < n; ++i) out += key;
    return out;
}

std::string readFile(const file::path& path) {
    std::ifstream in(path.string().c_str(), std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void writeFile(const file::path& path, const std::string& text) {
    std::ofstream out(path.string().c_str(), std::ios::binary);
    out << text;
}

struct Screen {
    std::string raw;                  // everything the editor wrote
    std::vector<std::string> rows;    // the last screen, escape codes replayed
};

// Replays the whole session onto a grid, as the terminal it was written for
// would. It cannot be done by reading the last screen alone any more: the
// editor writes only the rows that have changed since the one before, which is
// what stops it flickering, so the last thing written is a handful of rows and
// not a screen. What is checked is therefore what a person would have been
// looking at when the editor stopped.
//
// Four things move or clear the grid - absolute positioning, carriage return,
// newline and the two erases - and everything else the editor writes is
// colours, which take no room and are stepped over.
std::vector<std::string> lastScreen(const std::string& raw) {
    std::string all = raw;
    // The editor clears the screen on its way out; the picture wanted is the
    // one it had before that.
    size_t leaving = all.rfind("\x1b[2J");
    if (leaving != std::string::npos) all = all.substr(0, leaving);

    std::vector<std::string> rows;
    size_t row = 0, col = 0;

    for (size_t i = 0; i < all.size();) {
        if (all[i] == '\x1b' && i + 1 < all.size() && all[i + 1] == '[') {
            size_t j = i + 2;
            std::string digits;
            while (j < all.size() && !((all[j] >= 'A' && all[j] <= 'Z') ||
                                       (all[j] >= 'a' && all[j] <= 'z')))
                digits += all[j++];
            char final = (j < all.size()) ? all[j] : 0;

            if (final == 'H') {
                size_t semi = digits.find(';');
                row = static_cast<size_t>(std::atol(digits.c_str()));
                row = row ? row - 1 : 0;
                col = (semi == std::string::npos)
                          ? 0
                          : static_cast<size_t>(std::atol(digits.c_str() + semi + 1)) - 1;
            } else if (final == 'J' && (digits == "2" || digits.empty())) {
                rows.clear();
                row = col = 0;
            } else if (final == 'K' && (digits.empty() || digits == "0")) {
                if (row < rows.size() && rows[row].size() > col) rows[row].resize(col);
            }
            i = (j < all.size()) ? j + 1 : all.size();
            continue;
        }

        char c = all[i++];
        if (c == '\r') { col = 0; continue; }
        if (c == '\n') { ++row; continue; }

        while (rows.size() <= row) rows.push_back(std::string());
        if (rows[row].size() <= col) rows[row].resize(col + 1, ' ');
        rows[row][col] = c;
        ++col;
    }
    return rows;
}

Screen drive(const std::string& ed1, const std::string& arguments,
             const std::string& keys, const file::path& where) {
    file::path keyFile = where / "keys.in";
    file::path outFile = where / "screen.out";
    writeFile(keyFile, keys);

    std::string command = "\"" + ed1 + "\" " + arguments + " < \"" + keyFile.string() +
                          "\" > \"" + outFile.string() + "\" 2>&1";
#ifdef _WIN32
    // cmd eats the outer pair when a command has both a quoted program and
    // quoted arguments, exactly as it does for the compiler commands.
    command = "\"" + command + "\"";
#endif
    if (std::system(command.c_str()) < 0) std::printf("  (could not run %s)\n", ed1.c_str());

    Screen screen;
    screen.raw = readFile(outFile);
    screen.rows = lastScreen(screen.raw);
    file::remove(keyFile);
    file::remove(outFile);
    return screen;
}

// The bottom line, which is where the editor says what just happened.
std::string message(const Screen& screen) {
    for (size_t i = screen.rows.size(); i-- > 0;) {
        std::string row = screen.rows[i];
        while (!row.empty() && row[row.size() - 1] == ' ') row.resize(row.size() - 1);
        if (!row.empty()) return row;
    }
    return std::string();
}

// How many rows say it. What a program printed is hard to tell apart from the
// source that printed it - both are on the screen at once - so the test that
// the output arrived is that the line is there twice.
size_t rowsSaying(const Screen& screen, const std::string& text) {
    size_t found = 0;
    for (size_t i = 0; i < screen.rows.size(); ++i)
        if (screen.rows[i].find(text) != std::string::npos) ++found;
    return found;
}

bool onScreen(const Screen& screen, const std::string& text) {
    for (size_t i = 0; i < screen.rows.size(); ++i)
        if (screen.rows[i].find(text) != std::string::npos) return true;
    return false;
}

// Anywhere in the whole session rather than on the last screen. A build shows
// the console while it runs and then moves to the assembly when it works, so
// what the console said is history by the time the editor is quit.
bool wasShown(const Screen& screen, const std::string& text) {
    return screen.raw.find(text) != std::string::npos;
}

file::path freshProject(const std::string& name) {
    file::path dir = file::temp_directory_path() / ("ed1-session-" + name);
    file::remove_all(dir);
    file::create_directories(dir / "src");
    writeFile(dir / "ed1.json",
              "{\n  \"name\": \"Trial\",\n  \"indent\": 4,\n"
              "  \"groups\": { \"Sources\": [] }\n}\n");
    return dir;
}

// ---------------------------------------------------------------------------

void editingAndLayout(const std::string& ed1) {
    std::printf("typing, and what the editor does to it\n");

    file::path dir = freshProject("typing");
    file::path file = dir / "src" / "typed.c";

    // Typed with no leading space anywhere. What comes back should be laid out.
    std::string keys = "void f(void) {\ng();\nif (x)\ny();\nswitch (n) {\ncase 1:\n"
                       "break;\n}\n}" + ctrl('s') + ctrl('q');
    drive(ed1, "\"" + file.string() + "\" --project \"" + dir.string() + "\"", keys, dir);

    checkEqual(readFile(file),
               "void f(void) {\n    g();\n    if (x)\n        y();\n    switch (n) {\n"
               "    case 1:\n        break;\n    }\n}\n",
               "a function typed flat is saved laid out");

    // Ctrl-F lays out a file that arrived with no layout of its own.
    file::path flat = dir / "src" / "flat.c";
    writeFile(flat, "int main(void)\n{\nif (x)\nreturn 1;\nreturn 0;\n}\n");
    drive(ed1, "\"" + flat.string() + "\" --project \"" + dir.string() + "\"",
          ctrl('a') + ctrl('s') + ctrl('q'), dir);
    checkEqual(readFile(flat),
               "int main(void)\n{\n    if (x)\n        return 1;\n    return 0;\n}\n",
               "Ctrl-A lays out a whole file");

    // Tabs instead of spaces, asked for on the command line.
    file::path tabbed = dir / "src" / "tabbed.c";
    writeFile(tabbed, "int f(void)\n{\nreturn 1;\n}\n");
    drive(ed1, "\"" + tabbed.string() + "\" --project \"" + dir.string() + "\" --tabs",
          ctrl('a') + ctrl('s') + ctrl('q'), dir);
    check(readFile(tabbed).find("\treturn 1;") != std::string::npos,
          "--tabs indents with a tab");

    file::remove_all(dir);
}

void colouring(const std::string& ed1) {
    std::printf("what the screen is coloured with\n");

    file::path dir = freshProject("colour");
    file::path file = dir / "src" / "colour.c";
    writeFile(file, "int main(void)\n{\n    return 0;   /* done */\n}\n");

    Screen screen = drive(ed1, "\"" + file.string() + "\" --project \"" + dir.string() + "\"",
                          ctrl('q'), dir);

    // The colours are in the bytes even though they are not in the grid.
    check(screen.raw.find("\x1b[94m") != std::string::npos, "a keyword is coloured");
    check(screen.raw.find("\x1b[36m") != std::string::npos, "a type is coloured");
    check(screen.raw.find("\x1b[90m") != std::string::npos, "a comment is coloured");
    check(onScreen(screen, "  1 int main(void)"), "and the line numbers are there");
    check(onScreen(screen, "C  "), "the status bar names the language");

    file::remove_all(dir);
}

void fileCommands(const std::string& ed1) {
    std::printf("making, renaming, regrouping and deleting\n");

    file::path dir = freshProject("files");
    std::string project = " --project \"" + dir.string() + "\"";

    // Project menu: right twice from File, then down to the item wanted.
    const std::string toProject = kF10 + times(kRight, 2);

    // New file...
    drive(ed1, project, toProject + times(kDown, 3) + kEnter + "src/made.c" + kEnter + ctrl('q'),
          dir);
    check(file::exists(dir / "src" / "made.c"), "New file makes the file");
    check(readFile(dir / "ed1.json").find("src/made.c") != std::string::npos,
          "and puts it in the project");

    // A path two directories deep is refused, and nothing is written.
    Screen deep = drive(ed1, project,
                        toProject + times(kDown, 3) + kEnter + "a/b/deep.c" + kEnter + ctrl('q'),
                        dir);
    check(!file::exists(dir / "a"), "a file two directories deep is not made");
    check(onScreen(deep, "two levels at most"), "and the rule says so on screen");

    // Rename... acts on the file being edited.
    drive(ed1, "\"" + (dir / "src" / "made.c").string() + "\"" + project,
          toProject + times(kDown, 4) + kEnter + "src/moved.c" + kEnter + ctrl('q'), dir);
    check(!file::exists(dir / "src" / "made.c"), "Rename takes the old name away");
    check(file::exists(dir / "src" / "moved.c"), "and puts the new one there");
    check(readFile(dir / "ed1.json").find("src/moved.c") != std::string::npos,
          "and the project follows it");

    // Move to group... changes the project and nothing on disk.
    drive(ed1, "\"" + (dir / "src" / "moved.c").string() + "\"" + project,
          toProject + times(kDown, 5) + kEnter + "Extras" + kEnter + ctrl('q'), dir);
    std::string written = readFile(dir / "ed1.json");
    check(written.find("Extras") != std::string::npos, "regrouping makes the group");
    check(file::exists(dir / "src" / "moved.c"), "and leaves the file where it was");

    // Delete... only when the answer is yes.
    drive(ed1, "\"" + (dir / "src" / "moved.c").string() + "\"" + project,
          toProject + times(kDown, 6) + kEnter + "no" + kEnter + ctrl('q'), dir);
    check(file::exists(dir / "src" / "moved.c"), "Delete answered with no keeps the file");

    drive(ed1, "\"" + (dir / "src" / "moved.c").string() + "\"" + project,
          toProject + times(kDown, 6) + kEnter + "yes" + kEnter + ctrl('q'), dir);
    check(!file::exists(dir / "src" / "moved.c"), "and answered with yes deletes it");
    check(readFile(dir / "ed1.json").find("src/moved.c") == std::string::npos,
          "and takes it out of the project too");

    file::remove_all(dir);
}

void selectingAndPasting(const std::string& ed1) {
    std::printf("selecting, copying and pasting\n");

    file::path dir = freshProject("clip");
    file::path file = dir / "src" / "clip.c";
    std::string args = "\"" + file.string() + "\" --project \"" + dir.string() + "\"";

    // Select three characters, copy, go to the end of the line, paste.
    writeFile(file, "abcdef\n");
    drive(ed1, args,
          times(kShiftRight, 3) + ctrl('c') + "\x1b[F" + ctrl('v') + ctrl('s') + ctrl('q'),
          dir);
    checkEqual(readFile(file), "abcdefabc\n", "copy and paste move a selection about");

    // Cut takes it away.
    writeFile(file, "abcdef\n");
    drive(ed1, args, times(kShiftRight, 3) + ctrl('x') + ctrl('s') + ctrl('q'), dir);
    checkEqual(readFile(file), "def\n", "cut takes the selection out");

    // And puts it back where the caret goes next.
    writeFile(file, "abcdef\n");
    drive(ed1, args,
          times(kShiftRight, 3) + ctrl('x') + "\x1b[F" + ctrl('v') + ctrl('s') + ctrl('q'),
          dir);
    checkEqual(readFile(file), "defabc\n", "and paste puts it back");

    // With nothing selected, copy and cut take the whole line.
    writeFile(file, "first\nsecond\n");
    drive(ed1, args, ctrl('x') + ctrl('s') + ctrl('q'), dir);
    checkEqual(readFile(file), "second\n", "cut with no selection takes the line");

    // A selection crossing lines.
    writeFile(file, "one\ntwo\nthree\n");
    drive(ed1, args, kShiftDown + ctrl('x') + ctrl('s') + ctrl('q'), dir);
    checkEqual(readFile(file), "two\nthree\n", "a selection can cross a line ending");

    // Typing over a selection replaces it.
    writeFile(file, "abcdef\n");
    drive(ed1, args, times(kShiftRight, 3) + "X" + ctrl('s') + ctrl('q'), dir);
    checkEqual(readFile(file), "Xdef\n", "typing over a selection replaces it");

    // Backspace over a selection removes all of it.
    writeFile(file, "abcdef\n");
    drive(ed1, args, times(kShiftRight, 3) + "\x7f" + ctrl('s') + ctrl('q'), dir);
    checkEqual(readFile(file), "def\n", "and backspace removes all of it");

    // Undo puts a cut back in one step.
    writeFile(file, "abcdef\n");
    drive(ed1, args, times(kShiftRight, 3) + ctrl('x') + ctrl('z') + ctrl('s') + ctrl('q'),
          dir);
    checkEqual(readFile(file), "abcdef\n", "and undo puts a cut back");

    Screen shown = drive(ed1, args, times(kShiftRight, 3) + ctrl('q'), dir);
    check(shown.raw.find("\x1b[7m") != std::string::npos, "a selection is shown in reverse");

    file::remove_all(dir);
}

void multiByteText(const std::string& ed1) {
    std::printf("text that is not ASCII\n");

    file::path dir = freshProject("utf8");
    file::path file = dir / "src" / "urdu.c";
    // A comment in Urdu and a string with an accented letter in it.
    const std::string text =
        "/* \xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 */\nchar *s = \"caf\xc3\xa9\";\n";
    writeFile(file, text);

    std::string args = "\"" + file.string() + "\" --project \"" + dir.string() + "\"";

    // Opened and saved with nothing done to it, the bytes must be the same.
    drive(ed1, args, ctrl('s') + ctrl('q'), dir);
    checkEqual(readFile(file), text, "a file that is not ASCII survives being saved");

    // Four steps right cross three ASCII characters and one Urdu letter, which
    // is five bytes but four columns.
    Screen moved = drive(ed1, args, times(kRight, 4) + ctrl('q'), dir);
    check(onScreen(moved, "col 5"), "the caret moves by characters, not by bytes");

    // Backspace takes the whole letter, not its last byte.
    writeFile(file, text);
    drive(ed1, args, times(kRight, 4) + "\x7f" + ctrl('s') + ctrl('q'), dir);
    std::string after = readFile(file);
    check(after.find("/* \xd9\x84") != std::string::npos,
          "backspace removes a whole letter");
    check(after.size() == text.size() - 2, "which is two bytes, not one");

    file::remove_all(dir);
}

// Re-indenting what is selected, and everything when nothing is.
// Leaving with work unsaved, in a file that is not the one in front.
void leavingWithChanges(const std::string& ed1) {
    std::printf("what leaving does about unsaved work\n");

    file::path dir = freshProject("leaving");
    file::path one = dir / "src" / "one.c";
    file::path two = dir / "src" / "two.c";
    writeFile(one, "int one(void) { return 1; }\n");
    writeFile(two, "int two(void) { return 2; }\n");
    writeFile(dir / "ed1.json",
              "{\n  \"name\": \"leaving\",\n"
              "  \"groups\": { \"Sources\": [\"src/one.c\", \"src/two.c\"] }\n}\n");

    std::string common = "\"" + one.string() + "\" --project \"" + dir.string() + "\"";

    // Type into one.c, then open two.c from the pane so that the changed file
    // is the one behind. Ctrl-W moves the focus; Ctrl-P would toggle the pane.
    const std::string behind = "X" + ctrl('w') + times(kDown, 2) + kEnter;

    Screen left = drive(ed1, common, behind + ctrl('q'), dir);
    check(wasShown(left, "unsaved changes in one.c"),
          "leaving names the changed file even when another is in front");
    checkEqual(readFile(one), "int one(void) { return 1; }\n",
               "and nothing was written behind your back");

    // A file opened twice, spelled two ways, is one file. Opened by the name
    // given on the command line and then from the pane, which counts paths
    // from the project's root - the tab strip must hold one of it, and the
    // changes must still be in it.
    Screen once = drive(ed1, "\"src/one.c\" --project \"" + dir.string() + "\"",
                        "X" + ctrl('w') + kEnter + ctrl('q'), dir);
    check(rowsSaying(once, "one.c") >= 1, "the file is open");
    check(!onScreen(once, "one.c* - one.c"), "and opening it again from the pane opens no second tab");
    check(onScreen(once, "one.c*"), "with the changes still in it");

    // The one in front, which is the case that always worked.
    Screen front = drive(ed1, common, "X" + ctrl('q'), dir);
    check(wasShown(front, "unsaved changes in one.c"), "and it says so for the one in front");

    // Twice leaves anyway, which is what the message promises. Proved by what
    // comes after it: keys typed once it has gone are typed at nothing, so a
    // ZZZZ that never appears is an editor that had already left. Exiting on
    // its own would prove nothing here - a driven run ends when the keys do.
    Screen gone = drive(ed1, common, "X" + ctrl('q') + ctrl('q') + "ZZZZ", dir);
    check(!wasShown(gone, "ZZZZ"), "and pressing it twice leaves, before the next key");
    checkEqual(readFile(one), "int one(void) { return 1; }\n", "still without saving");

    file::remove_all(dir);
}

void reindenting(const std::string& ed1) {
    std::printf("re-indenting, all of it or the part that is selected\n");

    file::path dir = freshProject("reindent");
    file::path file = dir / "src" / "messy.c";
    const char* crooked =
        "int main(void)\n{\nint a = 1;\n        int b = 2;\n   if (a < b) {\n"
        "a = b;\n            }\n    int c = 3;\n  int d = 4;\n    return 0;\n}\n";
    writeFile(file, crooked);

    std::string common = "\"" + file.string() + "\" --project \"" + dir.string() + "\"";

    // Nothing selected: the whole file, as Ctrl-A has always done.
    Screen all = drive(ed1, common, ctrl('a') + ctrl('s') + ctrl('q'), dir);
    // wasShown, not onScreen: saving comes after, and the message line is
    // one line - what it says at the end is that the file was written.
    check(wasShown(all, "laid out - 11 lines"), "with nothing selected it lays the file out");
    // "  int d = 4;" laid out becomes "    int d = 4;", so the crooked spelling
    // is gone from the file - and looking for its absence has to allow for the
    // straight one containing it, which is why the whole line is matched.
    check(readFile(file).find("\n  int d = 4;") == std::string::npos,
          "and the line nobody selected is laid out too");

    // Selected: only those lines are written back, and the rest are left as
    // they were - which is the whole difference, and is checked by a line
    // outside the selection staying crooked.
    writeFile(file, crooked);
    Screen part = drive(ed1, common,
                        times(kDown, 2) + times(kShiftDown, 4) + ctrl('a') +
                            ctrl('s') + ctrl('q'),
                        dir);
    check(wasShown(part, "of the selection"), "a selection is laid out on its own");
    std::string after = readFile(file);
    check(after.find("    int a = 1;") != std::string::npos,
          "the selected lines are laid out");
    check(after.find("  int d = 4;") != std::string::npos,
          "and a line below the selection is left exactly as it was");

    file::remove_all(dir);
}

void undoing(const std::string& ed1) {
    std::printf("undo and redo, in the editor\n");

    file::path dir = freshProject("undo");
    file::path file = dir / "src" / "undo.c";
    const char* text = "int one(void) { return 1; }\n";
    std::string args = "\"" + file.string() + "\" --project \"" + dir.string() + "\"";

    // Typed, then taken back, then saved: the file should be as it started.
    writeFile(file, text);
    drive(ed1, args, "xyz" + ctrl('z') + ctrl('s') + ctrl('q'), dir);
    checkEqual(readFile(file), text, "undo takes back what was typed");

    // A run of typing is one step, so one undo removes all three letters and
    // one redo brings all three back.
    writeFile(file, text);
    drive(ed1, args, "xyz" + ctrl('z') + ctrl('y') + ctrl('s') + ctrl('q'), dir);
    check(readFile(file).compare(0, 3, "xyz") == 0, "redo puts it back");

    // Laying the file out is one step of its own.
    file::path flat = dir / "src" / "flat.c";
    const char* crooked = "int main(void)\n{\nreturn 0;\n}\n";
    writeFile(flat, crooked);
    std::string flatArgs = "\"" + flat.string() + "\" --project \"" + dir.string() + "\"";
    drive(ed1, flatArgs, ctrl('a') + ctrl('z') + ctrl('s') + ctrl('q'), dir);
    checkEqual(readFile(flat), crooked, "and undo takes a whole re-layout back");

    // So is a replace.
    writeFile(file, text);
    drive(ed1, args,
          ctrl('r') + "one" + kEnter + "two" + kEnter + ctrl('z') + ctrl('s') + ctrl('q'),
          dir);
    checkEqual(readFile(file), text, "and a replace, in one step");

    // And a newline is its own step, so undo gives back a line rather than
    // everything typed since the file was opened.
    writeFile(file, text);
    drive(ed1, args, "abc\ndef" + ctrl('z') + ctrl('z') + ctrl('s') + ctrl('q'), dir);
    check(readFile(file).find("abc") != std::string::npos,
          "two undos after typing over a newline leave the first part");
    check(readFile(file).find("def") == std::string::npos, "and remove the second");

    // The star that says 'modified' has to follow undo as well as typing.
    writeFile(file, text);
    Screen back = drive(ed1, args, "q" + ctrl('s') + ctrl('z') + ctrl('q') + ctrl('q'), dir);
    check(onScreen(back, "undo.c *"), "undoing past a save shows as modified again");

    writeFile(file, text);
    Screen forward = drive(ed1, args,
                           "q" + ctrl('s') + ctrl('z') + ctrl('y') + ctrl('q'), dir);
    check(!onScreen(forward, "undo.c *"), "and redoing back to it shows as saved");

    Screen nothing = drive(ed1, args, ctrl('z') + ctrl('q'), dir);
    check(onScreen(nothing, "nothing to undo"), "and with nothing done, it says so");

    file::remove_all(dir);
}

void findingAndReplacing(const std::string& ed1) {
    std::printf("finding and replacing, in the editor\n");

    file::path dir = freshProject("find");
    file::path file = dir / "src" / "find.c";
    const char* text =
        "int one(void) { return 1; }\n"
        "int two(void) { return 2; }\n"
        "int three(void) { return 3; }\n";
    writeFile(file, text);

    std::string args = "\"" + file.string() + "\" --project \"" + dir.string() + "\"";

    // Ctrl-F, the word, enter: the caret should land on line three.
    Screen found = drive(ed1, args, ctrl('f') + "three" + kEnter + ctrl('q'), dir);
    check(onScreen(found, "3/3"), "find moves the caret to the line it is on");
    check(onScreen(found, "three - line 3"), "and says where it went");

    Screen missing = drive(ed1, args, ctrl('f') + "absent" + kEnter + ctrl('q'), dir);
    check(onScreen(missing, "is not in this file"), "and says when it is not there");

    // An empty answer looks for nothing at all - it used to be read as the last
    // search again here, while the window read it as a cancel and said nothing.
    // Looking on is what Ctrl-G is for, in both.
    Screen nothing = drive(ed1, args,
                           ctrl('f') + "three" + kEnter + ctrl('f') + kEnter + ctrl('q'), dir);
    check(onScreen(nothing, "nothing looked for"), "an empty answer to find says so");
    check(onScreen(nothing, "3/3"), "and leaves the caret where the last find put it");

    // Ctrl-F for the first, Ctrl-G for the next: 'return' is on every line.
    Screen again = drive(ed1, args,
                         ctrl('f') + "return" + kEnter + ctrl('g') + ctrl('q'), dir);
    check(onScreen(again, "2/3"), "Ctrl-G moves on to the next one");

    // Replace, then save, and look at the file.
    drive(ed1, args, ctrl('r') + "return" + kEnter + "give" + kEnter + ctrl('s') + ctrl('q'),
          dir);
    std::string written = readFile(file);
    check(written.find("give 1") != std::string::npos, "replace changes the text");
    check(written.find("return") == std::string::npos, "everywhere it appeared");

    // And nothing is written unless it is saved.
    writeFile(file, text);
    drive(ed1, args, ctrl('r') + "return" + kEnter + "gone" + kEnter + ctrl('q') + ctrl('q'),
          dir);
    checkEqual(readFile(file), text, "and quitting without saving leaves the file alone");

    file::remove_all(dir);
}

void projectPane(const std::string& ed1) {
    std::printf("the project pane\n");

    file::path dir = freshProject("pane");
    writeFile(dir / "src" / "one.c", "int one(void) { return 1; }\n");
    writeFile(dir / "src" / "two.c", "int two(void) { return 2; }\n");
    writeFile(dir / "ed1.json",
              "{\n  \"name\": \"Panes\",\n  \"indent\": 2,\n"
              "  \"groups\": { \"First\": [\"src/one.c\"], \"Second\": [\"src/two.c\"] }\n}\n");

    Screen screen = drive(ed1, "--project \"" + dir.string() + "\"", ctrl('q'), dir);
    check(onScreen(screen, "- First"), "a group is shown");
    check(onScreen(screen, "src/one.c"), "with what is in it");
    check(onScreen(screen, "- Second"), "and so is the next one");
    check(onScreen(screen, "Panes"), "the project's name is reported");

    // Opening from the pane: focus it, walk to the file, press enter.
    Screen opened = drive(ed1, "--project \"" + dir.string() + "\"",
                          ctrl('w') + kDown + kEnter + ctrl('q'), dir);
    check(onScreen(opened, "int one(void)"), "enter in the pane opens the file");
    check(onScreen(opened, " one.c"), "and it gets a tab");

    // The project's indent setting is what the editor uses.
    file::path flat = dir / "src" / "three.c";
    writeFile(flat, "int f(void)\n{\nreturn 3;\n}\n");
    drive(ed1, "\"" + flat.string() + "\" --project \"" + dir.string() + "\"",
          ctrl('a') + ctrl('s') + ctrl('q'), dir);
    check(readFile(flat).find("\n  return 3;") != std::string::npos,
          "the project's indent of 2 is what gets used");

    file::remove_all(dir);
}

// The MSVC half. No path is needed: the editor finds Visual Studio itself, so
// on Windows this runs whether or not anyone named a compiler.
void buildingWithCl(const std::string& ed1) {
#ifndef _WIN32
    (void)ed1;   // there is no cl to find anywhere else
#else
    std::printf("building with cl\n");

    file::path dir = freshProject("cl");

    file::path good = dir / "src" / "good.c";
    writeFile(good, "int twice(int n)\n{\n    return n + n;\n}\n");
    Screen ok = drive(ed1, "\"" + good.string() + "\" --project \"" + dir.string() +
                           "\" --toolchain msvc",
                      ctrl('b') + ctrl('q'), dir);
    check(onScreen(ok, "lines of"), "cl builds C, found without a Developer prompt");

    // The one cc1 cannot take at all.
    file::path cpp = dir / "src" / "thing.cpp";
    writeFile(cpp, "class Thing {\npublic:\n    int twice(int n) { return n + n; }\n};\n"
                   "int main(void) { Thing t; return t.twice(2) - 4; }\n");
    Screen built = drive(ed1, "\"" + cpp.string() + "\" --project \"" + dir.string() + "\"",
                         ctrl('b') + ctrl('q'), dir);
    check(onScreen(built, "lines of"), "and C++ goes to cl on its own");
    check(onScreen(built, "C++"), "with the status bar saying what it is");
    check(onScreen(built, "cl*"), "and a star, because the file chose it");

    file::path bad = dir / "src" / "bad.c";
    writeFile(bad, "int main(void)\n{\n    int x = ;\n    return 0;\n}\n");
    Screen broken = drive(ed1, "\"" + bad.string() + "\" --project \"" + dir.string() +
                               "\" --toolchain msvc",
                          ctrl('b') + ctrl('q'), dir);
    check(onScreen(broken, "error"), "a build that fails says so");
    check(onScreen(broken, "3/5"), "and cl's line is read as well as cc1's");
    check(onScreen(broken, "col 13"), "and its column too");

    file::remove_all(dir);
#endif
}

void compiling(const std::string& ed1, const std::string& cc1) {
    std::printf("building with cc1\n");

    if (cc1.empty()) {
        std::printf("  (no cc1 named, so those cases are not tried)\n");
        return;
    }

    file::path dir = freshProject("build");
    file::path good = dir / "src" / "good.c";
    writeFile(good, "int twice(int n)\n{\n    return n + n;\n}\n");

    std::string arguments = "\"" + good.string() + "\" --project \"" + dir.string() +
                            "\" --cc1 \"" + cc1 + "\"";
    Screen ok = drive(ed1, arguments, ctrl('b') + ctrl('q'), dir);
    check(onScreen(ok, "lines of"), "a build that works reports what it produced");
    check(onScreen(ok, "Assembly"), "and the assembly tab is there");

    file::path bad = dir / "src" / "bad.c";
    writeFile(bad, "int main(void)\n{\n    int x = ;\n    return 0;\n}\n");
    arguments = "\"" + bad.string() + "\" --project \"" + dir.string() +
                "\" --cc1 \"" + cc1 + "\"";
    Screen broken = drive(ed1, arguments, ctrl('b') + ctrl('q'), dir);
    check(onScreen(broken, "error"), "a build that fails says so");
    check(onScreen(broken, "3/5"), "and the caret lands on the line cc1 named");
    check(onScreen(broken, "col 13"), "in the column it named too");

    // C++ handed to cc1 is turned away before anything is run.
    file::path cpp = dir / "src" / "thing.cpp";
    writeFile(cpp, "class Thing { public: int n; };\n");
    Screen refused = drive(ed1, "\"" + cpp.string() + "\" --project \"" + dir.string() +
                                "\" --toolchain cc1 --cc1 \"" + cc1 + "\"",
                           ctrl('b') + ctrl('q'), dir);
    check(onScreen(refused, "cc1 compiles C, not C++"), "cc1 is not handed C++");

    file::remove_all(dir);
}

// The project's own build, as against the file in front of you. Two sources
// that only work together, so that a program coming out at all is proof they
// were linked and not merely compiled one at a time.
void buildingTheProject(const std::string& ed1, const std::string& cc1) {
    std::printf("building the project, not just the file\n");

    if (cc1.empty()) {
        std::printf("  (no cc1 named, so those cases are not tried)\n");
        return;
    }

    file::path dir = freshProject("target");
    writeFile(dir / "src" / "sum.c", "#include \"sum.h\"\n\nint addUp(int a, int b) { return a + b; }\n");
    writeFile(dir / "src" / "sum.h", "int addUp(int a, int b);\n");
    writeFile(dir / "src" / "main.c",
              "#include <stdio.h>\n\n#include \"sum.h\"\n\n"
              "int main(void)\n{\n    printf(\"answer %d\\n\", addUp(2, 40));\n    return 0;\n}\n");
    writeFile(dir / "ed1.json",
              "{\n  \"name\": \"sums\",\n  \"indent\": 4,\n"
              "  \"groups\": {\n"
              "    \"Sources\": [\"src/sum.c\", \"src/main.c\"],\n"
              "    \"Headers\": [\"src/sum.h\"]\n  },\n"
              "  \"build\": { \"target\": \"sums\", \"groups\": [\"Sources\"] }\n}\n");

    std::string arguments = "--project \"" + dir.string() + "\" --cc1 \"" + cc1 + "\"";

    Screen built = drive(ed1, arguments, kF4 + ctrl('q'), dir);
    check(onScreen(built, "2 sources"), "F4 builds the project's sources, not the open file");
    check(onScreen(built, "built sums"), "and says what it built");
    check(file::exists(dir / "sums") || file::exists(dir / "sums.exe"),
          "the program is beside the project, where it can be found again");

    // Run project is on the Build menu under the two that build a file: F10,
    // three columns right to Build, three items down, enter.
    Screen ran = drive(ed1, arguments,
                       kF10 + times(kRight, 3) + times(kDown, 3) + kEnter + ctrl('q'), dir);
    check(onScreen(ran, "answer 42"), "running the project runs the linked program");
    check(onScreen(ran, "returned 0"), "and reports what it returned");

#ifdef _WIN32
    // Nothing to stop inside here: cc1 writes MASM for this target and MASM
    // carries no line table. The single-file case above already checks that
    // the editor says so rather than failing quietly.
    std::printf("  (x86_64-windows carries no line table, so the project's debugger is not tried)\n");
#else
    // Open sum.c, put the caret on the line that adds, break there, and ask
    // the Debug menu for the project rather than F8 for the file. The
    // breakpoint is in the file that has no main in it, which is the point:
    // one program, two sources, and the line has to be found in the right one.
    Screen stopped = drive(ed1, "\"" + (dir / "src" / "sum.c").string() + "\" " + arguments,
                           times(kDown, 2) + kF9 +
                               kF10 + times(kRight, 4) + kDown + kEnter + ctrl('q'),
                           dir);
    // "sum.c:3 in addUp" rather than "addUp" anywhere: the word is in the
    // source on the screen as well, and a check that the source is on the
    // screen is not a check that the debugger read the frame.
    check(onScreen(stopped, "sum.c:3 in addUp"),
          "the project's debugger stops in the file and function asked for");
    check(onScreen(stopped, "a = 2"), "with the argument it was called with");
    check(onScreen(stopped, "b = 40"), "and the other one");

    // Stepping out of the file it stopped in opens the file it arrives in.
    // Nothing had main.c open here, so the tab and the status bar naming it
    // are the whole check - it used to say "stopped at main.c:9" while showing
    // sum.c, which is a stranger thing to say than saying nothing.
    Screen stepped = drive(ed1, "\"" + (dir / "src" / "sum.c").string() + "\" " + arguments,
                           times(kDown, 2) + kF9 +
                               kF10 + times(kRight, 4) + kDown + kEnter + kF7 + ctrl('q'),
                           dir);
    check(onScreen(stepped, "main.c"), "stepping into another file opens it");
    check(onScreen(stepped, "in main"), "and says it is in main now");
#endif

    // An error in a file that is not open opens it first. Nothing is opened at
    // startup here, so the caret arriving in main.c is the whole check.
    writeFile(dir / "src" / "main.c",
              "#include <stdio.h>\n\n#include \"sum.h\"\n\n"
              "int main(void)\n{\n    int total = addUp(2, 40)\n"
              "    printf(\"answer %d\\n\", total);\n    return 0;\n}\n");
    Screen broken = drive(ed1, arguments, kF4 + ctrl('q'), dir);
    check(onScreen(broken, "error"), "an error in the project build is reported");
    check(onScreen(broken, "main.c"), "naming the file it is in");
    // Line 8, not 7: a missing semicolon is reported where the next thing was
    // found, which is the line after the one that is missing it.
    check(onScreen(broken, "8/10"), "and the caret goes there, in a file nothing had opened");

    // Both languages in one target, which used to be refused with "this project
    // holds both C and C++, which cannot make one program" and now is not: the
    // group is split by language, cc1 takes the C and cl takes the C++, and the
    // objects meet at the linker.
    //
    // The main.c above was left broken on purpose by the case before this one,
    // so it is written again - this case is about the mixture and a syntax
    // error would stop it before the mixture was reached.
    writeFile(dir / "src" / "main.c",
              "#include <stdio.h>\n\n#include \"sum.h\"\n\n"
              "int twice(int n);\n\n"
              "int main(void)\n{\n    printf(\"answer %d\\n\", twice(addUp(2, 19)));\n"
              "    return 0;\n}\n");
    writeFile(dir / "src" / "extra.cpp",
              "extern \"C\" int twice(int n) { return n * 2; }\n");
    writeFile(dir / "ed1.json",
              "{\n  \"name\": \"sums\",\n  \"indent\": 4,\n"
              "  \"groups\": {\n"
              "    \"Sources\": [\"src/sum.c\", \"src/main.c\", \"src/extra.cpp\"]\n  },\n"
              "  \"build\": { \"target\": \"sums\", \"groups\": [\"Sources\"] }\n}\n");
    Screen mixed = drive(ed1, arguments, kF4 + ctrl('q'), dir);
    // wasShown, not onScreen: the console panel holds nine rows and a build
    // that runs two compilers and a linker writes more than that, so the first
    // compiler's line has scrolled off by the time it is over. What is being
    // checked is that the editor said it, not that it is still visible.
    check(wasShown(mixed, "Sources (cc1)"), "a group of two languages sends the C to cc1");
    // cl where there is one and c++ where there is not, and the point is that
    // nothing in the project file said either. Written out rather than asked
    // of the editor's own resolve(): this harness links src/path.cpp and
    // nothing else on purpose - it drives the editor as a program, and a test
    // that shares the editor's opinion cannot catch the editor being wrong.
#if defined(_WIN32)
    const char* cpp = "cl";
#elif defined(__APPLE__)
    const char* cpp = "clang++";
#else
    const char* cpp = "g++";
#endif
    check(wasShown(mixed, std::string("Sources (") + cpp + ")"),
          "and the C++ to this machine's C++ compiler, without being told to");
    check(!wasShown(mixed, "cannot make one program"),
          "and is not refused for holding both any more");
    check(wasShown(mixed, "linking with"), "and links the two compilers' objects itself");
    check(onScreen(mixed, "built sums"), "the two compilers' objects link into one program");

    // And it runs, which is the whole of what a mixed target is for: a C main
    // calling a function cl or c++ compiled, in one program.
    Screen ranMixed = drive(ed1, arguments,
                            kF10 + times(kRight, 3) + times(kDown, 3) + kEnter + ctrl('q'),
                            dir);
    check(wasShown(ranMixed, "answer 42"), "and runs, C calling into what the C++ compiler made");

    // Debugging the project is the same choice again: the program under the
    // debugger is the one the project builds, not the file in front of you.
    // The breakpoint goes in a file that has no main in it, which is the whole
    // point - one program, three sources, and the debugger has to find the
    // line in the right one.
    writeFile(dir / "src" / "main.c",
              "#include <stdio.h>\n\n#include \"sum.h\"\n\n"
              "int main(void)\n{\n    printf(\"answer %d\\n\", addUp(2, 40));\n    return 0;\n}\n");
    // And a project that says nothing about building is not an error, just
    // nothing to build - the file in front of you is still Ctrl-B's business.
    file::path plain = freshProject("noTarget");
    writeFile(plain / "src" / "one.c", "int main(void) { return 0; }\n");
    Screen quiet = drive(ed1, "--project \"" + plain.string() + "\" --cc1 \"" + cc1 + "\"",
                         kF4 + ctrl('q'), plain);
    check(onScreen(quiet, "does not say what it builds"),
          "a project with no build entry says so plainly");

    file::remove_all(dir);
    file::remove_all(plain);
}

// A file that compiles to different code depending on NDEBUG, so that the two
// configurations can be told apart by what came out rather than by what the
// command line claimed.
const char* kTwoWays =
    "#ifdef NDEBUG\n"
    "int value(void) { return 1; }\n"
    "#else\n"
    "int value(void) { return 1; }\n"
    "int second(void) { return 2; }\n"
    "int third(void) { return 3; }\n"
    "int fourth(void) { return 4; }\n"
    "#endif\n";

void configurations(const std::string& ed1, const std::string& cc1) {
    std::printf("debug and release\n");

    file::path dir = freshProject("config");
    file::path file = dir / "src" / "twoways.c";
    writeFile(file, kTwoWays);

    std::string common = "\"" + file.string() + "\" --project \"" + dir.string() + "\"";

    // The word is in the status bar whether or not anything can be built.
    Screen shown = drive(ed1, common + " --config release", ctrl('q'), dir);
    check(onScreen(shown, "release"), "the status bar says which configuration");
    Screen shownDebug = drive(ed1, common, ctrl('q'), dir);
    check(onScreen(shownDebug, "debug"), "and debug is what it starts in");

    // The project file remembers it.
    writeFile(dir / "ed1.json",
              "{\n  \"name\": \"Conf\",\n  \"config\": \"release\",\n"
              "  \"groups\": { \"Sources\": [] }\n}\n");
    Screen fromFile = drive(ed1, common, ctrl('q'), dir);
    check(onScreen(fromFile, "release"), "the project's configuration is used");

    Screen overridden = drive(ed1, common + " --config debug", ctrl('q'), dir);
    check(onScreen(overridden, "debug"), "and the flag still overrides it");

#ifdef _WIN32
    // cl can show the difference whether or not cc1 is about: NDEBUG takes
    // three functions out, and /O2 rewrites what is left.
    {
        Screen clDebug = drive(ed1, common + " --toolchain msvc --config debug",
                               ctrl('b') + ctrl('q'), dir);
        Screen clRelease = drive(ed1, common + " --toolchain msvc --config release",
                                 ctrl('b') + ctrl('q'), dir);
        check(wasShown(clDebug, "/Od"), "cl is given /Od for debug");
        check(wasShown(clRelease, "/O2"), "and /O2 for release");
        check(!message(clDebug).empty() && message(clDebug) != message(clRelease),
              "and the two produce different code");
    }
#endif

    if (cc1.empty()) {
        std::printf("  (no cc1 named, so its two configurations are not compared)\n");
        file::remove_all(dir);
        return;
    }

    std::string withCc1 = common + " --cc1 \"" + cc1 + "\"";
    Screen debug = drive(ed1, withCc1 + " --config debug", ctrl('b') + ctrl('q'), dir);
    Screen release = drive(ed1, withCc1 + " --config release", ctrl('b') + ctrl('q'), dir);

    check(wasShown(debug, "-D_DEBUG=1"), "the debug define is on the command line");
    check(wasShown(release, "-DNDEBUG=1"), "and the release one is");

    // And it did something: the same file compiled two ways gives two different
    // amounts of assembly, so the define reached the preprocessor rather than
    // merely being printed.
    check(!message(debug).empty() && message(debug) != message(release),
          "the two configurations produce different code");
    check(message(debug).find("lines of") != std::string::npos,
          "and both of them built");

    file::remove_all(dir);
}

// What the Debug panel says depends on the target: cc1 writes DWARF for two of
// the three and nothing for the one it generates MASM for. Both answers are the
// editor's own words about a compiler it has not run, so this needs no cc1 and
// runs on every machine.
//
// The menu reopens on the column it was left on, and on that column's first
// item. So the second F10 in each of these is one step right of Build, not four
// steps right of File - which cost an hour of believing the panel was broken.
void debugPanelPerTarget(const std::string& ed1) {
    std::printf("what the Debug panel says about each target\n");

    file::path dir = freshProject("debugpanel");
    file::path file = dir / "src" / "one.c";
    writeFile(file, "int main(void) { return 0; }\n");
    std::string common = "\"" + file.string() + "\" --project \"" + dir.string() + "\"";

    // Menus are reached by counting, so anything added to one moves everything
    // after it - a column added moves the columns to its right, and an item
    // added moves the items below it. Build is the fourth column, its Debug
    // panel the sixth item, and Target is two columns further on now that
    // Debug sits between them.
    const int kBuildColumn = 3;
    const int kTargetColumn = 5;
    // Seventh in Build now: Build project and Run project went in above it,
    // beneath the two that compile the file in front of you.
    const int kDebugPanelItem = 7;
    const std::string showDebugTab =
        kF10 + times(kRight, kBuildColumn) + times(kDown, kDebugPanelItem) + kEnter;
    const std::string toTarget = kF10 + times(kRight, kTargetColumn - kBuildColumn);

    Screen linux = drive(ed1, common,
                         showDebugTab + toTarget + kDown + kEnter + ctrl('q'), dir);
    check(onScreen(linux, "DWARF"), "x86_64-linux is said to carry DWARF");
    check(onScreen(linux, "x86_64-linux"), "and named while it is said");

    Screen darwin = drive(ed1, common,
                          showDebugTab + toTarget + times(kDown, 2) + kEnter + ctrl('q'), dir);
    check(onScreen(darwin, "DWARF"), "and arm64-darwin carries it as well");

    Screen windows = drive(ed1, common,
                           showDebugTab + toTarget + kEnter + ctrl('q'), dir);
    check(onScreen(windows, "no debug information"),
          "x86_64-windows is said to carry none");
    check(!onScreen(windows, "DWARF"), "and is not told it has DWARF");

    // Switching the target under an open panel refills it, rather than leaving
    // what was true of the target before.
    // The third F10 needs no Right at all: the menu is already on Target, and
    // one more step would land on Tools.
    Screen switched = drive(ed1, common,
                            showDebugTab + toTarget + kDown + kEnter +
                                kF10 + kEnter + ctrl('q'),
                            dir);
    check(onScreen(switched, "no debug information") && !onScreen(switched, "DWARF"),
          "and switching target changes what the open panel already said");

    // The flag itself, in the status bar, with no compiler run. Ctrl-D is the
    // toggle, so twice from debug is release and back to debug again.
    const std::string sayConfig = ctrl('d') + ctrl('d');

    Screen debugOnLinux = drive(ed1, common,
                                kF10 + times(kRight, kTargetColumn) + kDown + kEnter +
                                    sayConfig + ctrl('q'),
                                dir);
    check(wasShown(debugOnLinux, "-g -D_DEBUG=1"), "a debug build of it asks for -g");

    Screen debugOnWindows = drive(ed1, common,
                                  kF10 + times(kRight, kTargetColumn) + kEnter + sayConfig +
                                      ctrl('q'),
                                  dir);
    check(wasShown(debugOnWindows, "-D_DEBUG=1"),
          "a debug build of the third defines _DEBUG");
    check(!wasShown(debugOnWindows, "-g -D_DEBUG=1"),
          "and asks for no -g, which it would be refused");

    file::remove_all(dir);
}

const char* const kPrintsAndReturns =
    "#include <stdio.h>\n"
    "int main(void)\n"
    "{\n"
    "    printf(\"counted to three\\n\");\n"
    "    return 3;\n"
    "}\n";

// F5 compiles, links and runs, which is three things that can each go their own
// way. What the console has to keep apart is a compiler that refused and a
// program that ran and returned something other than zero: only the program
// knows what its number meant, and a build that failed never got one.
void runningTheProgram(const std::string& ed1, const std::string& cc1) {
    std::printf("building it, and running what came out\n");

    // A target this machine cannot run is turned away before anything is built,
    // so this case needs no compiler at all. x86_64-windows is the one nothing
    // here is, except on Windows, where x86_64-linux is.
    //
    // It gets a project of its own because a chosen target is remembered in the
    // project file, and a second editor started on the same one would open on
    // the target this left behind rather than on the host.
    const int kTargetColumn = 5;   // File, Edit, Project, Build, Debug, Target
#ifdef _WIN32
    const std::string toElsewhere = kF10 + times(kRight, kTargetColumn) + kDown + kEnter;
#else
    const std::string toElsewhere = kF10 + times(kRight, kTargetColumn) + kEnter;
#endif
    file::path away = freshProject("run-elsewhere");
    file::path awayFile = away / "src" / "three.c";
    writeFile(awayFile, kPrintsAndReturns);
    Screen refused = drive(ed1,
                           "\"" + awayFile.string() + "\" --project \"" + away.string() + "\"",
                           toElsewhere + kF5 + ctrl('q'), away);
    check(wasShown(refused, "only reaches -S here"),
          "a target this machine cannot run is turned away");
    check(!wasShown(refused, "program returned"), "and nothing is run");
    file::remove_all(away);

    file::path dir = freshProject("run");
    file::path file = dir / "src" / "three.c";
    writeFile(file, kPrintsAndReturns);
    std::string common = "\"" + file.string() + "\" --project \"" + dir.string() + "\"";

    if (cc1.empty()) {
        std::printf("  (no cc1 named, so nothing is actually built and run)\n");
        file::remove_all(dir);
        return;
    }

    std::string withCc1 = common + " --cc1 \"" + cc1 + "\"";

    // Twice: once in the source being edited, once in the console under it. Once
    // would be the source alone, which is on the screen whether anything ran or
    // not.
    Screen ran = drive(ed1, withCc1, kF5 + ctrl('q'), dir);
    check(rowsSaying(ran, "counted to three") == 2,
          "what the program printed reaches the console");
    check(wasShown(ran, "[program returned 3]"), "and what it returned is said as a number");
    check(message(ran).find("it returned 3") != std::string::npos,
          "and the status bar says so too");

    // The same file with the semicolon taken out: the compiler stops, and the
    // console must not go on to claim a program ran.
    writeFile(file, "int main(void) { return 0 }\n");
    Screen broken = drive(ed1, withCc1, kF5 + ctrl('q'), dir);
    check(!wasShown(broken, "program returned"), "a file that will not compile runs nothing");
    check(message(broken).find("error") != std::string::npos, "and the error is what is said");

    file::remove_all(dir);
}

const char* const kWorthStoppingIn =
    "static int twice(int n)\n"
    "{\n"
    "    int doubled = n * 2;\n"
    "    return doubled;\n"
    "}\n"
    "\n"
    "int main(void)\n"
    "{\n"
    "    int total = 0;\n"
    "    for (int i = 1; i <= 3; ++i) {\n"
    "        total = total + twice(i);\n"
    "    }\n"
    "    return total;\n"
    "}\n";

// Stopping the program on a line and walking through it, driven the way a
// person drives it: F9 on the line, F8 to start, F7 and F6 to move.
void stoppingAndStepping(const std::string& ed1, const std::string& cc1) {
    std::printf("breakpoints, and stepping through what stopped\n");

    file::path dir = freshProject("debugging");
    file::path file = dir / "src" / "stepped.c";
    writeFile(file, kWorthStoppingIn);
    std::string common = "\"" + file.string() + "\" --project \"" + dir.string() + "\"";

    // Line 11 is the one inside the loop, and the caret starts on line 1.
    const std::string toLoopBody = times(kDown, 10);

    // A breakpoint is the editor's own note and needs no compiler: it can be
    // set, seen and taken away with nothing installed at all.
    Screen marked = drive(ed1, common, toLoopBody + kF9 + ctrl('q'), dir);
    check(wasShown(marked, "breakpoint on line 11"), "F9 puts a breakpoint on the line");
    // The number is right-aligned with a gap after it, so the marker sits in
    // the column before the first digit and nothing moves when it appears.
    check(onScreen(marked, "*11"), "and marks it in the gutter, beside the number");

    // Ctrl with an arrow, which is a key the console has to send and the
    // decoder has to read before anything can be done with it. Asked here,
    // with nothing stopped, because that answer needs no debugger and so is
    // asked on all three machines - including the one where cc1's own target
    // cannot be debugged at all and every check below this is skipped.
    Screen noStack = drive(ed1, common, kCtrlUp + ctrl('q'), dir);
    check(wasShown(noStack, "no stack to walk"),
          "Ctrl-Up arrives as Ctrl-Up, and says there is nothing stopped");

    Screen unmarked = drive(ed1, common, toLoopBody + kF9 + kF9 + ctrl('q'), dir);
    check(wasShown(unmarked, "breakpoint off line 11"), "and F9 again takes it away");
    check(!onScreen(unmarked, "*11"), "leaving the gutter as it was");

    // A breakpoint is filed under the file's name, so renaming the file has to
    // carry it across. It did not: the mark left the gutter, and a debugger
    // started afterwards was told to stop in a file that was no longer there.
    // In a project of its own, since it leaves the file under another name.
    {
        file::path renaming = freshProject("renamed-under-a-breakpoint");
        file::path was = renaming / "src" / "stepped.c";
        writeFile(was, kWorthStoppingIn);
        std::string there = "\"" + was.string() + "\" --project \"" + renaming.string() + "\"";

        // Project menu, then down to Rename..., as fileCommands drives it.
        const std::string toRename = kF10 + times(kRight, 2) + times(kDown, 4) + kEnter;
        Screen followed = drive(ed1, there,
                                toLoopBody + kF9 + toRename + "src/moved.c" + kEnter + ctrl('q'),
                                renaming);
        check(file::exists(renaming / "src" / "moved.c"),
              "a file with a breakpoint in it can be renamed");
        check(onScreen(followed, "*11"),
              "and the breakpoint is still on the line under the new name");
        file::remove_all(renaming);
    }

    // Deleting a file takes its breakpoints with it. Nothing shows that at the
    // time - the tab loses its name whether they went or not - but a name can
    // come back, and a file written under it used to arrive with somebody
    // else's lines already marked.
    {
        file::path deleting = freshProject("deleted-with-a-breakpoint");
        file::path was = deleting / "src" / "stepped.c";
        writeFile(was, kWorthStoppingIn);
        std::string there = "\"" + was.string() + "\" --project \"" + deleting.string() + "\"";

        const std::string toProject = kF10 + times(kRight, 2);
        const std::string deleteIt =
            toProject + times(kDown, 6) + kEnter + "yes" + kEnter;
        // No kRight this time: the menu reopens on the column it was left on,
        // so a second F10 is already on Project. Walking right again lands on
        // some other menu's first item and quietly does something else.
        const std::string makeItAgain =
            kF10 + times(kDown, 3) + kEnter + "src/stepped.c" + kEnter;

        // Twelve lines in the new file, so that line 11 is there to be marked.
        Screen reborn = drive(ed1, there,
                              toLoopBody + kF9 + deleteIt + makeItAgain +
                                  times(kEnter, 12) + ctrl('s') + ctrl('q'),
                              deleting);
        check(file::exists(deleting / "src" / "stepped.c"),
              "a deleted file's name can be used again");
        check(!onScreen(reborn, "*11"),
              "and the new file under it has none of the old one's breakpoints");
        file::remove_all(deleting);
    }

#ifdef _WIN32
    // cc1 generates MASM for this machine's own target, which carries no line
    // table, so there is nothing here for a debugger to read - and so nothing
    // for the compiler named here to do. Said out loud because MSVC at /W4 /WX
    // treats an untouched parameter as an error, where clang and gcc do not.
    (void)cc1;
    // The reason is about this compiler and this target rather than about the
    // machine: the C file here goes to cc1, and what cc1 writes for Windows is
    // MASM. A C++ file on the same machine goes to cl and is a different story.
    Screen refused = drive(ed1, common, toLoopBody + kF9 + kF8 + ctrl('q'), dir);
    check(wasShown(refused, "carries no line table"),
          "and debugging says why it cannot start");
    check(wasShown(refused, "cc1"), "naming the compiler it is talking about");
    file::remove_all(dir);
    return;
#else
    if (cc1.empty()) {
        std::printf("  (no cc1 named, so nothing is built to stop inside)\n");
        file::remove_all(dir);
        return;
    }

    std::string withCc1 = common + " --cc1 \"" + cc1 + "\"";

    Screen stopped = drive(ed1, withCc1, toLoopBody + kF9 + kF8 + ctrl('q'), dir);
    check(onScreen(stopped, "stopped at stepped.c:11"), "F8 runs it and it stops on the line");
    check(onScreen(stopped, "in main"), "saying which function that line is in");
    check(onScreen(stopped, ">11"), "the gutter marks where it is standing");

    // The variables are cc1's own debug information, read back by somebody
    // else's debugger and shown by this editor.
    check(onScreen(stopped, "total = 0"), "and the locals are there, with their values");
    check(onScreen(stopped, "i = 1"), "including the one the loop declared");
    check(!onScreen(stopped, "called from"),
          "and nothing is said about a stack, since main was called by nobody");

    Screen inside = drive(ed1, withCc1, toLoopBody + kF9 + kF8 + kF6 + ctrl('q'), dir);
    check(onScreen(inside, "in twice"), "F6 steps into the call");
    check(onScreen(inside, "n = 1"), "where the argument is in scope");
    check(onScreen(inside, "called from"), "and now there is a stack to show");
    check(onScreen(inside, "main   stepped.c:11"),
          "naming what called it and the line waiting for it to come back");

    // And going to that frame, driven the way a person drives it: Ctrl-W twice
    // to reach the panel - the first press is the project pane - then down to
    // the frame and enter on it. The panel's top line is the line the cursor
    // is on, so six presses put the frame there:
    //
    //   stopped at stepped.c:3 in twice     the locals are n and doubled,
    //                                       which both debuggers list
    //     n = 1   [int]
    //     doubled = ...
    //
    //   called from
    //     main   stepped.c:11
    const std::string toTheFrame = ctrl('w') + ctrl('w') + times(kDown, 6) + kEnter;
    Screen went = drive(ed1, withCc1, toLoopBody + kF9 + kF8 + kF6 + toTheFrame + ctrl('q'), dir);
    check(wasShown(went, "where the call came from"), "enter on a frame goes to it");
    check(onScreen(went, "11/14"), "putting the caret on the line that is waiting");
    check(onScreen(went, "[text]"), "and the keyboard back in the text");

    // And the variables are that frame's: total and i belong to main and are
    // not in scope in twice at all. The tab says whose they are, since the
    // line above them still says the program stopped in twice.
    check(onScreen(went, "the variables are main's"), "the tab says whose variables it shows");
    check(onScreen(went, "total = 0"), "and they are the caller's own");
    check(!onScreen(went, "n = 1"), "with nothing left of the frame it stopped in");

    // The way back is the top line, which names that frame - and the cursor is
    // already standing on it, the tab having come back to the top.
    const std::string andBack = ctrl('w') + ctrl('w') + kEnter;
    Screen backAgain = drive(
        ed1, withCc1, toLoopBody + kF9 + kF8 + kF6 + toTheFrame + andBack + ctrl('q'), dir);
    check(wasShown(backAgain, "back where it stopped"), "enter on the top line goes back");
    check(onScreen(backAgain, "n = 1"), "and the variables are the stopped frame's again");
    check(!onScreen(backAgain, "the variables are main's"),
          "with nothing said about whose they are, the top line saying it");

    // The program has not moved: going to a line is not stepping, and the
    // arrow in the gutter still marks where it is standing.
    // "> 3", not ">3": the number is right-aligned in the gutter and the marker
    // sits in the column before its first digit, so a one-digit line has a
    // space between them where an eleven has none.
    check(onScreen(went, "> 3"), "while the program is still standing where it stopped");

    // The same walk with a key, from the text, without going near the panel -
    // which is where a person is when the question occurs to them.
    Screen up = drive(ed1, withCc1, toLoopBody + kF9 + kF8 + kF6 + kCtrlUp + ctrl('q'), dir);
    check(onScreen(up, "the variables are main's"), "Ctrl-Up looks at what called this");
    check(onScreen(up, "total = 0"), "with that frame's variables");
    check(onScreen(up, "11/14"), "and the caret on the line waiting for the call");

    // And the gutter says which line that is, in the code rather than only in
    // the panel. It outranks the breakpoint that is on the same line - the
    // mark is about now, and the breakpoint is about every run of the program.
    //
    // With the line's own text after it, because "stepped.c:11" in the panel
    // holds ":11" as well and a check that matches that is a check that passes
    // whatever the gutter does.
    const std::string markedLine = ":11         total = total + twice(i);";
    const std::string breakLine = "*11         total = total + twice(i);";
    check(onScreen(up, markedLine), "the gutter marks the line the frame is waiting on");
    check(onScreen(up, "> 3     int doubled"),
          "while the arrow stays where the program is standing");
    check(!onScreen(up, breakLine), "and the breakpoint's own mark gives way to it");

    // And the ends of it, which are the two answers with nowhere to go.
    Screen top = drive(ed1, withCc1,
                       toLoopBody + kF9 + kF8 + kF6 + kCtrlUp + kCtrlUp + ctrl('q'), dir);
    check(wasShown(top, "nothing called main"), "and says so at the top of the stack");

    Screen down = drive(ed1, withCc1,
                        toLoopBody + kF9 + kF8 + kF6 + kCtrlUp + kCtrlDown + ctrl('q'), dir);
    check(wasShown(down, "back where it stopped"), "Ctrl-Down comes back down");
    check(onScreen(down, "n = 1"), "to the variables of the frame it stopped in");
    check(onScreen(down, "3/14"), "and the line it stopped on");
    check(!onScreen(down, markedLine), "with the mark gone from the frame it was looking at");
    check(onScreen(down, breakLine), "and the breakpoint's own mark back where it was");

    Screen bottom = drive(ed1, withCc1, toLoopBody + kF9 + kF8 + kF6 + kCtrlDown + ctrl('q'), dir);
    check(wasShown(bottom, "nothing below it"), "and says so at the bottom of it");

    // Setting a variable: the cursor on its line in the panel, enter, and the
    // value typed into the box that asks for filenames. Stopped in main here,
    // where there is no stack to walk and variables all the same.
    //
    // Which variable that line holds is the debugger's business and not the
    // same on two machines: lldb lists them as they were declared and gdb
    // lists the innermost block first, so under gdb the loop's own i is above
    // total. The check is that the one on that line was set, whichever it is -
    // the same keystrokes naming a different variable is not this feature
    // going wrong.
    const std::string toTheVariable = ctrl('w') + ctrl('w') + times(kDown, 2) + kEnter;
    Screen written = drive(ed1, withCc1,
                           toLoopBody + kF9 + kF8 + toTheVariable + "7" + kEnter + ctrl('q'), dir);
    check(wasShown(written, "is 7 now"), "enter on a variable sets it");
    check(onScreen(written, "= 7"), "and the tab shows what is in there now");

    // And a value it will not take is refused in the debugger's own words,
    // which name the mistake better than anything the editor could invent.
    Screen refused = drive(ed1, withCc1,
                           toLoopBody + kF9 + kF8 + toTheVariable + "nosuch" + kEnter + ctrl('q'),
                           dir);
    check(!onScreen(refused, "= nosuch"), "a value it will not take is not written into the tab");
    check(onScreen(refused, "total = 0"), "and the variable is left as it was");

    // A watch, added from the Debug menu and then left alone: what makes it a
    // watch is that it is read again at the next stop without being asked for.
    // Nine items down that menu - Start, Debug project, breakpoint, over,
    // into, out, up, down, and then Watch expression.
    const std::string toWatch = kF10 + times(kRight, 4) + times(kDown, 8) + kEnter;
    Screen watching = drive(ed1, withCc1,
                            toLoopBody + kF9 + kF8 + toWatch + "total + i" + kEnter + ctrl('q'),
                            dir);
    check(onScreen(watching, "watching"), "the tab has a block for what is being watched");
    check(onScreen(watching, "total + i = 1"), "with the expression answered where it stopped");

    Screen followed = drive(ed1, withCc1,
                            toLoopBody + kF9 + kF8 + toWatch + "total + i" + kEnter + kF8 +
                                ctrl('q'),
                            dir);
    check(onScreen(followed, "total + i = 4"),
          "and it follows the stepping - 2 plus 2 the next time round");

    // A line that is neither says so rather than doing something. Six down
    // from the top of that tab is the "called from" heading, which names a
    // frame without being one - the likeliest line to press enter on by
    // mistake.
    const std::string toTheHeading = ctrl('w') + ctrl('w') + times(kDown, 5) + kEnter;
    Screen neither = drive(ed1, withCc1,
                           toLoopBody + kF9 + kF8 + kF6 + toTheHeading + ctrl('q'), dir);
    check(wasShown(neither, "neither a frame nor a variable"),
          "enter on a line that is neither says so");

    Screen carried = drive(ed1, withCc1, toLoopBody + kF9 + kF8 + kF7 + kF8 + ctrl('q'), dir);
    check(onScreen(carried, "total = 2"), "F7 steps over it and F8 carries on round the loop");
    check(onScreen(carried, "i = 2"), "with the counter moved on");

    file::remove_all(dir);
#endif
}

// Opening a directory that has no project file. It gets one rather than the
// editor opening without a project, which is the difference between a tool
// that starts and a tool that asks you to go and make something first.
void aDirectoryWithNoProject(const std::string& ed1) {
    std::printf("opening somewhere that has no project file\n");

    file::path dir = file::temp_directory_path() / "ed1-session-noproject";
    file::remove_all(dir);
    editor::path::makeDirectories((dir / "src").string());
    writeFile(dir / "src" / "one.c", "int one;\n");
    writeFile(dir / "notes.txt", "not source\n");

    check(!file::exists(dir / "ed1.json"), "there is no project file to begin with");

    Screen made = drive(ed1, "--project \"" + dir.string() + "\"", ctrl('q'), dir);
    check(file::exists(dir / "ed1.json"), "opening there writes one");
    check(wasShown(made, "so one was made"), "and says that is what it did");
    check(onScreen(made, "one.c"), "the source it found is in the pane");
    check(!onScreen(made, "notes.txt"), "and what is not source is not");

    // Opened again, the file that was written is the file that is read - no
    // second one, and nothing said about making anything.
    Screen again = drive(ed1, "--project \"" + dir.string() + "\"", ctrl('q'), dir);
    check(!wasShown(again, "so one was made"), "opening it again makes nothing");
    check(onScreen(again, "one.c"), "and reads back what was written");
    check(wasShown(again, "ready"), "and says it is ready, having nothing to do first");

    // Help is the last column, and About is under it. Checked from here as
    // well as in the window, since the two show the same lines from the core.
    // Eight rights rather than seven since Language joined the bar, and two
    // downs rather than one since Contents joined this menu above About - a
    // count of the columns and of the items, written down in the one place
    // that walks them.
    Screen about = drive(ed1, "--project \"" + dir.string() + "\"",
                         kF10 + times(kRight, 8) + times(kDown, 2) + kEnter + ctrl('q'), dir);
    check(onScreen(about, "RStudio 1.1"), "About names the product and version");
    check(onScreen(about, "G. R. Akhtar"), "and who it belongs to");
    check(onScreen(about, "Islamabad"), "and where they are, which the last line must not lose");

    // A project file that will not parse is somebody's work and is left alone.
    writeFile(dir / "ed1.json", "{ this is not json\n");
    Screen broken = drive(ed1, "--project \"" + dir.string() + "\"", ctrl('q'), dir);
    check(readFile(dir / "ed1.json").find("not json") != std::string::npos,
          "a project file that will not parse is not written over");
    check(!wasShown(broken, "so one was made"), "and nothing is made in its place");

    file::remove_all(dir);
}

}  // namespace

// The third language, driven rather than described. Everything below asks the
// editor to do something with a .shl and looks at what came back on the
// screen; nothing here reaches into the core.
void compilingShalimar(const std::string& ed1, const std::string& shc) {
    std::printf("building Shalimar with shc\n");

    if (shc.empty()) {
        std::printf("  (no shc named, so those cases are not tried)\n");
        return;
    }

    file::path dir = freshProject("shalimar");
    file::path good = dir / "src" / "good.shl";
    writeFile(good,
              "fun <int> = twice(n: int) {\n"
              "  return n + n\n"
              "}\n"
              "\n"
              "fun <> = main() {\n"
              "  ? twice(21)\n"
              "}\n");

    std::string arguments = "\"" + good.string() + "\" --project \"" + dir.string() +
                            "\" --shc \"" + shc + "\"";

    Screen opened = drive(ed1, arguments, ctrl('q'), dir);
    check(onScreen(opened, "Shalimar"), "the status bar names the language");

    Screen ok = drive(ed1, arguments, ctrl('b') + ctrl('q'), dir);
    check(onScreen(ok, "lines of"), "a build that works reports what it produced");
    check(onScreen(ok, "Assembly"), "and the assembly tab is there");

    // F5 builds a program and runs it, which is the whole of what a Shalimar
    // program is for.
    Screen ran = drive(ed1, arguments, kF5 + ctrl('q'), dir);
    check(wasShown(ran, "42"), "running it prints what the program prints");

    // shc names the line and no column, so the caret lands at the start of it.
    file::path bad = dir / "src" / "bad.shl";
    writeFile(bad,
              "fun <> = main() {\n"
              "  ? x\n"
              "}\n");
    std::string broken = "\"" + bad.string() + "\" --project \"" + dir.string() +
                         "\" --shc \"" + shc + "\"";
    Screen refusedIt = drive(ed1, broken, ctrl('b') + ctrl('q'), dir);
    check(onScreen(refusedIt, "Undefined variable"), "a build that fails says why");
    check(onScreen(refusedIt, "2/3"), "and the caret lands on the line shc named");

    // The Language menu, which is what makes a file whose name says nothing
    // still a Shalimar file. Language is the seventh column and Shalimar the
    // fourth item in it.
    file::path anonymous = dir / "src" / "notes.txt";
    writeFile(anonymous, "fun <> = main() {\n  ? 1\n}\n");
    Screen asText = drive(ed1, "\"" + anonymous.string() + "\" --project \"" +
                                   dir.string() + "\" --shc \"" + shc + "\"",
                          ctrl('q'), dir);
    check(onScreen(asText, "text"), "a .txt opens as plain text");

    Screen asShalimar = drive(ed1, "\"" + anonymous.string() + "\" --project \"" +
                                       dir.string() + "\" --shc \"" + shc + "\"",
                              // Language is the seventh column, and its first
                              // item is already selected when the menu opens -
                              // so three downs reach the fourth, not four.
                              kF10 + times(kRight, 6) + times(kDown, 3) +
                                  kEnter + ctrl('b') + ctrl('q'),
                              dir);
    check(wasShown(asShalimar, "language: Shalimar"),
          "and the Language menu says it is Shalimar after all");
    check(onScreen(asShalimar, "lines of"), "which is enough for shc to build it");

    file::remove_all(dir);
}

// A project made of Shalimar, which is not the same shape as one made of C.
// The language has no include and no separate compilation, so several .shl in
// a group are several programs rather than the parts of one - and the project
// has to say which it builds instead of taking whichever came first.
void aShalimarProject(const std::string& ed1, const std::string& shc) {
    std::printf("a project made of Shalimar\n");

    if (shc.empty()) {
        std::printf("  (no shc named, so those cases are not tried)\n");
        return;
    }

    file::path dir = freshProject("shmproject");
    writeFile(dir / "src" / "hello.shl",
              "fun <> = main() {\n  ? 6 * 7\n}\n");
    writeFile(dir / "ed1.json",
              "{\n"
              "  \"name\": \"hello\",\n"
              "  \"build\": { \"target\": \"hello\", \"groups\": [\"Sources\"] },\n"
              "  \"groups\": { \"Sources\": [\"src/hello.shl\"] }\n"
              "}\n");

    std::string arguments = "--project \"" + dir.string() + "\" --shc \"" + shc + "\"";
    Screen built = drive(ed1, arguments, kF4 + ctrl('q'), dir);
    check(onScreen(built, "built hello"), "F4 builds the project's one program");
    check(file::exists(dir / "hello") || file::exists(dir / "hello.exe"),
          "and leaves it beside the project, where it can be found again");

    // Run project: F10, three columns right to Build, three items down.
    Screen ran = drive(ed1, arguments,
                       kF10 + times(kRight, 3) + times(kDown, 3) + kEnter + ctrl('q'), dir);
    check(onScreen(ran, "42"), "running the project runs what came out");

    // A second program in the group, and the target names the first.
    writeFile(dir / "src" / "other.shl", "fun <> = main() {\n  ? 1\n}\n");
    writeFile(dir / "ed1.json",
              "{\n"
              "  \"name\": \"hello\",\n"
              "  \"build\": { \"target\": \"hello\", \"groups\": [\"Sources\"] },\n"
              "  \"groups\": { \"Sources\": [\"src/other.shl\", \"src/hello.shl\"] }\n"
              "}\n");
    Screen chose = drive(ed1, arguments, kF4 + ctrl('q'), dir);
    check(!wasShown(chose, "programs and builds one"),
          "a target named after one of them builds that one");

    // And a target named after none of them is refused rather than guessed.
    writeFile(dir / "ed1.json",
              "{\n"
              "  \"name\": \"hello\",\n"
              "  \"build\": { \"target\": \"neither\", \"groups\": [\"Sources\"] },\n"
              "  \"groups\": { \"Sources\": [\"src/other.shl\", \"src/hello.shl\"] }\n"
              "}\n");
    Screen refusedIt = drive(ed1, arguments, kF4 + ctrl('q'), dir);
    check(wasShown(refusedIt, "programs and builds one"),
          "a target named after none of them is refused, not guessed at");

    // Two files, one program. Shalimar has no include: shc looks for what the
    // program calls and does not define in the other files it was given, and
    // the project is what says which files those are.
    writeFile(dir / "src" / "shapes.shl",
              "real tolerance : 1e-9\n"
              "\n"
              "fun <real> = area(w: real, h: real) {\n"
              "  return w * h\n"
              "}\n"
              "\n"
              "fun <int> = nearly(a: real, b: real) {\n"
              "  if abs(a - b) < tolerance {\n"
              "    return 1\n"
              "  }\n"
              "  return 0\n"
              "}\n");
    writeFile(dir / "src" / "hello.shl",
              "fun <> = main() {\n"
              "  ? area(6.0, 7.0)\n"
              "  ? nearly(0.1 + 0.2, 0.3)\n"
              "}\n");
    writeFile(dir / "ed1.json",
              "{\n"
              "  \"name\": \"hello\",\n"
              "  \"build\": { \"target\": \"hello\", \"groups\": [\"Sources\"] },\n"
              "  \"groups\": { \"Sources\": [\"src/shapes.shl\", \"src/hello.shl\"] }\n"
              "}\n");
    Screen two = drive(ed1, arguments,
                       kF10 + times(kRight, 3) + times(kDown, 3) + kEnter + ctrl('q'), dir);
    check(wasShown(two, "42.0000000"),
          "a program calling a function in another file of the project builds and runs");
    check(wasShown(two, "also compiled shapes.shl"),
          "and the compiler says which file it went to, so nothing is silent");

    // Shalimar beside C is still refused, and it is the one refusal that did
    // not go away when a group got its own compiler - it is not about the
    // editor at all. Refused twice over, and the two say different things.
    //
    // In one group: no compiler takes both, so naming one cannot help.
    writeFile(dir / "src" / "bit.c", "int bit(void) { return 1; }\n");
    writeFile(dir / "ed1.json",
              "{\n"
              "  \"name\": \"hello\",\n"
              "  \"build\": { \"target\": \"hello\", \"groups\": [\"Sources\"] },\n"
              "  \"groups\": { \"Sources\": [\"src/hello.shl\", \"src/bit.c\"] }\n"
              "}\n");
    Screen together = drive(ed1, arguments, kF4 + ctrl('q'), dir);
    check(wasShown(together, "Shalimar and C or C++ in one group"),
          "Shalimar and C in one group is refused, naming the group");

    // In two groups, where every other pair of languages now works: this is
    // about what a Shalimar object is. Whichever file it came from it exports
    // the same three startup symbols, so two of them collide - and the
    // language has no declarations, so a call across a link could not be
    // checked. Compiler-S/docs/LINKING.md has it in full.
    writeFile(dir / "ed1.json",
              "{\n"
              "  \"name\": \"hello\",\n"
              "  \"build\": { \"target\": \"hello\", \"groups\": [\"Sources\", \"C\"] },\n"
              "  \"groups\": {\n"
              "    \"Sources\": [\"src/hello.shl\"],\n"
              "    \"C\": [\"src/bit.c\"]\n"
              "  }\n"
              "}\n");
    Screen apart = drive(ed1, arguments, kF4 + ctrl('q'), dir);
    check(wasShown(apart, "whole program"),
          "and in a group of its own it is refused for what a Shalimar object is");

    file::remove_all(dir);
}

// Stopping a Shalimar program from the Debug menu, which is a different thing
// from stopping a C one and not a second copy of it. There is no gdb, no lldb
// and no cdb here: the program stops itself, so this runs on every machine the
// suite runs on - including Windows, where cc1's own target cannot be debugged
// at all and every check in stoppingAndStepping is skipped.
void stoppingShalimar(const std::string& ed1, const std::string& shc) {
    std::printf("stopping a Shalimar program from the editor\n");

    if (shc.empty()) {
        std::printf("  (no shc named, so those cases are not tried)\n");
        return;
    }

    file::path dir = freshProject("shmdebug");
    file::path file = dir / "src" / "steps.shl";
    writeFile(file,
              "fun <int> = twice(n: int) {\n"      // 1
              "  int d : n + n\n"                  // 2
              "  return d\n"                       // 3
              "}\n"                                // 4
              "\n"                                 // 5
              "fun <> = main() {\n"                // 6
              "  int a : 1\n"                      // 7
              "  int b : twice(a)\n"               // 8
              "  ? b\n"                            // 9
              "}\n");                              // 10

    std::string arguments = "\"" + file.string() + "\" --project \"" + dir.string() +
                            "\" --shc \"" + shc + "\"";

    // The caret starts on line 1; line 8 is the call.
    const std::string toTheCall = times(kDown, 7);

    Screen stopped = drive(ed1, arguments, toTheCall + kF9 + kF8 + ctrl('q'), dir);
    check(onScreen(stopped, "stopped at steps.shl:8"),
          "F8 runs it and it stops on the line, with no debugger anywhere near it");
    check(onScreen(stopped, "> 8"), "the gutter marks where it is standing");

    // What it says in place of variables, which is the one thing this cannot
    // do and the one thing worth saying out loud. An empty list would have
    // read as "this line has none" rather than "there are none to have".
    check(onScreen(stopped, "not what is in it"),
          "and the tab says why there are no variables, rather than showing none");

    Screen inside = drive(ed1, arguments, toTheCall + kF9 + kF8 + kF6 + ctrl('q'), dir);
    check(onScreen(inside, "steps.shl:2"), "F6 steps into the call");

    // Out of the call is the statement *after* the one that made it: the call's
    // own statement was entered before the call was made, and stepping out
    // looks for the next statement shallower than where it is.
    Screen back = drive(ed1, arguments, toTheCall + kF9 + kF8 + kF6 + kF7 + kF7 + ctrl('q'), dir);
    check(onScreen(back, "steps.shl:9"), "and stepping on comes back past the call");

    // The program's own printing reaches the console, which is the point of
    // the channel keeping the two streams apart: a #stop in the middle of a
    // half-written line would have been unreadable and would have changed what
    // the program appeared to print.
    Screen printed = drive(ed1, arguments,
                           toTheCall + kF9 + kF8 + kF8 + ctrl('q'), dir);
    check(wasShown(printed, "returned"), "carrying on to the end says so");

    // Release links a runtime with no debugger in it, so F8 has nothing to
    // stop. That is the boundary, and the message says the true reason rather
    // than "built without -g", which shc has never had.
    //
    // Ctrl-D is the toggle rather than the debug half of a pair, and debug is
    // where a project starts - so one press is release.
    Screen release = drive(ed1, arguments, ctrl('d') + toTheCall + kF9 + kF8 + ctrl('q'), dir);
    check(wasShown(release, "no debugger in it"),
          "and a release build says what it has not got, not what shc has never had");

    file::remove_all(dir);
}

// A compiler per group, and one link at the end.
//
// This is the shape that used to be refused: a target whose groups do not all
// go to the same compiler. Each group compiles to objects with its own, and the
// editor names the linker itself, because no compiler here takes an object as
// an input - hand cc1 a .o and it reads it as C and complains about a stray
// byte on line 1.
void aCompilerPerGroup(const std::string& ed1, const std::string& cc1) {
    std::printf("a compiler per group, and one link\n");

    // The machine's real C++ compiler, by name. Written out rather than asked
    // of the editor: this harness links src/path.cpp and nothing else on
    // purpose - a test that shares the editor's opinion cannot catch the
    // editor being wrong about it.
#if defined(_WIN32)
    const char* cpp = "cl";
#elif defined(__APPLE__)
    const char* cpp = "clang++";
#else
    const char* cpp = "g++";
#endif

    if (cc1.empty()) {
        std::printf("  (no cc1 named, so those cases are not tried)\n");
        return;
    }

    file::path dir = freshProject("per-group");
    file::create_directories(dir / "lib");
    writeFile(dir / "src" / "main.c",
              "#include <stdio.h>\n\nint helper(int n);\n\n"
              "int main(void)\n{\n    printf(\"helper %d\\n\", helper(6));\n"
              "    return 0;\n}\n");
    writeFile(dir / "lib" / "helper.c", "int helper(int n) { return n * 7; }\n");

    // Two groups, and the second names its compiler by hand. Both go to cc1
    // here, which is the point: it is two *parts* rather than two languages,
    // so the object-and-link path is what runs, on a machine where the whole
    // of it can be checked.
    writeFile(dir / "ed1.json",
              "{\n  \"name\": \"two\",\n  \"indent\": 4,\n"
              "  \"groups\": {\n"
              "    \"Sources\": [\"src/main.c\"],\n"
              "    \"Library\": { \"files\": [\"lib/helper.c\"], \"toolchain\": \"cc1\" }\n"
              "  },\n"
              "  \"build\": { \"target\": \"two\", \"groups\": [\"Sources\", \"Library\"] }\n}\n");

    std::string arguments = "--project \"" + dir.string() + "\" --cc1 \"" + cc1 + "\"";

    Screen built = drive(ed1, arguments, kF4 + ctrl('q'), dir);
    check(onScreen(built, "Sources (cc1)"), "each group is compiled under its own name");
    check(onScreen(built, "Library (cc1)"), "including the one that named its compiler");
    check(onScreen(built, "linking with"), "and the editor says what it linked with");
    check(onScreen(built, "built two"), "the program comes out");
    check(file::exists(dir / "two") || file::exists(dir / "two.exe"),
          "and is beside the project where it can be found again");

    // The objects are the editor's mess and go with it. A directory of stale
    // objects is how a later build comes to link something nobody compiled.
    check(!file::exists(dir / "main.o") && !file::exists(dir / "helper.o"),
          "and no objects are left lying about the project");

    Screen ran = drive(ed1, arguments,
                       kF10 + times(kRight, 3) + times(kDown, 3) + kEnter + ctrl('q'), dir);
    check(wasShown(ran, "helper 42"), "running it runs what the two groups made together");

    // Three groups and three routings, which is the shape the whole thing was
    // for. C++ names nothing anywhere - every machine has one C++ compiler and
    // there is nothing to choose - and the only group that names a compiler is
    // a group of C that wants the other one. That is the asymmetry: C is the
    // one language two compilers can both take.
    {
        file::path three = freshProject("three-routings");
        file::create_directories(three / "engine");
        writeFile(three / "src" / "main.c",
                  "#include <stdio.h>\n\nint spin(int n);\nint legacy(int n);\n\n"
                  "int main(void)\n{\n    printf(\"total %d\\n\", spin(3) + legacy(4));\n"
                  "    return 0;\n}\n");
        writeFile(three / "src" / "legacy.c", "int legacy(int n) { return n * 10; }\n");
        // std::vector on purpose: if the C++ runtime did not reach the link,
        // this is what says so, and it says it at link time rather than by
        // going wrong later.
        // <cstddef> and std::size_t, spelled out. Apple's libc++ drags size_t
        // into scope through <vector> and libstdc++ does not, so the bare name
        // compiled on the Mac and stopped the build on the Linux box - which
        // is the third machine earning its keep on the day it was added.
        writeFile(three / "engine" / "engine.cpp",
                  "#include <cstddef>\n#include <vector>\n\n"
                  "extern \"C\" int spin(int n)\n{\n"
                  "    std::vector<int> v;\n"
                  "    for (int i = 0; i < n; ++i) v.push_back(i * i);\n"
                  "    int total = 0;\n"
                  "    for (std::size_t i = 0; i < v.size(); ++i) total += v[i];\n"
                  "    return total;\n}\n");
        writeFile(three / "ed1.json",
                  "{\n  \"name\": \"three\",\n  \"indent\": 4,\n"
                  "  \"groups\": {\n"
                  "    \"Sources\": [\"src/main.c\"],\n"
                  "    \"Legacy\": { \"files\": [\"src/legacy.c\"], \"toolchain\": \"c++\" },\n"
                  "    \"Engine\": [\"engine/engine.cpp\"]\n  },\n"
                  "  \"build\": { \"target\": \"three\", "
                  "\"groups\": [\"Sources\", \"Legacy\", \"Engine\"] }\n}\n");

        std::string theirs = "--project \"" + three.string() + "\" --cc1 \"" + cc1 + "\"";
        Screen made = drive(ed1, theirs, kF4 + ctrl('q'), three);
        check(wasShown(made, "Sources (cc1)"), "a C group that says nothing goes to cc1");
        check(wasShown(made, std::string("Legacy (") + cpp + ")"),
              "a C group that names the host's C++ compiler goes there instead");
        check(wasShown(made, std::string("Engine (") + cpp + ")"),
              "and a C++ group needs to name nothing, there being one answer");
        check(onScreen(made, "built three"), "all three link into one program");

        Screen went = drive(ed1, theirs,
                            kF10 + times(kRight, 3) + times(kDown, 3) + kEnter + ctrl('q'),
                            three);
        check(wasShown(went, "total 45"),
              "which runs - and the std::vector in it says the C++ runtime reached the link");
        file::remove_all(three);
    }

    // An error in the second group lands in the second group's file. It used
    // to be looked for among the target's sources starting at the first, which
    // is right when there is one command and wrong the moment there are two.
    writeFile(dir / "lib" / "helper.c", "int helper(int n) { return n * ; }\n");
    Screen broken = drive(ed1, arguments, kF4 + ctrl('q'), dir);
    check(onScreen(broken, "error"), "an error in the second group is reported");
    check(onScreen(broken, "helper.c"), "naming the file it is actually in");

    file::remove_all(dir);
}

// Help, which is the one menu whose whole job is to be readable.
void theHelpMenu(const std::string& ed1) {
    std::printf("the manual, from the Help menu\n");

    file::path dir = freshProject("help-menu");
    writeFile(dir / "src" / "one.c", "int main(void) { return 0; }\n");
    std::string arguments = "\"" + (dir / "src" / "one.c").string() +
                            "\" --project \"" + dir.string() + "\"";

    // Help is the ninth column and Contents its first item, which is already
    // selected when the menu opens - so no downs.
    const std::string toContents = kF10 + times(kRight, 8) + kEnter;
    Screen shown = drive(ed1, arguments, toContents + ctrl('q'), dir);
    check(onScreen(shown, "the manual"), "Help > Contents shows the manual's contents");
    check(onScreen(shown, "What it is"), "with the first page in it");
    check(onScreen(shown, "three languages"), "and a line saying what that page is about");
    check(wasShown(shown, "help/"), "and says where the pages themselves are");

    // The version is not written out twice. A contents that said 1.0 while
    // About said 1.1 is the sort of thing nobody notices for a year.
    Screen about = drive(ed1, arguments, kF10 + times(kRight, 8) + times(kDown, 2) + kEnter +
                                             ctrl('q'), dir);
    check(onScreen(about, "RStudio"), "Help > About still names the product");

    // F1 is the keys and is not the same thing as the contents.
    // The keys are longer than the seven rows the panel shows and it opens at
    // the top, so what is checked has to be near the top of the listing.
    // wasShown does not help either: the editor writes only the rows the panel
    // is showing, so a line below the fold never reaches the terminal at all -
    // which is what stops it flickering and is also why it cannot be checked
    // from out here without scrolling to it first.
    Screen keys = drive(ed1, arguments, kF1 + ctrl('q'), dir);
    check(onScreen(keys, "these keys"), "F1 shows the keys");
    check(!onScreen(keys, "the manual"), "which is a different screen from the contents");

    file::remove_all(dir);
}

int main(int argc, char** argv) {
#ifdef _WIN32
    std::string ed1 = "ed1.exe";
#else
    std::string ed1 = "./ed1";
#endif
    std::string cc1;
    std::string shc;

    if (argc > 1) ed1 = argv[1];
    // Named in the environment rather than positionally: an empty CC1 on a
    // make line collapses, and the compiler after it then arrives as the one
    // before - which reads as fifty debugger failures and is nothing of the
    // kind.
    if (argc > 2) cc1 = argv[2];
    if (cc1.empty()) {
        const char* fromEnv = std::getenv("CC1");
        if (fromEnv) cc1 = fromEnv;
    }
    if (shc.empty()) {
        const char* fromEnv = std::getenv("SHC");
        if (fromEnv) shc = fromEnv;
    }

    // A compiler named but not there is worse than none named: every case that
    // needs it fails, and none of them says why. Dropped with a word, so the
    // cases skip themselves as they do when nothing was named at all. A path
    // with a ~ in it is the way this happens - make does not expand one.
    if (!cc1.empty() && !editor::path::exists(cc1)) {
        std::printf("no cc1 at %s - the cases that need one are not tried\n\n", cc1.c_str());
        cc1.clear();
    }
    if (!shc.empty() && !editor::path::exists(shc)) {
        std::printf("no shc at %s - the cases that need one are not tried\n\n", shc.c_str());
        shc.clear();
    }

    std::printf("driving %s\n\n", ed1.c_str());

    editingAndLayout(ed1);
    aDirectoryWithNoProject(ed1);
    colouring(ed1);
    fileCommands(ed1);
    projectPane(ed1);
    findingAndReplacing(ed1);
    leavingWithChanges(ed1);
    reindenting(ed1);
    undoing(ed1);
    selectingAndPasting(ed1);
    multiByteText(ed1);
    compiling(ed1, cc1);
    buildingTheProject(ed1, cc1);
    buildingWithCl(ed1);
    configurations(ed1, cc1);
    debugPanelPerTarget(ed1);
    runningTheProgram(ed1, cc1);
    stoppingAndStepping(ed1, cc1);
    compilingShalimar(ed1, shc);
    aShalimarProject(ed1, shc);
    stoppingShalimar(ed1, shc);
    aCompilerPerGroup(ed1, cc1);
    theHelpMenu(ed1);

    std::printf("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
