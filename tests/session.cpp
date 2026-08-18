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
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

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

std::string readFile(const fs::path& path) {
    std::ifstream in(path.string().c_str(), std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void writeFile(const fs::path& path, const std::string& text) {
    std::ofstream out(path.string().c_str(), std::ios::binary);
    out << text;
}

struct Screen {
    std::string raw;                  // everything the editor wrote
    std::vector<std::string> rows;    // the last screen, escape codes replayed
};

// Replays the final screen onto a grid. The editor positions things absolutely
// - the menu drops over the text - so the bytes cannot simply have their escape
// codes stripped out and be read in order.
std::vector<std::string> lastScreen(const std::string& raw) {
    const std::string start = "\x1b[?25l\x1b[H";
    size_t at = raw.rfind(start);
    std::string last = (at == std::string::npos) ? raw : raw.substr(at + start.size());
    size_t clear = last.find("\x1b[2J");
    if (clear != std::string::npos) last = last.substr(0, clear);

    std::vector<std::string> rows;
    size_t row = 0, col = 0;

    for (size_t i = 0; i < last.size();) {
        if (last[i] == '\x1b' && i + 1 < last.size() && last[i + 1] == '[') {
            size_t j = i + 2;
            std::string digits;
            while (j < last.size() && !((last[j] >= 'A' && last[j] <= 'Z') ||
                                        (last[j] >= 'a' && last[j] <= 'z')))
                digits += last[j++];
            if (j < last.size() && last[j] == 'H') {
                size_t semi = digits.find(';');
                row = static_cast<size_t>(std::atol(digits.c_str()));
                row = row ? row - 1 : 0;
                col = (semi == std::string::npos)
                          ? 0
                          : static_cast<size_t>(std::atol(digits.c_str() + semi + 1)) - 1;
            }
            i = (j < last.size()) ? j + 1 : last.size();
            continue;
        }

        char c = last[i++];
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
             const std::string& keys, const fs::path& where) {
    fs::path keyFile = where / "keys.in";
    fs::path outFile = where / "screen.out";
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
    fs::remove(keyFile);
    fs::remove(outFile);
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

fs::path freshProject(const std::string& name) {
    fs::path dir = fs::temp_directory_path() / ("ed1-session-" + name);
    fs::remove_all(dir);
    fs::create_directories(dir / "src");
    writeFile(dir / "ed1.json",
              "{\n  \"name\": \"Trial\",\n  \"indent\": 4,\n"
              "  \"groups\": { \"Sources\": [] }\n}\n");
    return dir;
}

// ---------------------------------------------------------------------------

void editingAndLayout(const std::string& ed1) {
    std::printf("typing, and what the editor does to it\n");

    fs::path dir = freshProject("typing");
    fs::path file = dir / "src" / "typed.c";

    // Typed with no leading space anywhere. What comes back should be laid out.
    std::string keys = "void f(void) {\ng();\nif (x)\ny();\nswitch (n) {\ncase 1:\n"
                       "break;\n}\n}" + ctrl('s') + ctrl('q');
    drive(ed1, "\"" + file.string() + "\" --project \"" + dir.string() + "\"", keys, dir);

    checkEqual(readFile(file),
               "void f(void) {\n    g();\n    if (x)\n        y();\n    switch (n) {\n"
               "    case 1:\n        break;\n    }\n}\n",
               "a function typed flat is saved laid out");

    // Ctrl-F lays out a file that arrived with no layout of its own.
    fs::path flat = dir / "src" / "flat.c";
    writeFile(flat, "int main(void)\n{\nif (x)\nreturn 1;\nreturn 0;\n}\n");
    drive(ed1, "\"" + flat.string() + "\" --project \"" + dir.string() + "\"",
          ctrl('a') + ctrl('s') + ctrl('q'), dir);
    checkEqual(readFile(flat),
               "int main(void)\n{\n    if (x)\n        return 1;\n    return 0;\n}\n",
               "Ctrl-A lays out a whole file");

    // Tabs instead of spaces, asked for on the command line.
    fs::path tabbed = dir / "src" / "tabbed.c";
    writeFile(tabbed, "int f(void)\n{\nreturn 1;\n}\n");
    drive(ed1, "\"" + tabbed.string() + "\" --project \"" + dir.string() + "\" --tabs",
          ctrl('a') + ctrl('s') + ctrl('q'), dir);
    check(readFile(tabbed).find("\treturn 1;") != std::string::npos,
          "--tabs indents with a tab");

    fs::remove_all(dir);
}

void colouring(const std::string& ed1) {
    std::printf("what the screen is coloured with\n");

    fs::path dir = freshProject("colour");
    fs::path file = dir / "src" / "colour.c";
    writeFile(file, "int main(void)\n{\n    return 0;   /* done */\n}\n");

    Screen screen = drive(ed1, "\"" + file.string() + "\" --project \"" + dir.string() + "\"",
                          ctrl('q'), dir);

    // The colours are in the bytes even though they are not in the grid.
    check(screen.raw.find("\x1b[94m") != std::string::npos, "a keyword is coloured");
    check(screen.raw.find("\x1b[36m") != std::string::npos, "a type is coloured");
    check(screen.raw.find("\x1b[90m") != std::string::npos, "a comment is coloured");
    check(onScreen(screen, "  1 int main(void)"), "and the line numbers are there");
    check(onScreen(screen, "C  "), "the status bar names the language");

    fs::remove_all(dir);
}

void fileCommands(const std::string& ed1) {
    std::printf("making, renaming, regrouping and deleting\n");

    fs::path dir = freshProject("files");
    std::string project = " --project \"" + dir.string() + "\"";

    // Project menu: right twice from File, then down to the item wanted.
    const std::string toProject = kF10 + times(kRight, 2);

    // New file...
    drive(ed1, project, toProject + times(kDown, 3) + kEnter + "src/made.c" + kEnter + ctrl('q'),
          dir);
    check(fs::exists(dir / "src" / "made.c"), "New file makes the file");
    check(readFile(dir / "ed1.json").find("src/made.c") != std::string::npos,
          "and puts it in the project");

    // A path two directories deep is refused, and nothing is written.
    Screen deep = drive(ed1, project,
                        toProject + times(kDown, 3) + kEnter + "a/b/deep.c" + kEnter + ctrl('q'),
                        dir);
    check(!fs::exists(dir / "a"), "a file two directories deep is not made");
    check(onScreen(deep, "two levels at most"), "and the rule says so on screen");

    // Rename... acts on the file being edited.
    drive(ed1, "\"" + (dir / "src" / "made.c").string() + "\"" + project,
          toProject + times(kDown, 4) + kEnter + "src/moved.c" + kEnter + ctrl('q'), dir);
    check(!fs::exists(dir / "src" / "made.c"), "Rename takes the old name away");
    check(fs::exists(dir / "src" / "moved.c"), "and puts the new one there");
    check(readFile(dir / "ed1.json").find("src/moved.c") != std::string::npos,
          "and the project follows it");

    // Move to group... changes the project and nothing on disk.
    drive(ed1, "\"" + (dir / "src" / "moved.c").string() + "\"" + project,
          toProject + times(kDown, 5) + kEnter + "Extras" + kEnter + ctrl('q'), dir);
    std::string written = readFile(dir / "ed1.json");
    check(written.find("Extras") != std::string::npos, "regrouping makes the group");
    check(fs::exists(dir / "src" / "moved.c"), "and leaves the file where it was");

    // Delete... only when the answer is yes.
    drive(ed1, "\"" + (dir / "src" / "moved.c").string() + "\"" + project,
          toProject + times(kDown, 6) + kEnter + "no" + kEnter + ctrl('q'), dir);
    check(fs::exists(dir / "src" / "moved.c"), "Delete answered with no keeps the file");

    drive(ed1, "\"" + (dir / "src" / "moved.c").string() + "\"" + project,
          toProject + times(kDown, 6) + kEnter + "yes" + kEnter + ctrl('q'), dir);
    check(!fs::exists(dir / "src" / "moved.c"), "and answered with yes deletes it");
    check(readFile(dir / "ed1.json").find("src/moved.c") == std::string::npos,
          "and takes it out of the project too");

    fs::remove_all(dir);
}

void selectingAndPasting(const std::string& ed1) {
    std::printf("selecting, copying and pasting\n");

    fs::path dir = freshProject("clip");
    fs::path file = dir / "src" / "clip.c";
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

    fs::remove_all(dir);
}

void multiByteText(const std::string& ed1) {
    std::printf("text that is not ASCII\n");

    fs::path dir = freshProject("utf8");
    fs::path file = dir / "src" / "urdu.c";
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

    fs::remove_all(dir);
}

void undoing(const std::string& ed1) {
    std::printf("undo and redo, in the editor\n");

    fs::path dir = freshProject("undo");
    fs::path file = dir / "src" / "undo.c";
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
    fs::path flat = dir / "src" / "flat.c";
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

    fs::remove_all(dir);
}

void findingAndReplacing(const std::string& ed1) {
    std::printf("finding and replacing, in the editor\n");

    fs::path dir = freshProject("find");
    fs::path file = dir / "src" / "find.c";
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

    fs::remove_all(dir);
}

void projectPane(const std::string& ed1) {
    std::printf("the project pane\n");

    fs::path dir = freshProject("pane");
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
    fs::path flat = dir / "src" / "three.c";
    writeFile(flat, "int f(void)\n{\nreturn 3;\n}\n");
    drive(ed1, "\"" + flat.string() + "\" --project \"" + dir.string() + "\"",
          ctrl('a') + ctrl('s') + ctrl('q'), dir);
    check(readFile(flat).find("\n  return 3;") != std::string::npos,
          "the project's indent of 2 is what gets used");

    fs::remove_all(dir);
}

// The MSVC half. No path is needed: the editor finds Visual Studio itself, so
// on Windows this runs whether or not anyone named a compiler.
void buildingWithCl(const std::string& ed1) {
#ifndef _WIN32
    (void)ed1;   // there is no cl to find anywhere else
#else
    std::printf("building with cl\n");

    fs::path dir = freshProject("cl");

    fs::path good = dir / "src" / "good.c";
    writeFile(good, "int twice(int n)\n{\n    return n + n;\n}\n");
    Screen ok = drive(ed1, "\"" + good.string() + "\" --project \"" + dir.string() +
                           "\" --toolchain msvc",
                      ctrl('b') + ctrl('q'), dir);
    check(onScreen(ok, "lines of"), "cl builds C, found without a Developer prompt");

    // The one cc1 cannot take at all.
    fs::path cpp = dir / "src" / "thing.cpp";
    writeFile(cpp, "class Thing {\npublic:\n    int twice(int n) { return n + n; }\n};\n"
                   "int main(void) { Thing t; return t.twice(2) - 4; }\n");
    Screen built = drive(ed1, "\"" + cpp.string() + "\" --project \"" + dir.string() + "\"",
                         ctrl('b') + ctrl('q'), dir);
    check(onScreen(built, "lines of"), "and C++ goes to cl on its own");
    check(onScreen(built, "C++"), "with the status bar saying what it is");
    check(onScreen(built, "cl*"), "and a star, because the file chose it");

    fs::path bad = dir / "src" / "bad.c";
    writeFile(bad, "int main(void)\n{\n    int x = ;\n    return 0;\n}\n");
    Screen broken = drive(ed1, "\"" + bad.string() + "\" --project \"" + dir.string() +
                               "\" --toolchain msvc",
                          ctrl('b') + ctrl('q'), dir);
    check(onScreen(broken, "error"), "a build that fails says so");
    check(onScreen(broken, "3/5"), "and cl's line is read as well as cc1's");
    check(onScreen(broken, "col 13"), "and its column too");

    fs::remove_all(dir);
#endif
}

void compiling(const std::string& ed1, const std::string& cc1) {
    std::printf("building with cc1\n");

    if (cc1.empty()) {
        std::printf("  (no cc1 named, so those cases are not tried)\n");
        return;
    }

    fs::path dir = freshProject("build");
    fs::path good = dir / "src" / "good.c";
    writeFile(good, "int twice(int n)\n{\n    return n + n;\n}\n");

    std::string arguments = "\"" + good.string() + "\" --project \"" + dir.string() +
                            "\" --cc1 \"" + cc1 + "\"";
    Screen ok = drive(ed1, arguments, ctrl('b') + ctrl('q'), dir);
    check(onScreen(ok, "lines of"), "a build that works reports what it produced");
    check(onScreen(ok, "Assembly"), "and the assembly tab is there");

    fs::path bad = dir / "src" / "bad.c";
    writeFile(bad, "int main(void)\n{\n    int x = ;\n    return 0;\n}\n");
    arguments = "\"" + bad.string() + "\" --project \"" + dir.string() +
                "\" --cc1 \"" + cc1 + "\"";
    Screen broken = drive(ed1, arguments, ctrl('b') + ctrl('q'), dir);
    check(onScreen(broken, "error"), "a build that fails says so");
    check(onScreen(broken, "3/5"), "and the caret lands on the line cc1 named");
    check(onScreen(broken, "col 13"), "in the column it named too");

    // C++ handed to cc1 is turned away before anything is run.
    fs::path cpp = dir / "src" / "thing.cpp";
    writeFile(cpp, "class Thing { public: int n; };\n");
    Screen refused = drive(ed1, "\"" + cpp.string() + "\" --project \"" + dir.string() +
                                "\" --toolchain cc1 --cc1 \"" + cc1 + "\"",
                           ctrl('b') + ctrl('q'), dir);
    check(onScreen(refused, "cc1 compiles C, not C++"), "cc1 is not handed C++");

    fs::remove_all(dir);
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

    fs::path dir = freshProject("config");
    fs::path file = dir / "src" / "twoways.c";
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
        fs::remove_all(dir);
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

    fs::remove_all(dir);
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    std::string ed1 = "ed1.exe";
#else
    std::string ed1 = "./ed1";
#endif
    std::string cc1;

    if (argc > 1) ed1 = argv[1];
    if (argc > 2) cc1 = argv[2];
    if (cc1.empty()) {
        const char* fromEnv = std::getenv("CC1");
        if (fromEnv) cc1 = fromEnv;
    }

    std::printf("driving %s\n\n", ed1.c_str());

    editingAndLayout(ed1);
    colouring(ed1);
    fileCommands(ed1);
    projectPane(ed1);
    findingAndReplacing(ed1);
    undoing(ed1);
    selectingAndPasting(ed1);
    multiByteText(ed1);
    compiling(ed1, cc1);
    buildingWithCl(ed1);
    configurations(ed1, cc1);

    std::printf("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
