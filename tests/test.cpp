// The two pieces of this editor with a contract worth pinning down: the layout
// rules, and the reading of cc1's one diagnostic. Neither needs a terminal, so
// neither is checked by typing into one and looking.

#include <cstdio>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "buffer.h"
#include "path.h"
#include "process.h"
#include "settings.h"
#include "workspace.h"
#include "debugger.h"

// The seam the window is built on. It is tested from here because the window
// itself only runs on Windows, where cc1 emits MASM and there is no debugging
// to be had - so the one machine that can run the GUI is the one machine that
// cannot exercise what it calls.
#include "bridge.h"
#include "compile.h"
#include "indent.h"
#include "symbols.h"
#include "syntax.h"
#include "find.h"
#include "json.h"
#include "project.h"
#include "toolchain.h"
#include "workspace.h"
#include "utf8.h"

// What these tests used of <filesystem>, which is C++17 and so not here: a path
// that can be joined with /, and three operations. Over src/path.cpp, the same
// code the editor itself uses.
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
inline bool remove_all(const path& where) { return editor::path::removeTree(where.text); }
inline bool create_directories(const path& where) {
    return editor::path::makeDirectories(where.text);
}
inline path temp_directory_path() { return path(editor::path::tempDir()); }

}  // namespace file

namespace {

int failures = 0;
int checks = 0;

const std::string kWindows = "x86_64-windows";
const std::string kLinux = "x86_64-linux";
const std::string kDarwin = "arm64-darwin";

std::string joined(const std::vector<std::string>& lines) {
    std::string all;
    for (size_t i = 0; i < lines.size(); ++i) all += lines[i] + "\n";
    return all;
}

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

std::vector<std::string> split(const std::string& text) {
    std::vector<std::string> out;
    std::string line;
    std::istringstream in(text);
    while (std::getline(in, line)) out.push_back(line);
    return out;
}

std::string join(const std::vector<std::string>& lines) {
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) out += "\n";
        out += lines[i];
    }
    return out;
}

std::string laidOut(const std::string& text) {
    editor::IndentStyle style;
    return join(editor::reindent(split(text), style));
}

void diagnostics() {
    std::printf("cc1's diagnostic\n");

    editor::Diagnostic d = editor::parseDiagnostic(
        "hello.c:12:5: error: expected ';'\n    x = 1\n        ^\n");
    check(d.present, "a plain diagnostic is recognised");
    checkEqual(d.file, "hello.c", "file");
    check(d.line == 12 && d.col == 5, "line and column");
    checkEqual(d.message, "expected ';'", "message");

    // The one that decides how the parser is written: a Windows path begins
    // with a drive letter and a colon, which is not a separator.
    editor::Diagnostic w = editor::parseDiagnostic(
        "C:\\src\\hello.c:7:19: error: unknown type name 'foo'\n");
    check(w.present, "a Windows path is recognised");
    checkEqual(w.file, "C:\\src\\hello.c", "drive letter survives");
    check(w.line == 7 && w.col == 19, "line and column after a drive letter");

    editor::Diagnostic none = editor::parseDiagnostic("cc1: no input files\n");
    check(!none.present, "a message that is not a diagnostic is not one");

    editor::Diagnostic later = editor::parseDiagnostic(
        "cc1: reading foo.c\nfoo.c:3:1: error: stray '@'\n");
    check(later.present && later.line == 3, "a diagnostic after other output");

    // The other spelling. These are cl's own words, copied from a real run on
    // the Windows box rather than written from memory.
    editor::Diagnostic cl = editor::parseDiagnostic(
        "bad.c\nbad.c(3,13): error C2059: syntax error: ';'\n");
    check(cl.present, "cl's diagnostic is recognised");
    checkEqual(cl.file, "bad.c", "cl file");
    check(cl.line == 3 && cl.col == 13, "cl line and column");
    checkEqual(cl.message, "C2059: syntax error: ';'",
               "cl's message drops the word the caller puts back");

    // Without /diagnostics:column cl gives no column at all, and ml64 never
    // does. Column one is a better answer than refusing to read the line.
    editor::Diagnostic noCol = editor::parseDiagnostic("bad.c(3): error C2059: oops\n");
    check(noCol.present && noCol.line == 3 && noCol.col == 1,
          "a diagnostic with no column lands at column one");

    editor::Diagnostic clWin = editor::parseDiagnostic(
        "C:\\src\\bad.c(7,19): error C2065: 'foo': undeclared identifier\n");
    check(clWin.present, "cl with a full path");
    checkEqual(clWin.file, "C:\\src\\bad.c", "cl drive letter survives");
    check(clWin.line == 7 && clWin.col == 19, "cl line and column after a drive letter");

    editor::Diagnostic fatal = editor::parseDiagnostic(
        "bad.c(1): fatal error C1083: Cannot open include file: 'x.h'\n");
    check(fatal.present && fatal.line == 1, "a fatal error is an error");

    editor::Diagnostic warn = editor::parseDiagnostic(
        "bad.c(3,5): warning C4101: unreferenced local variable\n");
    check(!warn.present, "a warning is not what the caret is sent to");

    editor::Diagnostic link = editor::parseDiagnostic(
        "main.obj : error LNK2019: unresolved external symbol foo\n");
    check(!link.present, "a linker error names no line, and is left in the console");
}

void layout() {
    std::printf("layout\n");

    checkEqual(laidOut("int main(void)\n{\nreturn 0;\n}\n"),
               "int main(void)\n{\n    return 0;\n}",
               "a function body goes in one step");

    checkEqual(laidOut("void f(void) {\nif (x) {\ng();\n}\n}\n"),
               "void f(void) {\n    if (x) {\n        g();\n    }\n}",
               "a closing brace settles under the line that opened its group");

    checkEqual(laidOut("void f(void) {\nif (x)\ng();\nh();\n}\n"),
               "void f(void) {\n    if (x)\n        g();\n    h();\n}",
               "an if without braces indents one statement and no more");

    checkEqual(laidOut("void f(void) {\nif (a)\nif (b)\nx();\ny();\n}\n"),
               "void f(void) {\n    if (a)\n        if (b)\n            x();\n    y();\n}",
               "one semicolon closes every head that was waiting");

    checkEqual(laidOut("void f(void) {\nswitch (x) {\ncase 1:\na();\nbreak;\ndefault:\nb();\n}\n}\n"),
               "void f(void) {\n    switch (x) {\n    case 1:\n        a();\n        break;\n"
               "    default:\n        b();\n    }\n}",
               "K&R: a case label sits in its switch's own column");

    checkEqual(laidOut("void f(void) {\nx();\nagain:\ny();\n}\n"),
               "void f(void) {\n    x();\nagain:\n    y();\n}",
               "a goto label steps back out");

    checkEqual(laidOut("void f(void) {\nx = a ? b : c;\n}\n"),
               "void f(void) {\n    x = a ? b : c;\n}",
               "a conditional is not a label");

    checkEqual(laidOut("void f(void) {\n#define N 1\ng();\n}\n"),
               "void f(void) {\n#define N 1\n    g();\n}",
               "the preprocessor lives at the left margin");

    checkEqual(laidOut("void f(void) {\nputs(\"} not a brace {\");\ng();\n}\n"),
               "void f(void) {\n    puts(\"} not a brace {\");\n    g();\n}",
               "braces inside a string are text");

    checkEqual(laidOut("void f(void) {\nc = '{';\ng();\n}\n"),
               "void f(void) {\n    c = '{';\n    g();\n}",
               "a brace in a character constant is text");

    checkEqual(laidOut("void f(void) {\nputs(\"a \\\" and a } \");\ng();\n}\n"),
               "void f(void) {\n    puts(\"a \\\" and a } \");\n    g();\n}",
               "an escaped quote does not end the string");

    checkEqual(laidOut("void f(void) {\n/* a brace } here\n   and here { */\ng();\n}\n"),
               "void f(void) {\n    /* a brace } here\n   and here { */\n    g();\n}",
               "a block comment keeps its own layout and hides its braces");

    checkEqual(laidOut("void f(void) {\ng();  // }\nh();\n}\n"),
               "void f(void) {\n    g();  // }\n    h();\n}",
               "a brace after // is text");

    checkEqual(laidOut("void f(void) {\n\ng();\n}\n"),
               "void f(void) {\n\n    g();\n}",
               "a blank line stays blank");

    checkEqual(laidOut("void f(void) {\n} else {\ng();\n}\n"),
               "void f(void) {\n} else {\n    g();\n}",
               "a line that closes and opens holds its place");
}

void typing() {
    std::printf("what a typed newline becomes\n");

    editor::IndentStyle style;

    std::vector<std::string> lines = split("void f(void) {\n    g();");
    checkEqual(editor::indentAfterNewline(lines, 1, 8, style), "    ",
               "enter inside a body keeps the body's level");

    // Shalimar's hard-won rule, carried over: the brace waiting on the other
    // side of the caret belongs to the line that opened the group.
    std::vector<std::string> pair = split("void f(void) { return 1; }");
    checkEqual(editor::indentAfterNewline(pair, 0, 24, style), "",
               "a } after the caret pulls the new line out a step");

    std::vector<std::string> open = split("void f(void) {");
    checkEqual(editor::indentAfterNewline(open, 0, 14, style), "    ",
               "enter after an opening brace goes in a step");

    std::vector<std::string> head = split("void f(void) {\n    if (x)");
    checkEqual(editor::indentAfterNewline(head, 1, 10, style), "        ",
               "enter after an if with no brace goes in a step");

    std::vector<std::string> deep = split("void f(void) {\n    if (x) {\n        g();");
    checkEqual(editor::indentAfterNewline(deep, 2, 12, style), "        ",
               "enter two levels down stays two levels down");
}

// The colours as letters, so a test can say what it expects and be read.
std::string marks(const std::string& line, editor::Language lang,
                  editor::SyntaxState& state) {
    std::vector<unsigned char> kinds = editor::highlight(line, lang, state);
    std::string out;
    for (size_t i = 0; i < kinds.size(); ++i) {
        switch (kinds[i]) {
            case editor::KindKeyword: out += 'k'; break;
            case editor::KindType:    out += 't'; break;
            case editor::KindString:  out += 's'; break;
            case editor::KindChar:    out += 'q'; break;
            case editor::KindComment: out += 'c'; break;
            case editor::KindPreproc: out += 'p'; break;
            case editor::KindNumber:  out += 'n'; break;
            case editor::KindLabel:   out += 'l'; break;
            default:                  out += '.'; break;
        }
    }
    return out;
}

std::string marks(const std::string& line, editor::Language lang) {
    editor::SyntaxState state;
    return marks(line, lang, state);
}

void colours() {
    // The C++ lists, extended - and the two rules they follow: a word cl at
    // /std:c++14 would refuse is not coloured, and none of this leaks into C.
    {
        editor::SyntaxState state;
        std::string line = "thread_local int n = not_eq_count;";
        std::vector<unsigned char> cpp = editor::highlight(line, editor::LangCpp, state);
        check(cpp[0] == editor::KindKeyword, "thread_local is a C++ keyword");

        editor::SyntaxState plain;
        std::vector<unsigned char> asC = editor::highlight(line, editor::LangC, plain);
        check(asC[0] != editor::KindKeyword, "and is not one in C");

        editor::SyntaxState s2;
        std::vector<unsigned char> alt =
            editor::highlight("if (a and b) return not c;", editor::LangCpp, s2);
        check(alt[7] == editor::KindKeyword, "the alternative tokens are keywords too");

        editor::SyntaxState s3;
        std::vector<unsigned char> lib =
            editor::highlight("std::shared_ptr<thread> p;", editor::LangCpp, s3);
        check(lib[0] == editor::KindType, "a library name is coloured as the type it is");
        check(lib[5] == editor::KindType, "shared_ptr among them");
    }

    std::printf("colouring\n");

    check(editor::languageFor("main.c") == editor::LangC, ".c is C");
    check(editor::languageFor("main.h") == editor::LangC, ".h is C here, not C++");
    check(editor::languageFor("main.cpp") == editor::LangCpp, ".cpp is C++");
    check(editor::languageFor("out.S") == editor::LangAsm, ".S is assembly, whatever its case");
    check(editor::languageFor("README") == editor::LangPlain, "a file with no suffix is text");

    checkEqual(marks("return 0;", editor::LangC),
               "kkkkkk.n.",
               "a keyword and a number");

    checkEqual(marks("int x;", editor::LangC),
               "ttt...",
               "a type");

    checkEqual(marks("x = \"return\";", editor::LangC),
               "....ssssssss.",
               "a keyword inside a string is not a keyword");

    checkEqual(marks("a; // b", editor::LangC),
               "...cccc",
               "a line comment runs to the end");

    checkEqual(marks("#include <stdio.h>", editor::LangC),
               "pppppppp.sssssssss",
               "what an include brings in is a string, not two comparisons");

    checkEqual(marks("c = '\\'';", editor::LangC),
               "....qqqq.",
               "an escaped quote does not end a character constant");

    // The state has to survive the line, exactly as the indenter's does.
    {
        editor::SyntaxState state;
        checkEqual(marks("a; /* open", editor::LangC, state), "...ccccccc",
                   "a block comment starts");
        check(state.comment, "and is still open at the end of the line");
        checkEqual(marks("still inside", editor::LangC, state), "cccccccccccc",
                   "the next line is all comment");
        checkEqual(marks("*/ x;", editor::LangC, state), "cc...",
                   "and it closes");
        check(!state.comment, "the comment is closed after that");
    }

    check(marks("class Foo;", editor::LangCpp).compare(0, 5, "kkkkk") == 0,
          "class is a keyword in C++");
    check(marks("class Foo;", editor::LangC).compare(0, 5, ".....") == 0,
          "and is nothing in particular in C");

    checkEqual(marks("  .globl _factorial", editor::LangAsm),
               "..pppppp...........",
               "an assembler directive");
    checkEqual(marks("_factorial:", editor::LangAsm),
               "lllllllllll",
               "an assembler label");
    checkEqual(marks("  mov x29, sp", editor::LangAsm),
               "..kkk........",
               "the mnemonic is the first word");
}

void routing() {
    std::printf("which compiler gets the file\n");

    editor::Toolchain automatic;   // ToolAuto by default

    check(editor::resolve(automatic, editor::LangC) == editor::ToolCc1,
          "C goes to cc1, which is what this editor is for");
    check(editor::resolve(automatic, editor::LangCpp) == editor::ToolMsvc,
          "C++ goes to cl, because cc1 compiles C");
    check(editor::resolve(automatic, editor::LangPlain) == editor::ToolCc1,
          "anything else falls to cc1, and is refused there rather than here");

    // A choice made by hand is kept, even when it is the wrong one - the editor
    // says why rather than quietly doing something else.
    editor::Toolchain byHand;
    byHand.kind = editor::ToolCc1;
    check(editor::resolve(byHand, editor::LangCpp) == editor::ToolCc1,
          "a hand-picked compiler is not overridden");

    check(!editor::canCompile(editor::ToolCc1, editor::LangCpp),
          "cc1 cannot take C++");
    check(editor::canCompile(editor::ToolMsvc, editor::LangCpp), "cl can");
    check(editor::canCompile(editor::ToolMsvc, editor::LangC), "cl takes C as well");
    check(editor::canCompile(editor::ToolCc1, editor::LangC), "and so does cc1");
    check(!editor::canCompile(editor::ToolCc1, editor::LangAsm),
          "assembly is shown, not compiled");

    check(editor::usesArch(editor::ToolCc1), "cc1 generates for three architectures");
    check(!editor::usesArch(editor::ToolMsvc),
          "cl generates for its own host, so no target is offered");

    // The flags each compiler is given for each language.
    editor::Toolchain tool;
    std::string cpp = editor::shownCommand(tool, editor::ToolMsvc, "a.cpp",
                                           editor::LangCpp, "x86_64-windows",
                                           editor::ConfigDebug);
    check(cpp.find("/TP") != std::string::npos, "C++ is compiled as C++, and said so");
    check(cpp.find("/EHsc") != std::string::npos, "with exceptions turned on");

    std::string c = editor::shownCommand(tool, editor::ToolMsvc, "a.c",
                                         editor::LangC, "x86_64-windows",
                                         editor::ConfigDebug);
    check(c.find("/TC") != std::string::npos, "C is compiled as C");
    check(c.find("/TP") == std::string::npos, "and not as C++");

    // Debug and release, and an honest account of what each compiler can do
    // about them.
    check(editor::configFlags(editor::ToolMsvc, editor::ConfigDebug, kWindows).find("/Od") !=
              std::string::npos,
          "cl's debug turns the optimiser off");
    check(editor::configFlags(editor::ToolMsvc, editor::ConfigRelease, kWindows).find("/O2") !=
              std::string::npos,
          "and its release turns it up");
    check(editor::configFlags(editor::ToolMsvc, editor::ConfigRelease, kWindows).find("NDEBUG") !=
              std::string::npos,
          "with NDEBUG defined alongside");

    // cc1 has no -O, so release is the define and nothing else. Debug is the
    // define plus -g on the two targets cc1 writes DWARF for, and the define
    // alone on the one it does not - where passing -g would be refused.
    std::string linuxDebug = editor::configFlags(editor::ToolCc1, editor::ConfigDebug, kLinux);
    std::string darwinDebug = editor::configFlags(editor::ToolCc1, editor::ConfigDebug, kDarwin);
    std::string winDebug = editor::configFlags(editor::ToolCc1, editor::ConfigDebug, kWindows);
    std::string cc1Release = editor::configFlags(editor::ToolCc1, editor::ConfigRelease, kLinux);

    check(linuxDebug.find("_DEBUG") != std::string::npos, "cc1's debug defines _DEBUG");
    check(cc1Release.find("NDEBUG") != std::string::npos, "and its release defines NDEBUG");
    check(linuxDebug.find("-g") != std::string::npos, "debug asks x86_64-linux for -g");
    check(darwinDebug.find("-g") != std::string::npos, "and arm64-darwin for -g as well");
    check(winDebug.find("-g") == std::string::npos,
          "but not x86_64-windows, whose MASM carries no line table");
    check(winDebug.find("_DEBUG") != std::string::npos,
          "which still gets the define, since that is what assert reads");

    // A release build is never given -g on any target: debug information is
    // what debug means here, and release means its absence.
    check(editor::configFlags(editor::ToolCc1, editor::ConfigRelease, kLinux).find("-g") ==
              std::string::npos &&
          editor::configFlags(editor::ToolCc1, editor::ConfigRelease, kDarwin).find("-g") ==
              std::string::npos,
          "and release asks for -g nowhere");
    check(linuxDebug.find("-O") == std::string::npos &&
              darwinDebug.find("-O") == std::string::npos &&
              cc1Release.find("-O") == std::string::npos,
          "no configuration passes a -O, which cc1 still has not got");

    check(editor::emitsDebugInfo(editor::ToolCc1, kLinux) &&
              editor::emitsDebugInfo(editor::ToolCc1, kDarwin),
          "cc1 writes DWARF for two of its three targets");
    check(!editor::emitsDebugInfo(editor::ToolCc1, kWindows), "and not for the third");
    // cl is the other half of this machine's story, and not in the same
    // position: the C file goes to cc1 and carries no line table, while the
    // C++ file goes to cl, which writes CodeView into a .pdb and always could.
    check(editor::emitsDebugInfo(editor::ToolMsvc, kWindows),
          "cl writes debug information for the target it builds for");
    check(editor::configFlags(editor::ToolMsvc, editor::ConfigDebug, kWindows)
              .find("/Zi") != std::string::npos,
          "and a debug build asks it for that, not only for /Od");
    check(editor::configFlags(editor::ToolMsvc, editor::ConfigRelease, kWindows)
              .find("/Zi") == std::string::npos,
          "while a release build does not");

    // The words the panel says are the core's, and they follow the target
    // rather than being written out once and left.
    std::vector<std::string> carries = editor::debugNote(editor::ToolCc1, kDarwin);
    std::vector<std::string> carriesNot = editor::debugNote(editor::ToolCc1, kWindows);
    check(!carries.empty() && !carriesNot.empty(), "the panel is told something either way");
    check(carries != carriesNot, "and not the same thing about both targets");
    check(joined(carries).find("DWARF") != std::string::npos &&
              joined(carries).find(kDarwin) != std::string::npos,
          "the target that carries DWARF is said to, by name");
    check(joined(carriesNot).find("no debug information") != std::string::npos,
          "and the one that does not is said not to");

    // Compiling is one question and running is another. Every target compiles
    // to assembly anywhere; only the host's own goes on to a program, because
    // the assembler and linker cc1 hands off to are this machine's.
    std::string host = editor::hostArch();
    check(host == kWindows || host == kLinux || host == kDarwin,
          "the host is one of the three targets");
    check(editor::runsHere(editor::ToolCc1, host), "and what it builds for itself runs here");

    std::string elsewhere = (host == kLinux) ? kDarwin : kLinux;
    check(!editor::runsHere(editor::ToolCc1, elsewhere), "what it builds for elsewhere does not");
    check(editor::whyNotRun(editor::ToolCc1, host).empty(), "so there is nothing to explain");

    std::string why = editor::whyNotRun(editor::ToolCc1, elsewhere);
    check(why.find(elsewhere) != std::string::npos && why.find(host) != std::string::npos,
          "and when there is, it names both the target and the one to switch to");

    const std::string every[3] = {kWindows, kLinux, kDarwin};
    for (size_t i = 0; i < 3; ++i)
        check(editor::whyNotRun(editor::ToolCc1, every[i]).size() < 80,
              "in a line the status bar can show whole - " + every[i]);

    // cl builds for the machine it was installed on and is handed no target at
    // all, so the target menu cannot make it unrunnable.
    check(editor::runsHere(editor::ToolMsvc, elsewhere), "cl builds for its own host either way");

    // The recipe that makes a program rather than assembly: cc1 with neither -S
    // nor -c links one, and cl does when it is not given /c.
    editor::Recipe program = editor::programRecipe(tool, editor::ToolCc1, "a.c",
                                                   editor::LangC, host, editor::ConfigDebug);
    check(program.command.find(" -S") == std::string::npos, "the program recipe passes no -S");
    check(program.command.find(" -c") == std::string::npos, "and no -c");
    check(program.command.find("-o") != std::string::npos, "and names what to make");
    check(program.command.find("a.c") != std::string::npos, "out of the file being edited");
    check(!program.assemblyPath.empty(), "and says where it put it");

    // Named for the editor that built it. It used to be one fixed name, so two
    // editors - or an editor and this suite - wrote to the same file.
    size_t named = program.assemblyPath.find("ed1-run-");
    check(named != std::string::npos, "and gives it a name of this editor's own");
    check(named + 8 < program.assemblyPath.size() &&
              program.assemblyPath[named + 8] >= '0' && program.assemblyPath[named + 8] <= '9',
          "with the number that tells one editor's from another's");
    check(editor::emitsDebugInfo(editor::ToolCc1, host) ==
              (program.command.find("-g") != std::string::npos),
          "and asks for -g exactly when the target can carry it");

    editor::Recipe clProgram = editor::programRecipe(tool, editor::ToolMsvc, "a.cpp",
                                                     editor::LangCpp, kWindows,
                                                     editor::ConfigDebug);
    check(clProgram.command.find(" /c ") == std::string::npos,
          "cl is not told to stop at an object");
    check(clProgram.command.find("/Fe") != std::string::npos, "and is told what to call the program");
    check(clProgram.command.find("/TP") != std::string::npos, "and that this one is C++");
    check(clProgram.command.find("/link /DEBUG") != std::string::npos,
          "and the linker is told as well, since /Zi only describes the object");
    check(clProgram.leftovers.size() == 3,
          "and the object, the .pdb and the .ilk are all cleared up after");

    // C++14, which is what this arena holds itself to - the editor tells cl so
    // rather than leaving it on whatever that compiler defaults to.
    check(clProgram.command.find("/std:c++14") != std::string::npos,
          "and that C++ here means C++14");

    check(editor::optimises(editor::ToolMsvc), "cl optimises");
    check(!editor::optimises(editor::ToolCc1), "cc1 does not, and does not pretend to");

    std::string release = editor::shownCommand(tool, editor::ToolCc1, "a.c",
                                               editor::LangC, kDarwin,
                                               editor::ConfigRelease);
    check(release.find("-DNDEBUG=1") != std::string::npos,
          "and the define reaches the command line");

    // The flag has to survive the whole way to what is actually run, not just
    // to what is shown.
    std::string shownDebug = editor::shownCommand(tool, editor::ToolCc1, "a.c",
                                                  editor::LangC, kDarwin,
                                                  editor::ConfigDebug);
    check(shownDebug.find("-g") != std::string::npos, "and -g reaches it too");
    check(editor::assemblyRecipe(tool, editor::ToolCc1, "a.c", editor::LangC,
                                 kDarwin, editor::ConfigDebug)
              .command.find("-g") != std::string::npos,
          "and reaches the command that is run, not only the one that is shown");
}

void multiByte() {
    std::printf("characters that take more than one byte\n");

    // "café" - the e-acute is two bytes. "سلام" - four Arabic letters, two
    // bytes each. "中文" - two Chinese characters, three bytes each and two
    // columns each.
    const std::string cafe = "caf\xc3\xa9";
    const std::string salam = "\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85";
    const std::string chinese = "\xe4\xb8\xad\xe6\x96\x87";

    check(editor::utf8::lengthFrom('a') == 1, "ASCII is one byte");
    check(editor::utf8::lengthFrom(0xC3) == 2, "a two-byte lead says two");
    check(editor::utf8::lengthFrom(0xE4) == 3, "a three-byte lead says three");
    check(editor::utf8::lengthFrom(0xF0) == 4, "a four-byte lead says four");

    check(cafe.size() == 5 && editor::utf8::count(cafe) == 4,
          "five bytes, four characters");
    check(salam.size() == 8 && editor::utf8::count(salam) == 4,
          "and eight bytes, four letters");

    // Moving over it lands on boundaries and nowhere else.
    check(editor::utf8::next(cafe, 3) == 5, "stepping over the accented letter");
    check(editor::utf8::previous(cafe, 5) == 3, "and back over it");
    check(editor::utf8::startOf(cafe, 4) == 3, "a caret inside one is pulled to its start");

    size_t at = 0, steps = 0;
    while (at < salam.size()) { at = editor::utf8::next(salam, at); ++steps; }
    check(steps == 4, "four steps cross four letters");

    // Columns are not bytes and not characters either.
    check(editor::utf8::columns(cafe, cafe.size()) == 4, "café takes four columns");
    check(editor::utf8::columns(salam, salam.size()) == 4, "and so does سلام");
    check(editor::utf8::columns(chinese, chinese.size()) == 4,
          "two Chinese characters take four");
    check(editor::utf8::widthOf(0x4E2D) == 2, "a Chinese character is two columns wide");
    check(editor::utf8::widthOf(0x0633) == 1, "an Arabic letter is one");
    check(editor::utf8::widthOf(0x064E) == 0, "and a mark drawn on top is none");

    // Rubbish must not wedge anything: every step has to move forward.
    std::string broken = "a\x80\x80z";
    size_t walked = 0, guard = 0;
    while (walked < broken.size() && guard < 100) {
        size_t step = editor::utf8::next(broken, walked);
        check(step > walked, "stepping through malformed bytes always moves on");
        walked = step;
        ++guard;
    }
}

void ranges() {
    std::printf("stretches of text\n");

    editor::Buffer buf;
    size_t endRow = 0, endCol = 0;
    buf.insertText(0, 0, "one\ntwo\nthree", endRow, endCol);
    check(buf.lineCount() == 3, "text with newlines in it becomes lines");
    check(endRow == 2 && endCol == 5, "and says where it ended");

    editor::Range within = editor::ordered(0, 1, 0, 3);
    checkEqual(buf.textIn(within), "ne", "a stretch inside one line");

    editor::Range across = editor::ordered(0, 1, 2, 3);
    checkEqual(buf.textIn(across), "ne\ntwo\nthr", "and one across three");

    // Given backwards, it is the same stretch.
    editor::Range backwards = editor::ordered(2, 3, 0, 1);
    checkEqual(buf.textIn(backwards), "ne\ntwo\nthr", "a selection made backwards");

    editor::Buffer cut = buf;
    cut.eraseRange(across);
    check(cut.lineCount() == 1, "erasing across lines joins what is left");
    checkEqual(cut.line(0), "oee", "of the first and the last");

    editor::Buffer inside = buf;
    inside.eraseRange(within);
    checkEqual(inside.line(0), "o", "and erasing within a line leaves the rest");
}

void undoing() {
    std::printf("going back, and forward again\n");

    editor::Buffer buf;
    size_t cx = 0, cy = 0;

    // A run of typing is one step, not one per letter.
    const char* word = "hello";
    for (size_t i = 0; word[i]; ++i) {
        buf.beginEdit(editor::EditTyping, cx, cy);
        buf.insertChar(0, cx, word[i]);
        ++cx;
    }
    checkEqual(buf.line(0), "hello", "five letters typed");
    check(buf.undoDepth() == 1, "are one step, not five");

    check(buf.undo(cx, cy), "and one undo");
    checkEqual(buf.line(0), "", "takes the word back");
    check(cx == 0 && cy == 0, "and the caret with it");

    check(buf.redo(cx, cy), "redo puts it back");
    checkEqual(buf.line(0), "hello", "text and all");
    check(cx == 5, "with the caret where it was");

    // Moving the caret ends the run, so what comes next is its own step.
    buf.breakRun();
    buf.beginEdit(editor::EditTyping, cx, cy);
    buf.insertChar(0, 5, '!');
    check(buf.undoDepth() == 2, "after moving, the next typing is a new step");
    check(buf.undo(cx, cy), "which undoes on its own");
    checkEqual(buf.line(0), "hello", "leaving what came before it");

    // A different kind of change is always its own step.
    buf.beginEdit(editor::EditOther, 5, 0);
    buf.splitLine(0, 5);
    check(buf.lineCount() == 2, "a line is split");
    check(buf.undo(cx, cy) && buf.lineCount() == 1, "and undoing joins it back");

    // Doing something new throws away what was undone.
    check(buf.canRedo(), "there is something to redo");
    buf.beginEdit(editor::EditOther, 0, 0);
    buf.insertChar(0, 0, 'x');
    check(!buf.canRedo(), "until something else is done");

    // Nothing to undo is not a failure, it is an answer.
    editor::Buffer fresh;
    size_t fx = 0, fy = 0;
    check(!fresh.undo(fx, fy), "an untouched buffer has nothing to undo");
    check(!fresh.redo(fx, fy), "and nothing to redo");

    // The history is capped, and going past the cap does not break it.
    editor::Buffer many;
    size_t mx = 0, my = 0;
    for (int i = 0; i < 150; ++i) {
        many.beginEdit(editor::EditOther, mx, my);
        many.insertChar(0, 0, 'a');
    }
    check(many.undoDepth() == 100, "the history stops at a hundred steps");
    check(many.undo(mx, my), "and still undoes");
    check(many.line(0).size() == 149, "one step at a time");
}

void savedState() {
    std::printf("knowing when the file matches the disk\n");

    file::path dir = file::temp_directory_path() / "ed1-saved-test";
    file::remove_all(dir);
    file::create_directories(dir);

    editor::Buffer buf;
    buf.setPath((dir / "saved.c").string());
    size_t cx = 0, cy = 0;
    std::string error;

    buf.beginEdit(editor::EditTyping, cx, cy);
    buf.insertChar(0, 0, 'a');
    check(buf.dirty(), "typing makes it modified");

    check(buf.save(error), "it saves");
    check(!buf.dirty(), "and is not modified once written");

    check(buf.undo(cx, cy), "undoing past the save");
    check(buf.dirty(), "makes it modified again - the disk says otherwise");

    check(buf.redo(cx, cy), "and coming back");
    check(!buf.dirty(), "makes it match the disk once more");

    // A change after a save is its own step, so undoing it lands exactly on
    // what was written.
    buf.beginEdit(editor::EditTyping, cx, cy);
    buf.insertChar(0, 0, 'b');
    check(buf.dirty(), "a change after saving shows as modified");
    check(buf.undo(cx, cy) && !buf.dirty(),
          "and undoing it shows as saved, without writing anything");

    // Saving further along moves the mark with it.
    buf.beginEdit(editor::EditTyping, cx, cy);
    buf.insertChar(0, 0, 'c');
    check(buf.save(error), "saving again");
    check(!buf.dirty(), "clears it");
    check(buf.undo(cx, cy) && buf.dirty(), "and undo past the newer save is modified");

    // When the saved point falls off the end of the capped history it cannot
    // be recognised again, and the safe answer is 'modified'.
    editor::Buffer long_;
    long_.setPath((dir / "long.c").string());
    size_t lx = 0, ly = 0;
    check(long_.save(error), "an empty file is written");
    check(!long_.dirty(), "and is unmodified");
    for (int i = 0; i < 150; ++i) {
        long_.beginEdit(editor::EditOther, lx, ly);
        long_.insertChar(0, 0, 'z');
    }
    while (long_.canUndo()) long_.undo(lx, ly);
    check(long_.dirty(),
          "undoing to the bottom of a capped history still says modified");

    file::remove_all(dir);
}

void searching() {
    std::printf("finding and replacing\n");

    std::vector<std::string> lines = split(
        "int one(void) { return 1; }\n"
        "int two(void) { return 2; }\n"
        "int one_more(void) { return 3; }");

    editor::Match first = editor::findNext(lines, "one", 0, 0);
    check(first.found && first.row == 0 && first.col == 4, "the first one is found");

    editor::Match second = editor::findNext(lines, "one", first.row, first.col + 1);
    check(second.found && second.row == 2 && second.col == 4,
          "and the next is the one after it");

    // Round the end and back to where it started, once.
    editor::Match wrapped = editor::findNext(lines, "one", second.row, second.col + 1);
    check(wrapped.found && wrapped.row == 0, "searching wraps to the top");

    editor::Match missing = editor::findNext(lines, "nowhere", 0, 0);
    check(!missing.found, "what is not there is not found");
    check(!editor::findNext(lines, "", 0, 0).found, "and nothing is not searched for");

    editor::Match back = editor::findPrevious(lines, "one", 2, 4);
    check(back.found && back.row == 0, "and it goes backwards too");

    editor::Match onlyOne = editor::findNext(lines, "two", 1, 4);
    check(onlyOne.found && onlyOne.row == 1 && onlyOne.col == 4,
          "a match under the caret is found where it is");

    std::vector<std::string> changed = lines;
    check(editor::replaceAll(changed, "return", "give back") == 3, "every one is replaced");
    check(changed[0].find("give back 1") != std::string::npos, "and the text is right");

    // The one that could go round for ever if the replacement were searched
    // again.
    std::vector<std::string> growing = split("aaa");
    check(editor::replaceAll(growing, "a", "aa") == 3, "a replacement holding the needle");
    checkEqual(growing[0], "aaaaaa", "grows once and stops");

    std::vector<std::string> untouched = split("nothing here");
    check(editor::replaceAll(untouched, "absent", "x") == 0, "nothing found, nothing changed");
    checkEqual(untouched[0], "nothing here", "and the line is as it was");
}

void jsonReading() {
    std::printf("the project file's format\n");

    std::string why;
    editor::Json one = editor::Json::parse(
        "{\"name\": \"Editor\", \"indent\": {\"width\": 4, \"tabs\": false},"
        " \"groups\": [\"a\", \"b\"]}", why);
    check(why.empty(), "a plain object reads");
    checkEqual(one.get("name").text(), "Editor", "a string member");
    check(one.get("indent").get("width").integer() == 4, "a number inside an object");
    check(one.get("indent").get("tabs").boolean() == false, "a false");
    check(one.get("groups").size() == 2, "an array's length");
    checkEqual(one.get("groups").at(1).text(), "b", "an array's contents");

    check(one.get("missing").text("fallback") == "fallback",
          "what is not there gives the default back");

    // Comments are not JSON, and are allowed on purpose: this is a file people
    // open and edit, and people leave notes in files they edit.
    editor::Json noted = editor::Json::parse(
        "{\n  // which compiler\n  \"toolchain\": \"auto\"\n}", why);
    check(why.empty() && noted.get("toolchain").text() == "auto",
          "a comment is skipped rather than refused");

    std::string broken;
    editor::Json::parse("{\"a\": }", broken);
    check(!broken.empty(), "a malformed file says what went wrong");

    editor::Json::parse("{} and then some", broken);
    check(!broken.empty(), "text after the end is an error too");

    // What goes out must read back as what went in.
    editor::Json out = editor::Json::object();
    out.set("name", editor::Json::fromText("has \"quotes\" in it"));
    out.set("width", editor::Json::fromNumber(4));
    editor::Json back = editor::Json::parse(out.write(), why);
    check(why.empty(), "what it writes, it can read");
    checkEqual(back.get("name").text(), "has \"quotes\" in it", "quotes survive the trip");
    check(out.write().find("4") != std::string::npos &&
              out.write().find("4.0") == std::string::npos,
          "a whole number is written whole");
}

void projects() {
    std::printf("the project\n");

    file::path dir =
        file::temp_directory_path() / "ed1-project-test";
    file::remove_all(dir);
    file::create_directories(dir);

    editor::Project made;
    made.begin(dir.string(), "Trial");
    check(made.loaded(), "a project begun is a project loaded");
    check(made.groups().size() == 1, "and starts with one group");

    check(made.addFile("src/main.c", "Sources"), "a file is added");
    check(!made.addFile("src/main.c", "Sources"), "and not added twice");
    check(made.addFile("src/util.c", "Sources"), "another one");
    check(made.addFile("docs/notes.txt", "Notes"), "a file into a group not yet there");
    check(made.groups().size() == 2, "which makes the group");

    check(made.groupOf("docs/notes.txt") < made.groups().size(), "and it is findable");
    check(made.moveToGroup("docs/notes.txt", "Sources"), "regrouping moves it");
    checkEqual(made.groups()[made.groupOf("docs/notes.txt")].name, "Sources",
               "to the group asked for");

    check(made.renameFile("src/util.c", "src/helper.c"), "renaming follows it");
    check(made.groupOf("src/util.c") == made.groups().size(), "the old name is gone");
    check(made.groupOf("src/helper.c") < made.groups().size(), "the new one is there");

    check(made.removeFile("src/helper.c"), "removing takes it out of the list");
    check(made.groupOf("src/helper.c") == made.groups().size(), "and it is not found after");

    // Two levels and no more. A structure nobody has to explore is one anyone
    // can read, so this is a rule the project keeps rather than a convention.
    std::string why;
    check(editor::Project::allows("main.c", why), "a file at the root");
    check(editor::Project::allows("src/main.c", why), "and one directory down");
    check(!editor::Project::allows("src/deep/main.c", why), "but no deeper");
    check(!why.empty(), "and it says why");
    check(!editor::Project::allows("/etc/passwd", why), "nothing absolute");
    check(!editor::Project::allows("../outside.c", why), "and no going up and out");
    check(!editor::Project::allows("src/", why), "a directory is not a file");

    check(!made.addFile("a/b/c.c", "Sources"), "a file too deep is not added");

    // Depth is limited; width is not. As many directories as a project wants
    // may sit side by side on the ground floor.
    {
        editor::Project wide;
        wide.begin(dir.string(), "Wide");
        const char* dirs[7] = {"src", "tests", "examples", "docs", "tools",
                               "extra", "more"};
        for (int i = 0; i < 7; ++i)
            check(wide.addFile(std::string(dirs[i]) + "/a.c", "Sources"),
                  std::string("directory ") + dirs[i] + " sits alongside the rest");
        check(wide.directories().size() == 7, "seven of them, and none refused");
        check(wide.addFile("root.c", "Sources"), "a file on the ground floor too");
        check(!wide.addFile("src/deeper/a.c", "Sources"), "but still nothing two deep");
    }

    check(made.addFile("src/ok.c", "Sources"), "one at the right depth is");
    check(!made.renameFile("src/ok.c", "a/b/c.c"), "nor renamed into somewhere too deep");
    check(made.removeFile("src/ok.c"), "tidy that away again");

    editor::IndentStyle style;
    style.width = 2;
    style.tabs = true;
    made.setIndent(style);
    made.setToolchain(editor::ToolMsvc);
    made.setConfig(editor::ConfigRelease);

    std::string error;
    check(made.save(error), "it writes itself out");
    check(error.empty(), "with nothing to report");

    // And the file that comes back is the project that went in.
    editor::Project read;
    check(read.load(dir.string(), error), "and reads back");
    checkEqual(read.name(), "Trial", "the name survives");
    check(read.indent().width == 2 && read.indent().tabs, "the layout settings survive");
    check(read.groups()[0].name == "Sources", "and the groups keep their order");
    check(read.toolchain() == editor::ToolMsvc, "the compiler choice survives");
    check(read.config() == editor::ConfigRelease, "and so does the configuration");
    check(read.groups().size() == 2, "the groups survive");
    check(read.groupOf("src/main.c") < read.groups().size(), "and what is in them");

    // A directory with no project file is not a failure - it means there is no
    // project, and the pane shows the directory instead.
    file::path bare = dir / "empty";
    file::create_directories(bare);
    editor::Project none;
    check(!none.load(bare.string(), error), "a directory with no project file");
    check(error.empty(), "is not an error");

    // The smallest file that works. Everything has a default, so an empty
    // object is a valid project.
    file::path tiny = dir / "tiny";
    file::create_directories(tiny);
    { std::ofstream f((tiny / "ed1.json").string().c_str()); f << "{}\n"; }
    editor::Project small;
    check(small.load(tiny.string(), error), "an empty object is a project");
    check(error.empty() && small.indent().width == 4 && !small.indent().tabs,
          "and every setting falls back to its default");
    check(small.config() == editor::ConfigDebug,
          "debug being the one you want while the code is still being written");

    file::remove_all(dir);
}

void operations() {
    std::printf("changing what the project holds\n");

    file::path dir = file::temp_directory_path() / "ed1-workspace-test";
    file::remove_all(dir);
    file::create_directories(dir);

    editor::Project project;
    project.begin(dir.string(), "Work");

    // Making a file: on disk, in the project, and the project written back.
    editor::Outcome made = editor::createFile(project, "src/one.c", "Sources");
    check(made.ok, "a file is made");
    check(file::exists(dir / "src" / "one.c"), "and it is there on disk");
    check(project.groupOf("src/one.c") < project.groups().size(), "and in the project");
    check(file::exists(dir / "ed1.json"), "and the project was written");
    check(!made.path.empty(), "and it says where the file went");

    check(!editor::createFile(project, "src/one.c", "Sources").ok, "twice is refused");
    editor::Outcome deep = editor::createFile(project, "a/b/c.c", "Sources");
    check(!deep.ok, "and so is anything two directories down");
    check(deep.message.find("two levels") != std::string::npos, "with the rule as the reason");
    check(!file::exists(dir / "a"), "and nothing was written for it");

    // Renaming follows on disk and in the list.
    editor::Outcome moved =
        editor::renameFile(project, (dir / "src" / "one.c").string(), "src/two.c");
    check(moved.ok, "renaming works");
    check(!file::exists(dir / "src" / "one.c"), "the old name is gone");
    check(file::exists(dir / "src" / "two.c"), "the new one is there");
    check(project.groupOf("src/two.c") < project.groups().size(), "and the project followed");

    // Regrouping changes the lists and nothing else.
    editor::Outcome grouped =
        editor::moveToGroup(project, (dir / "src" / "two.c").string(), "Extras");
    check(grouped.ok, "regrouping works");
    checkEqual(project.groups()[project.groupOf("src/two.c")].name, "Extras",
               "into the group asked for");
    check(file::exists(dir / "src" / "two.c"), "and the file has not moved");

    // Adding something that already exists.
    { std::ofstream f((dir / "src" / "three.c").string().c_str()); f << "int three;\n"; }
    check(editor::addExisting(project, (dir / "src" / "three.c").string(), "Sources").ok,
          "a file already on disk can be added");
    check(!editor::addExisting(project, (dir / "src" / "three.c").string(), "Sources").ok,
          "but not twice");

    // Deleting.
    editor::Outcome gone = editor::deleteFile(project, (dir / "src" / "two.c").string());
    check(gone.ok, "deleting works");
    check(!file::exists(dir / "src" / "two.c"), "the file is gone");
    check(project.groupOf("src/two.c") == project.groups().size(), "and so is the entry");

    // And what was written survives being read again.
    editor::Project again;
    std::string error;
    check(again.load(dir.string(), error), "the project reads back");
    check(again.groupOf("src/three.c") < again.groups().size(), "with what was added to it");
    check(again.groupOf("src/two.c") == again.groups().size(), "and without what was deleted");

    // With no project file there is nothing to write, and the disk work still
    // stands - which is what lets these run before anyone has made a project.
    file::path bare = dir / "bare";
    file::create_directories(bare);
    editor::Project none;
    none.setRoot(bare.string());
    editor::Outcome loose = editor::createFile(none, "loose.c", "Sources");
    check(loose.ok, "a file can be made without a project");
    check(file::exists(bare / "loose.c"), "and it is really there");
    check(!file::exists(bare / "ed1.json"), "and no project file was invented");

    file::remove_all(dir);
}

void whatTheBuildMade() {
    std::printf("reading a build out of its assembly\n");

    // cc1's own output, in the GNU spelling it uses for arm64 and Linux.
    std::vector<std::string> gnu = split(
        ".L.str.0:\n"
        "  .ascii \"as expected\\000\"\n"
        "  .globl _main\n"
        "_factorial:\n"
        "  stp x29, x30, [sp, #-16]!\n"
        "  mov x9, #16\n"
        "  sub sp, sp, x9\n"
        "L.factorial.end.0:\n"
        "  ret\n");

    std::vector<editor::Symbol> gnuFound = editor::symbolsIn(gnu);

    int functions = 0, exported = 0, strings = 0;
    std::string frame;
    for (size_t i = 0; i < gnuFound.size(); ++i) {
        if (gnuFound[i].kind == editor::SymbolFunction) {
            ++functions;
            if (gnuFound[i].name == "_factorial") frame = gnuFound[i].detail;
        }
        if (gnuFound[i].kind == editor::SymbolExported) ++exported;
        if (gnuFound[i].kind == editor::SymbolText) ++strings;
    }
    check(functions == 1, "a function is found in the GNU spelling");
    check(exported == 1, "and what is exported");
    check(strings == 1, "and a string");
    checkEqual(frame, "stack 16 bytes",
               "and the stack it takes, though arm64 puts the number in a register first");

    // The compiler's own labels are not symbols anyone asked for.
    for (size_t i = 0; i < gnuFound.size(); ++i)
        check(gnuFound[i].name.compare(0, 2, "L.") != 0,
              "the compiler's own labels are left out");

    // MASM, which cc1 writes for Windows and cl writes always.
    std::vector<std::string> masm = split(
        "PUBLIC main\n"
        "EXTERN puts:PROC\n"
        "$_L_str_1 DB 115, 111, 109, 101, 116, 104, 105, 110, 103, 32, 105, 115, 32, 119\n"
        "  DB 114, 111, 110, 103, 0\n"
        "factorial PROC FRAME\n"
        "  sub rsp, 16\n"
        "  .ENDPROLOG\n"
        "factorial ENDP\n");

    std::vector<editor::Symbol> masmFound = editor::symbolsIn(masm);

    std::string said, stack;
    bool sawExternal = false;
    for (size_t i = 0; i < masmFound.size(); ++i) {
        if (masmFound[i].kind == editor::SymbolText) said = masmFound[i].detail;
        if (masmFound[i].kind == editor::SymbolExternal) sawExternal = true;
        if (masmFound[i].kind == editor::SymbolFunction) stack = masmFound[i].detail;
    }
    checkEqual(said, "something is wrong",
               "a string broken across two DB lines is put back together");
    check(sawExternal, "what is called but not defined is found");
    checkEqual(stack, "stack 16 bytes", "and the stack a MASM function takes");

    // cl's own way of writing a string.
    std::vector<std::string> cl = split("$SG5346 DB 'first is %d', 0aH, 00H\n");
    std::vector<editor::Symbol> clFound = editor::symbolsIn(cl);
    check(clFound.size() == 1 && clFound[0].detail.compare(0, 8, "first is") == 0,
          "and cl's quoted form of the same thing");

    // Nothing in, nothing claimed.
    std::vector<std::string> nothing;
    check(editor::symbolsIn(nothing).empty(), "no assembly means no symbols");
    check(!editor::describe(editor::symbolsIn(nothing)).empty(),
          "and it says so rather than showing a blank");
}

}  // namespace

// The paths, which used to be std::filesystem's business and are now this
// project's. Everything above this leans on them, so they are worth pinning
// down on their own rather than only through what uses them.
void paths() {
    std::printf("paths, without <filesystem>\n");

    namespace p = editor::path;

    check(p::withSlashes("a\\b\\c") == "a/b/c", "backslashes are turned round on the way in");
    check(p::join("a", "b") == "a/b", "joining puts one slash between");
    check(p::join("a/", "b") == "a/b", "and not two when there is one already");
    check(p::join("", "b") == "b", "and none in front of nothing");
    check(p::filename("a/b/c.c") == "c.c", "the name is what is after the last slash");
    check(p::filename("c.c") == "c.c", "or the whole of it when there is none");
    check(p::parent("a/b/c.c") == "a/b", "the parent is what is before it");
    check(p::parent("c.c").empty(), "and nothing when there is nothing before it");
    check(p::parent("/c.c") == "/", "the root being its own parent's whole name");

    // . and .. are taken out the way a filesystem takes them out, and a path
    // already absolute is left where it is.
    check(p::absolute("/a/b/../c") == "/a/c", "'..' cancels the name before it");
    check(p::absolute("/a/./b") == "/a/b", "and '.' cancels itself");
    check(p::absolute("/a//b") == "/a/b", "a doubled slash is one slash");
    check(p::absolute("/a/b/") == "/a/b", "and a trailing one is none");

    // What a project file holds: the way from the project's directory to a
    // file in it, which is the one thing here with real work in it.
    check(p::relativeTo("/w/src/one.c", "/w") == "src/one.c", "down into the project");
    check(p::relativeTo("/w/one.c", "/w") == "one.c", "or straight into it");
    check(p::relativeTo("/w", "/w") == ".", "a directory against itself is here");
    check(p::relativeTo("/w/one.c", "/w/src") == "../one.c", "and up when it has to be");
    check(p::relativeTo("/a/one.c", "/b/deep/er") == "../../../a/one.c",
          "up as many times as it takes, then down");

    // On disk. A directory made several deep at once, a file moved, a file
    // taken away, and the whole lot removed at the end.
    std::string dir = p::join(p::tempDir(), "ed1-path-test");
    p::removeTree(dir);
    check(!p::exists(dir), "the temporary directory starts absent");

    check(p::makeDirectories(p::join(dir, "one/two/three")),
          "every directory on the way is made");
    check(p::isDirectory(p::join(dir, "one/two/three")), "and the last of them is there");
    check(p::makeDirectories(p::join(dir, "one/two")), "making one twice is not a failure");

    std::string file = p::join(dir, "one/two/three/a.c");
    FILE* made = std::fopen(file.c_str(), "wb");
    if (made) std::fclose(made);
    check(p::exists(file), "a file written into it is there");
    check(!p::isDirectory(file), "and is not a directory");

    std::string moved = p::join(dir, "one/two/three/b.c");
    check(p::rename(file, moved), "it can be renamed");
    check(!p::exists(file) && p::exists(moved), "which takes the old name away");

    // One name for a file, whatever spelling it arrives in. This is what keeps
    // one file to one tab, and one file to one set of breakpoints.
    check(p::same(moved, p::withSlashes(moved)), "a path is the same file as itself");
    check(p::same(moved, p::join(dir, "one/two/./three/b.c")),
          "and so is the same path written through a dot");
    check(p::same(moved, p::join(dir, "one/two/three/../three/b.c")),
          "and one written through a step up and back");
    check(!p::same(moved, p::join(dir, "one/two/three/c.c")),
          "two different files are not the same file");
    check(p::oneName(moved) == p::oneName(moved), "the one name is stable");

    // What is in a directory, without . and .., and saying which are which.
    bool readable = false;
    std::vector<p::Entry> inside = p::entries(p::join(dir, "one"), &readable);
    check(readable, "a directory that is there can be read");
    check(inside.size() == 1 && inside[0].name == "two" && inside[0].directory,
          "and holds the one directory that was made in it");

    p::entries(p::join(dir, "nowhere"), &readable);
    check(!readable, "and one that is not there says so rather than looking empty");

    check(p::remove(moved), "a file can be removed");
    check(!p::exists(moved), "and is gone afterwards");

    // The recursive one, and the two things it refuses to do.
    check(!p::removeTree(""), "nothing is removed when nothing is named");
    check(!p::removeTree("/"), "and a root is refused outright");
    check(p::removeTree(dir), "a directory goes, and everything under it");
    check(!p::exists(dir), "leaving nothing behind");
}

// Where the running program is, and what is next to it. This is how the editor
// finds a compiler installed alongside it, so the answer has to be the
// program's own directory whatever directory it was started in.
void whereTheProgramIs(const char* argv0) {
    std::printf("where the program is, and what is beside it\n");

    namespace p = editor::path;

    const std::string where = p::programDirectory();
    check(!where.empty(), "the machine says where the running program is");
    check(p::isDirectory(where), "and it is a directory");
    checkEqual(where, p::withSlashes(where), "in forward slashes, like everything here");
    checkEqual(where, p::absolute(where), "and absolute, with nothing left to resolve");

    // Whatever this binary is called - test on a Mac or a Linux box, test.exe
    // on Windows, and whatever anyone renames it to - it is beside itself, so
    // asking for its own name has to find it. Taking the name from argv[0]
    // rather than writing "test" here keeps that true.
    std::string me = p::filename(p::withSlashes(argv0 ? argv0 : ""));
    if (me.size() > 4 && me.compare(me.size() - 4, 4, ".exe") == 0) me.resize(me.size() - 4);
    check(!me.empty(), "this test knows what it was called");

    const std::string found = p::besideProgram(me);
    check(!found.empty(), "a program beside the running one is found");
    check(p::exists(found), "and what comes back is really there");
    checkEqual(p::parent(found), where, "in the directory the program is in");

    check(p::besideProgram("").empty(), "nothing is beside nothing");
    check(p::besideProgram("cc1-nobody-has-installed").empty(),
          "a name that is not there is not answered with a path");

    // A directory of the right name is not a program, and answering with one
    // would put it on a command line to be run.
#ifdef _WIN32
    const std::string decoyLeaf = "beside-decoy.exe";
#else
    const std::string decoyLeaf = "beside-decoy";
#endif
    const std::string decoy = p::join(where, decoyLeaf);
    if (p::makeDirectories(decoy)) {
        check(p::besideProgram("beside-decoy").empty(),
              "a directory of that name is not a program");
        p::removeTree(decoy);
    }
}

// Talking to a child rather than only listening to one. Everything else here
// runs a command with popen, says nothing to it and reads until it ends; a
// debugger needs the other direction as well.
void talkingToAChild() {
    std::printf("a child that answers back\n");

    // Something that reads lines and writes them straight back. cat does it on
    // one machine; on the other, findstr looks like the answer and is not -
    // it holds its output until it exits, so a marker sent to it never comes
    // back and the wait for it hung this whole suite on the Windows box. What
    // is wanted there is something that flushes each line as it writes it, and
    // says so.
#ifdef _WIN32
    const char* echoes =
        "powershell -NoProfile -Command \"while (($l = [Console]::In.ReadLine()) -ne $null)"
        " { [Console]::Out.WriteLine($l); [Console]::Out.Flush() }\"";
#else
    const char* echoes = "cat";
#endif

    editor::Process child;
    check(child.start(echoes), "a child starts");
    check(child.running(), "and says it is running");

    check(child.say("first <<mark>>"), "a line can be said to it");
    bool found = false;
    std::string answer = child.readUntil("<<mark>>", &found);
    check(found, "and the marker in the answer is found");
    check(answer.find("first") != std::string::npos, "with what came before it");

    // The second answer must not carry the first: what was read past the
    // marker last time is kept for this time rather than thrown away.
    check(child.say("second <<mark>>"), "and another after it");
    answer = child.readUntil("<<mark>>", &found);
    check(found, "which is found too");
    check(answer.find("second") != std::string::npos, "with its own line");
    check(answer.find("first") == std::string::npos, "and not the one before it");

    child.stop();
    check(!child.running(), "it stops when it is told to");

    // A marker that will never arrive ends when the child does, rather than
    // waiting for it forever.
    editor::Process brief;
    check(brief.start("exit 0"), "a child that does nothing starts");
    brief.readUntil("<<never>>", &found);
    check(!found, "a marker that never comes is not reported as found");
    check(!brief.running(), "and the child is known to have gone");

    editor::Process missing;
    // The shell is what fails here, not this - it is started either way and
    // says its piece on the same stream.
    missing.start("no-such-program-ed1-test");
    missing.readUntil("<<never>>", &found);
    check(!found, "a command that is not there answers nothing");
    missing.stop();
}

std::string readWholeFile(const std::string& where) {
    std::ifstream in(where.c_str(), std::ios::binary);
    std::stringstream all;
    all << in.rdbuf();
    return all.str();
}

void writeSource(const std::string& where, const char* text) {
    std::ofstream out(where.c_str());
    out << text;
}

// What the two debuggers say when they stop, which is the fiddly half of
// driving them and needs neither a debugger nor a built program to check.
// Both of these are what they actually printed, kept as they came.
const char* const kLldbStop =
    "Process 10819 stopped\n"
    "* thread #1, queue = 'com.apple.main-thread', stop reason = breakpoint 1.1\n"
    "    frame #0: 0x0000000100000508 dbg`main at dbg.c:13:9\n"
    "   12  \tfor (int i = 1; i <= 3; ++i) {\n"
    "-> 13  \t    total = total + twice(i);\n";

// A function with two arguments, which is where the name used to be lost: the
// comma inside the argument list was the last one on the line, and the reader
// cut there. Every function in every other recording here takes one argument
// or none, so nothing noticed until a project with a two-argument function was
// stepped into.
const char* const kLldbStopTwoArgs =
    "Process 41207 stopped\n"
    "* thread #1, queue = 'com.apple.main-thread', stop reason = breakpoint 1.1\n"
    "    frame #0: 0x0000000100000440 sums`addUp(a=2, b=40) at sum.c:3:27\n"
    "   2   \t\n"
    "-> 3   \tint addUp(int a, int b) { return a + b; }\n";

// The same shape from gdb, which writes the address and " in " when it did not
// stop at the start of a line.
const char* const kGdbStopTwoArgs =
    "Breakpoint 1, addUp (a=2, b=40) at sum.c:3\n"
    "3\tint addUp(int a, int b) { return a + b; }\n";

const char* const kGdbStop =
    "Breakpoint 1, main () at dbg.c:13\n"
    "13\t        total = total + twice(i);\n";

void whatADebuggerSays() {
    std::printf("where a debugger says it stopped\n");

    editor::Stop twoArgs = editor::dbg_readStop(editor::DebuggerLldb, kLldbStopTwoArgs);
    checkEqual(twoArgs.function, "addUp", "lldb: a two-argument function keeps its name");
    checkEqual(twoArgs.file, "sum.c", "lldb: and the file it is in");
    check(twoArgs.line == 3, "lldb: on the line it stopped at");

    editor::Stop twoArgsGdb = editor::dbg_readStop(editor::DebuggerGdb, kGdbStopTwoArgs);
    checkEqual(twoArgsGdb.function, "addUp", "gdb: the same, past its own preamble");
    check(twoArgsGdb.line == 3, "gdb: and the line");

    editor::Stop lldb = editor::dbg_readStop(editor::DebuggerLldb, kLldbStop);
    check(lldb.stopped, "lldb's stop is read as a stop");
    check(lldb.file == "dbg.c", "with the file it names");
    check(lldb.line == 13, "and the line");
    check(lldb.function == "main", "and the function, without the program in front of it");
    check(!lldb.exited, "and it has not exited");

    editor::Stop gdb = editor::dbg_readStop(editor::DebuggerGdb, kGdbStop);
    check(gdb.stopped && gdb.file == "dbg.c" && gdb.line == 13,
          "gdb says the same thing in its own words");
    check(gdb.function == "main", "including the function, without its empty brackets");

    // lldb writes file:line:column and gdb writes file:line. The column must
    // not be read as the line, which is the one way this goes quietly wrong.
    editor::Stop inside = editor::dbg_readStop(
        editor::DebuggerLldb, "    frame #0: 0x100 dbg`twice(n=1) at dbg.c:5:9\n");
    check(inside.line == 5, "a column after the line is not mistaken for it");
    check(inside.function == "twice", "and arguments are not part of the name");

    // Gone, and what it went with.
    editor::Stop doneLldb = editor::dbg_readStop(
        editor::DebuggerLldb, "Process 10819 exited with status = 3 (0x00000003)\n");
    check(doneLldb.exited && !doneLldb.stopped, "a program that ended is not stopped");
    check(doneLldb.status == 3, "and what it returned is read");

    editor::Stop doneGdb = editor::dbg_readStop(
        editor::DebuggerGdb, "[Inferior 1 (process 41) exited with code 03]\n");
    check(doneGdb.exited && doneGdb.status == 3, "gdb's way of saying it is read too");

    // gdb prints that code in octal, so the two agree on three and disagree on
    // anything above seven. Twelve is where it would have gone wrong quietly.
    check(editor::dbg_readStop(editor::DebuggerGdb,
                           "[Inferior 1 (process 41) exited with code 014]\n").status == 12,
          "and it is read as the octal gdb wrote");
    check(editor::dbg_readStop(editor::DebuggerLldb,
                           "Process 41 exited with status = 12 (0x0000000c)\n").status == 12,
          "while lldb's is the decimal lldb wrote");
    check(editor::dbg_readStop(editor::DebuggerGdb,
                           "[Inferior 1 (process 41) exited normally]\n").status == 0,
          "and normally means nothing went wrong");

    // The variables, which each spells with the type in a different place.
    std::vector<editor::Variable> mine = editor::dbg_readVariables(
        editor::DebuggerLldb, "(int) total = 0\n(int) i = 1\n");
    check(mine.size() == 2, "lldb's variables are read");
    check(mine[0].name == "total" && mine[0].type == "int" && mine[0].value == "0",
          "with name, type and value apart");

    std::vector<editor::Variable> theirs = editor::dbg_readVariables(
        editor::DebuggerGdb, "total = 0\ni = 1\n");
    check(theirs.size() == 2 && theirs[1].name == "i" && theirs[1].value == "1",
          "and gdb's, which say no type");
    check(theirs[0].type.empty(), "so none is invented for them");

    check(editor::dbg_readVariables(editor::DebuggerGdb, "No symbol table info available.\n").empty(),
          "and a line that is not a variable is not read as one");

    // cdb, which answers a move with an address and has to be asked separately
    // where that is. This is what it actually printed for `ln`.
    editor::Stop cdb = editor::dbg_readStop(
        editor::DebuggerCdb,
        "0:000> C:\\Users\\me\\seam.cpp(10)+0x9\n"
        "(00007ff6`44e87160)   seam!main+0x27   |  (00007ff6`44e871c0)   seam!pre_c_init\n");
    check(cdb.stopped, "cdb's answer is read as a stop");
    check(cdb.file == "C:\\Users\\me\\seam.cpp", "with the whole Windows path, drive and all");
    check(cdb.line == 10, "and the line in brackets after it");
    check(cdb.function == "main", "and the function, without its module or its offset");

    // Its program ending is a break in ntdll rather than a message, and what
    // the program returned is in edx - printed in hex, whatever the radix.
    editor::Stop cdbEnd = editor::dbg_readStop(
        editor::DebuggerCdb,
        "ntdll!NtTerminateProcess+0x14:\n00007ffb`d6460904 c3   ret\n"
        "0:000> Last event: 8ec.1ff0: Exit process 0:8ec, code c\n");
    check(cdbEnd.exited && !cdbEnd.stopped, "cdb's ending is read as an ending");
    check(cdbEnd.status == 12, "and the code it gives is read as the hex it is");

    // Which thread it happens to break on when the program ends is not fixed,
    // so the ending must be recognised without depending on that at all.
    editor::Stop onAnother = editor::dbg_readStop(
        editor::DebuggerCdb,
        "ntdll!ZwWaitForWorkViaWorkerFactory+0x14:\n00007ffb`d6464034 c3   ret\n"
        "0:001> Last event: 8ec.1ff0: Exit process 0:8ec, code c\n");
    check(onAnother.exited && onAnother.status == 12,
          "including when it ends on a worker thread rather than the main one");

    std::vector<editor::Variable> cdbLocals = editor::dbg_readVariables(
        editor::DebuggerCdb, "0:000>               i = 0n1\n          total = 0n0\n");
    check(cdbLocals.size() == 2, "cdb's variables are read");
    check(cdbLocals[0].name == "i" && cdbLocals[0].value == "1",
          "with the 0n it puts in front of a decimal taken off again");

    // Both print their prompt and then, on the same line, the first line of the
    // answer. Left on, it is read as part of the name - which showed up as the
    // first variable of every gdb listing being missing and nothing else.
    std::vector<editor::Variable> prompted =
        editor::dbg_readVariables(editor::DebuggerGdb, "(gdb) i = 1\ntotal = 0\n");
    check(prompted.size() == 2 && prompted[0].name == "i",
          "a prompt in front of the first variable is not part of its name");

    // Stepping out, where gdb says what it is leaving before it says where it
    // arrived, and names the address because it did not land on a line start.
    editor::Stop out = editor::dbg_readStop(
        editor::DebuggerGdb,
        "(gdb) Run till exit from #0  twice (n=1) at s.c:3\n"
        "0x00000000004011b3 in main () at s.c:11\n"
        "11\t        total = total + twice(i);\n"
        "Value returned is $1 = 2\n");
    check(out.function == "main" && out.line == 11,
          "stepping out reports where it came back to, not what it left");

    editor::Stop afterPrompt = editor::dbg_readStop(
        editor::DebuggerGdb, "(gdb) twice (n=1) at s.c:3\n3\t    int doubled = n * 2;\n");
    check(afterPrompt.stopped && afterPrompt.function == "twice" && afterPrompt.line == 3,
          "nor of the function it stopped in");
}

// The whole conversation, against a program cc1 built. Needs both a debugger
// and a compiler, so it says when it is skipping rather than passing quietly.
void debuggingForReal() {
    std::printf("stopping, stepping and looking, for real\n");

    const char* cc1 = std::getenv("CC1");
    // Named but not there counts as not named, and the path is printed: a
    // $CC1 with a ~ in it never expands, and a build with an unfindable
    // compiler fails in a way that reads as a broken editor rather than as a
    // path nobody resolved. That cost most of a day once.
    if (cc1 && *cc1 && !editor::path::exists(cc1)) {
        std::printf("  (no cc1 at %s, so nothing is built to debug)\n", cc1);
        return;
    }
    if (!cc1 || !*cc1) {
        std::printf("  (no $CC1, so nothing is built to debug)\n");
        return;
    }
    if (editor::dbg_here() == editor::DebuggerNone) {
        std::printf("  (no debugger on this machine)\n");
        return;
    }

    std::string dir = editor::path::join(editor::path::tempDir(), "ed1-debug-test");
    editor::path::removeTree(dir);
    editor::path::makeDirectories(dir);

    std::string source = editor::path::join(dir, "stepped.c");
    writeSource(source,
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
              "}\n");

    std::string program = editor::path::join(dir, "stepped");
    std::string build = "\"" + std::string(cc1) + "\" \"" + source + "\" -o \"" + program +
                        "\" -g > /dev/null 2>&1";
    if (std::system(build.c_str()) != 0 || !editor::path::exists(program)) {
        std::printf("  (cc1 built nothing to debug)\n");
        editor::path::removeTree(dir);
        return;
    }

    editor::Debugger debugger;
    check(debugger.start(editor::dbg_for(editor::ToolCc1, editor::hostArch()), program),
          "the debugger starts on what cc1 built");
    if (!debugger.running()) { editor::path::removeTree(dir); return; }

    check(debugger.breakAt(source, 11), "a breakpoint is set on a line of C");

    editor::Stop at = debugger.run();
    check(at.stopped, "and running stops on it");
    check(at.line == 11, "on the line it was asked for");
    check(at.function == "main", "in the function that line is in");

    // The variables are the point of the whole exercise: this is cc1's DWARF
    // being read back by somebody else's debugger.
    std::vector<editor::Variable> locals = debugger.locals();
    bool sawTotal = false, sawCounter = false;
    for (size_t i = 0; i < locals.size(); ++i) {
        if (locals[i].name == "total" && locals[i].value == "0") sawTotal = true;
        if (locals[i].name == "i" && locals[i].value == "1") sawCounter = true;
    }
    check(sawTotal, "the local it declared is there, with the value it has");
    check(sawCounter, "and so is the one the loop declared");

    editor::Stop into = debugger.stepInto();
    check(into.stopped && into.function == "twice", "stepping into a call arrives inside it");

    editor::Stop out = debugger.stepOut();
    check(out.stopped && out.function == "main", "and stepping out comes back");

    editor::Stop again = debugger.resume();
    check(again.stopped && again.line == 11, "a breakpoint in a loop is hit again");

    debugger.clearBreakpoints();
    editor::Stop ended = debugger.resume();
    check(ended.exited, "and with none left the program runs to the end");
    check(ended.status == 12, "returning what it worked out - 2 + 4 + 6");

    debugger.stop();
    check(!debugger.running(), "the debugger goes when it is told to");
    editor::path::removeTree(dir);
}

// What the debugger said, across the seam the window uses.
//
// A debugged program writes down the debugger's own stream, so what it printed
// is in `said` along with the debugger's words - and the window had no way to
// read `said` at all until ed1_stop_said existed. This is the property that
// makes that accessor worth having, so it is checked rather than assumed: the
// program's own output has to be in there.
void whatTheDebuggerHeard() {
    std::printf("what the debugger said, and the program with it\n");

    const std::string host = editor::hostArch();
    if (editor::dbg_for(editor::ToolCc1, host) == editor::DebuggerNone) {
        std::printf("  (no debugger on this machine, so nothing is listened to)\n");
        return;
    }
    const char* cc1 = std::getenv("CC1");
    if (!cc1 || !*cc1 || !editor::path::exists(cc1)) {
        std::printf("  (no cc1, so nothing is built to listen to)\n");
        return;
    }

    std::string dir = editor::path::join(editor::path::tempDir(), "ed1-said-test");
    editor::path::removeTree(dir);
    editor::path::makeDirectories(dir);
    std::string source = editor::path::join(dir, "talker.c");

    // It flushes after printing, so a marker that is missing from `said` is
    // missing because it never travelled - not because it was in a buffer.
    writeSource(source,
                "#include <stdio.h>\n"
                "\n"
                "int main(void)\n"
                "{\n"
                "    printf(\"MARKER-BEFORE\\n\");\n"
                "    fflush(stdout);\n"
                "    int x = 1;\n"
                "    x = x + 1;\n"
                "    return x;\n"
                "}\n");

    Ed1Program* built = ed1_build_program(cc1, "cl", editor::ToolCc1, source.c_str(),
                                          editor::LangC, host.c_str(), editor::ConfigDebug);
    if (ed1_program_ok(built) == 0) {
        std::printf("  (cc1 did not build it, so there is nothing to stop inside)\n");
        ed1_program_free(built);
        editor::path::removeTree(dir);
        return;
    }

    Ed1Debugger* debugger = ed1_debugger_new();
    check(ed1_debugger_start(debugger, ed1_debugger_for(editor::ToolCc1, host.c_str()),
                             ed1_program_path(built)) != 0,
          "the debugger starts on a program that talks");

    // Line 8 is x = x + 1, after the printf and its flush.
    check(ed1_debugger_break(debugger, source.c_str(), 8) != 0, "a breakpoint after the printing");

    ed1_debugger_run(debugger);
    check(ed1_stop_stopped(debugger) != 0, "and it stops there");

    const std::string said = ed1_stop_said(debugger);
    check(!said.empty(), "what the debugger said comes across the seam");
    check(said.find("MARKER-BEFORE") != std::string::npos,
          "and the program's own output is in it, which is why the window wants it");

    ed1_debugger_stop(debugger);
    ed1_debugger_free(debugger);
    ed1_program_free(built);
    editor::path::removeTree(dir);
}

// C++ on Windows, where none of the chain is ours: cl writes the .pdb, cdb
// reads it, and the editor only drives them. This is the other half of the
// same machine - the C file next to it goes to cc1 and cannot be debugged at
// all, because MASM carries no line table.
void debuggingCppForReal() {
    std::printf("stopping inside what cl built\n");

    if (editor::dbg_for(editor::ToolMsvc, editor::hostArch()) == editor::DebuggerNone) {
        std::printf("  (%s)\n",
                    editor::dbg_whyNot(editor::ToolMsvc, editor::hostArch()).c_str());
        return;
    }

    std::string dir = editor::path::join(editor::path::tempDir(), "ed1-cpp-debug-test");
    editor::path::removeTree(dir);
    editor::path::makeDirectories(dir);
    std::string source = editor::path::join(dir, "counted.cpp");
    writeSource(source,
                "static int twice(int n)\n"
                "{\n"
                "    return n * 2;\n"
                "}\n"
                "\n"
                "int main(void)\n"
                "{\n"
                "    int total = 0;\n"
                "    for (int i = 1; i <= 3; ++i) {\n"
                "        total = total + twice(i);\n"
                "    }\n"
                "    return total;\n"
                "}\n");

    editor::Toolchain tool;
    editor::Built made = editor::buildProgram(tool, editor::ToolMsvc, source, editor::LangCpp,
                                              editor::hostArch(), editor::ConfigDebug);
    check(made.ok, "cl builds a program from the C++ file");
    if (!made.ok) { editor::removeProgram(made); editor::path::removeTree(dir); return; }

    editor::Debugger debugger;
    check(debugger.start(editor::dbg_for(editor::ToolMsvc, editor::hostArch()), made.program),
          "cdb starts on it");
    if (!debugger.running()) { editor::removeProgram(made); editor::path::removeTree(dir); return; }

    check(debugger.breakAt(source, 10), "a breakpoint is set on a line of C++");

    editor::Stop at = debugger.run();
    check(at.stopped, "and running stops on it");
    check(at.line == 10, "on the line asked for");
    check(at.function == "main", "in the function that line is in");

    // These come out of the .pdb cl wrote, read by Microsoft's own debugger.
    std::vector<editor::Variable> locals = debugger.locals();
    bool sawTotal = false, sawCounter = false;
    for (size_t i = 0; i < locals.size(); ++i) {
        if (locals[i].name == "total" && locals[i].value == "0") sawTotal = true;
        if (locals[i].name == "i" && locals[i].value == "1") sawCounter = true;
    }
    check(sawTotal, "the local it declared is there, with the value it has");
    check(sawCounter, "and so is the one the loop declared");

    editor::Stop into = debugger.stepInto();
    check(into.stopped && into.function == "twice", "stepping into a call arrives inside it");

    editor::Stop out = debugger.stepOut();
    check(out.stopped && out.function == "main", "and stepping out comes back");

    debugger.clearBreakpoints();
    editor::Stop ended = debugger.resume();
    check(ended.exited, "and with none left the program runs to the end");
    check(ended.status == 12, "returning what it worked out - 2 + 4 + 6");

    debugger.stop();
    editor::removeProgram(made);
    editor::path::removeTree(dir);
}

// Everything the Windows front end does to stop a program on a line, done
// through the same seam it uses, on a machine where a debugger exists.
// The window asks for the project's build through the same seam, and the
// checks below are the same questions the terminal's F4 asks - which is the
// point of there being one core and two front ends rather than two editors.
void theWindowsProjectBuild() {
    std::printf("what the window asks about building a project\n");

    file::path dir = file::temp_directory_path() / "ed1-bridge-target";
    file::remove_all(dir);
    file::create_directories(dir);
    writeSource((dir / "add.c").string(), "int add(int a, int b) { return a + b; }\n");
    writeSource((dir / "main.c").string(), "int add(int, int);\nint main(void) { return add(1, 2); }\n");
    writeSource((dir / "ed1.json").string(),
                "{\n  \"name\": \"sums\",\n"
                "  \"groups\": { \"Sources\": [\"add.c\", \"main.c\"] },\n"
                "  \"build\": { \"target\": \"sums\", \"groups\": [\"Sources\"] }\n}\n");

    Ed1Project* project = ed1_project_new();
    char trouble[512] = {0};
    check(ed1_project_load(project, dir.string().c_str(), trouble, sizeof trouble) != 0,
          "the window loads a project that says what it builds");
    check(ed1_project_builds(project) != 0, "and is told that it builds something");
    check(ed1_project_target_ready(project) != 0, "the sources come back through the seam");
    check(ed1_project_target_sources(project) == 2, "both of them");
    check(std::string(ed1_project_target_program(project)).find("sums") != std::string::npos,
          "with the program named after the target");

    // The refusal reaches the window in the two pieces it needs: a line for
    // the status bar and the rest for the console.
    writeSource((dir / "extra.cpp").string(), "int twice(int n) { return n * 2; }\n");
    writeSource((dir / "ed1.json").string(),
                "{\n  \"name\": \"sums\",\n"
                "  \"groups\": { \"Sources\": [\"add.c\", \"main.c\", \"extra.cpp\"] },\n"
                "  \"build\": { \"target\": \"sums\", \"groups\": [\"Sources\"] }\n}\n");
    check(ed1_project_load(project, dir.string().c_str(), trouble, sizeof trouble) != 0,
          "a project of both languages loads");
    check(ed1_project_target_ready(project) == 0, "and is refused before a compiler is run");
    check(std::string(ed1_project_target_why(project)).find("C++") != std::string::npos,
          "with a line saying which two languages");
    check(std::string(ed1_project_target_detail(project)).find("cc1") != std::string::npos,
          "and the rest of it for the console");

    ed1_project_free(project);
    file::remove_all(dir);
}

void theSeamTheWindowUses() {
    std::printf("what the window asks the core to do\n");

    std::string host = editor::hostArch();
    check(ed1_debugger_for(editor::ToolCc1, host.c_str()) ==
              static_cast<int>(editor::dbg_for(editor::ToolCc1, host)),
          "the window is told the same debugger the editor found");
    check(std::string(ed1_debugger_name(ed1_debugger_for(editor::ToolCc1, host.c_str()))) ==
              editor::dbg_name(editor::dbg_for(editor::ToolCc1, host)),
          "and the same name for it");

    // The two compilers are not in the same position on the same machine, and
    // the reason given has to say which one it is talking about.
    check(ed1_debugger_for(editor::ToolCc1, "x86_64-windows") == 0,
          "what cc1 builds for Windows can never be debugged");
    check(std::string(ed1_no_debugger_because(editor::ToolCc1, "x86_64-windows"))
              .find("MASM") != std::string::npos,
          "and the reason names the MASM that has no line table");
    // cl is a different matter on the same machine, and which way it goes
    // depends on whether Microsoft's own debugger is installed - so the check
    // is that the answer and the reason agree, not that either is fixed.
    int forCl = ed1_debugger_for(editor::ToolMsvc, "x86_64-windows");
    std::string whyNotCl = ed1_no_debugger_because(editor::ToolMsvc, "x86_64-windows");
    if (forCl == static_cast<int>(editor::DebuggerCdb)) {
        check(whyNotCl.empty(), "where cdb is installed, cl's C++ has nothing standing in its way");
    } else {
        check(whyNotCl.find("cdb") != std::string::npos,
              "and where it is not, the reason names the debugger that is missing");
    }

    if (editor::dbg_for(editor::ToolCc1, host) == editor::DebuggerNone) {
        std::printf("  (no debugger on this machine, so the rest is not tried)\n");
        return;
    }

    const char* cc1 = std::getenv("CC1");
    if (cc1 && *cc1 && !editor::path::exists(cc1)) {
        std::printf("  (no cc1 at %s, so nothing is built to stop inside)\n", cc1);
        return;
    }
    if (!cc1 || !*cc1) {
        std::printf("  (no $CC1, so nothing is built to stop inside)\n");
        return;
    }

    std::string dir = editor::path::join(editor::path::tempDir(), "ed1-bridge-test");
    editor::path::removeTree(dir);
    editor::path::makeDirectories(dir);
    std::string source = editor::path::join(dir, "seam.c");
    writeSource(source,
                "static int twice(int n)\n"
                "{\n"
                "    return n * 2;\n"
                "}\n"
                "\n"
                "int main(void)\n"
                "{\n"
                "    int total = 0;\n"
                "    for (int i = 1; i <= 3; ++i) {\n"
                "        total = total + twice(i);\n"
                "    }\n"
                "    return total;\n"
                "}\n");

    // Built through the bridge, exactly as the window builds it.
    Ed1Program* built = ed1_build_program(cc1, "cl", editor::ToolCc1, source.c_str(),
                                          editor::LangC, editor::hostArch(),
                                          editor::ConfigDebug);
    check(ed1_program_ok(built) != 0, "the window's build makes a program");
    if (ed1_program_ok(built) == 0) { ed1_program_free(built); editor::path::removeTree(dir); return; }
    check(editor::path::exists(ed1_program_path(built)),
          "and leaves it where it said it did, for a debugger to open");

    Ed1Debugger* debugger = ed1_debugger_new();
    check(ed1_debugger_start(debugger, ed1_debugger_for(editor::ToolCc1, host.c_str()),
                             ed1_program_path(built)) != 0,
          "the debugger starts on it");
    check(ed1_debugger_running(debugger) != 0, "and says it is running");

    check(ed1_debugger_break(debugger, source.c_str(), 10) != 0, "a breakpoint is set");

    ed1_debugger_run(debugger);
    check(ed1_stop_stopped(debugger) != 0, "running stops on it");
    check(ed1_stop_line(debugger) == 10, "on the line asked for");
    check(std::string(ed1_stop_function(debugger)) == "main", "in the right function");

    // The locals are read once when it stops and handed over one string at a
    // time, because the managed side cannot hold a std::vector.
    int howMany = ed1_locals_count(debugger);
    bool sawTotal = false;
    for (int i = 0; i < howMany; ++i)
        if (std::string(ed1_local_name(debugger, i)) == "total" &&
            std::string(ed1_local_value(debugger, i)) == "0") sawTotal = true;
    check(howMany > 0 && sawTotal, "and what is in scope comes back one name at a time");
    check(std::string(ed1_local_name(debugger, howMany + 5)).empty(),
          "an index past the end answers with nothing rather than reading past it");

    ed1_debugger_step_into(debugger);
    check(std::string(ed1_stop_function(debugger)) == "twice", "stepping into arrives inside");

    ed1_debugger_step_out(debugger);
    check(std::string(ed1_stop_function(debugger)) == "main", "and stepping out comes back");

    ed1_debugger_clear(debugger);
    ed1_debugger_resume(debugger);
    check(ed1_stop_exited(debugger) != 0, "with no breakpoints left it runs to the end");
    check(ed1_stop_status(debugger) == 12, "returning what it worked out");

    ed1_debugger_stop(debugger);
    check(ed1_debugger_running(debugger) == 0, "and stops when it is told to");
    ed1_debugger_free(debugger);

    // Freeing the handle takes the program with it, which is what stops a
    // debugging session leaving one behind in the temporary directory.
    std::string was = ed1_program_path(built);
    ed1_program_free(built);
    check(!editor::path::exists(was), "freeing the build removes the program it made");

    editor::path::removeTree(dir);
}

// A directory with no project file gets one made, rather than the editor
// opening without a project at all.
void aProjectMadeFromWhatIsThere() {
    std::printf("a project made where there was none\n");

    namespace pth = editor::path;
    std::string dir = pth::join(pth::tempDir(), "ed1-made-project");
    pth::removeTree(dir);
    pth::makeDirectories(pth::join(dir, "src"));
    pth::makeDirectories(pth::join(dir, "obj"));

    writeSource(pth::join(dir, "one.c"), "int one;\n");
    writeSource(pth::join(dir, "notes.txt"), "not source\n");
    writeSource(pth::join(dir, "src/two.cpp"), "int two;\n");
    writeSource(pth::join(dir, "src/two.h"), "extern int two;\n");
    writeSource(pth::join(dir, "obj/two.o"), "not source either\n");

    editor::Project project;
    editor::Outcome made = editor::beginFromWhatIsThere(project, dir);
    check(made.ok, "a project is made where there was none");
    check(project.loaded(), "and the project says it is loaded");
    check(pth::exists(pth::join(dir, "ed1.json")), "and the file is written");
    check(project.name() == "ed1-made-project", "named after the directory it is in");

    // What it picked up, and what it left alone.
    std::string written = readWholeFile(pth::join(dir, "ed1.json"));
    check(written.find("one.c") != std::string::npos, "source in the directory is in it");
    check(written.find("src/two.cpp") != std::string::npos, "and source one level down");
    check(written.find("src/two.h") != std::string::npos, "headers as well as sources");
    check(written.find("notes.txt") == std::string::npos, "what is not source is left out");
    check(written.find("two.o") == std::string::npos, "and so is anything under obj");

    // Read back by the thing that will read it tomorrow.
    editor::Project again;
    std::string why;
    check(again.load(dir, why), "and what was written can be read again");
    check(why.empty(), "without complaint");
    check(again.groups().size() == 2, "into the two groups it was given");

    // Headers and sources are different things to look at, so they are in
    // different groups even when nobody said so.
    bool headersHoldTheHeader = false, sourcesHoldTheSource = false, headersHoldNoSource = true;
    for (size_t i = 0; i < again.groups().size(); ++i) {
        const editor::Group& group = again.groups()[i];
        for (size_t j = 0; j < group.files.size(); ++j) {
            if (group.name == "Headers" && group.files[j] == "src/two.h") headersHoldTheHeader = true;
            if (group.name == "Headers" && group.files[j] == "one.c") headersHoldNoSource = false;
            if (group.name == "Sources" && group.files[j] == "one.c") sourcesHoldTheSource = true;
        }
    }
    check(headersHoldTheHeader, "the header is in Headers");
    check(sourcesHoldTheSource, "the source is in Sources");
    check(headersHoldNoSource, "and neither is in the other");

    pth::removeTree(dir);
}

// What the editor remembers between sessions, and what it opens when there is
// nothing to remember. Both are about the machine you are on, so both are
// checked with home pointed somewhere disposable.
void sayWhereHomeIs(const std::string& where) {
#ifdef _WIN32
    _putenv_s("USERPROFILE", where.c_str());
#else
    setenv("HOME", where.c_str(), 1);
#endif
}

void whatItRemembers() {
    std::printf("what it remembers, and where a first run opens\n");

    namespace pth = editor::path;
    std::string realHome = pth::homeDir();

    std::string home = pth::join(pth::tempDir(), "ed1-home-test");
    pth::removeTree(home);
    pth::makeDirectories(home);
    sayWhereHomeIs(home);

    check(pth::homeDir() == home, "home is where the machine says it is");
    check(editor::settings::fileName().find(".ed1config.json") != std::string::npos,
          "the editor's own configuration is beside your files");
    check(editor::settings::lastProject().empty(), "and remembers nothing to begin with");

    // A configuration that will not parse is not silently buried. It is kept
    // under .error, a fresh one is written in its place, and the editor can say
    // where the old one went. Before this, the first setting changed after a
    // bad file wrote straight over it.
    {
        std::string config = editor::settings::fileName();
        writeSource(config, "{ this is not json at all");
        editor::settings::rememberProject(home);   // any read is enough to trip it

        check(pth::exists(config + ".error"), "an unreadable configuration is kept as .error");
        check(pth::exists(config), "and a fresh one is written in its place");
        check(editor::settings::setAside() == config + ".error",
              "and the editor can say where the old one went");
        check(editor::settings::lastProject() == home,
              "the fresh one is readable, and takes what is written to it");

        // An empty file is nobody's work: it is not worth keeping a copy of.
        pth::remove(config + ".error");
        writeSource(config, "");
        check(editor::settings::lastProject().empty(), "an empty configuration reads as nothing");
        check(!pth::exists(config + ".error"), "and is not kept aside");
    }

    // A first run has nothing to go back to, so it is given something to open.
    std::string demo = editor::demoDirectory();
    check(!demo.empty(), "a first run is given a project of its own");
    check(pth::exists(pth::join(demo, "src/first.c")), "with one program in it");

    // Two groups, and Headers empty until there is a header - the place to put
    // one exists before the first one does.
    editor::Project made;
    check(editor::beginFromWhatIsThere(made, demo).ok, "and a project over it");
    check(made.groups().size() == 2, "with two groups");
    for (size_t i = 0; i < made.groups().size(); ++i)
        if (made.groups()[i].name == "Headers")
            check(made.groups()[i].files.empty(), "Headers empty until there is a header");

    std::string was = readWholeFile(pth::join(demo, "src/first.c"));
    check(was.find("t_flight") != std::string::npos,
          "which works something out a line at a time, to step through");
    check(was.find("#ifndef M_PI") != std::string::npos,
          "and guards M_PI, which MSVC keeps behind a define");
    check(was.find("3.14") != std::string::npos,
          "with a value only Windows ever uses, and two decimals do not notice");

    // Made once: asking again must not write over what you have done to it.
    writeSource(pth::join(demo, "src/first.c"), "int mine(void) { return 1; }\n");
    check(editor::demoDirectory() == demo, "asking again gives the same directory");
    check(readWholeFile(pth::join(demo, "src/first.c")).find("mine") != std::string::npos,
          "and leaves what you have done to it alone");

    // Remembering, and forgetting what has gone away.
    check(editor::settings::rememberProject(demo), "a project can be remembered");
    check(editor::settings::lastProject() == pth::absolute(demo), "and is given back");

    std::string gone = pth::join(home, "taken-away");
    pth::makeDirectories(gone);
    check(editor::settings::rememberProject(gone), "another can be remembered over it");
    pth::removeTree(gone);
    check(editor::settings::lastProject().empty(),
          "one that has since been deleted is not offered");

    sayWhereHomeIs(realHome);
    pth::removeTree(home);
}

// What a project says it builds, and the three ways it can say something that
// cannot be built. The compiling itself is the session suite's job; this is
// about the reading and the refusing, which is where the rules live.
// cc1 says it in two shapes, and the editor only ever read one of them: a
// missing header - the most ordinary mistake there is - left the caret where it
// was and the console to be read by eye.
void theOtherShapeOfDiagnostic() {
    std::printf("the second shape a diagnostic comes in\n");

    // What cc1's preprocessor actually writes, caret line and all.
    std::string said =
        "/tmp/p/src/main.c:1: #include \"shapes.h\"\n"
        "                               ^ cannot find \"shapes.h\" - looked in /tmp/p/src\n";
    editor::Diagnostic d = editor::parseDiagnostic(said);
    check(d.present, "a preprocessor diagnostic is read at all");
    checkEqual(d.file, "/tmp/p/src/main.c", "with the file it is about");
    check(d.line == 1, "and the line");
    check(d.col > 1, "and a column worked out from where the caret points");
    check(d.message.find("cannot find") != std::string::npos, "and what it says");

    // A Windows path has a colon after the drive letter that means nothing
    // here, and the line number is still the last one.
    std::string onWindows =
        "C:\\work\\src\\main.c:7: #include \"gone.h\"\n"
        "                              ^ cannot find \"gone.h\"\n";
    editor::Diagnostic w = editor::parseDiagnostic(onWindows);
    check(w.present, "a Windows path is read too");
    checkEqual(w.file, "C:\\work\\src\\main.c", "with the drive letter kept");
    check(w.line == 7, "and the right line");

    // The first shape still wins where both could be read.
    editor::Diagnostic ordinary =
        editor::parseDiagnostic("/tmp/a.c:3:9: error: no such thing\n");
    check(ordinary.present && ordinary.line == 3 && ordinary.col == 9,
          "the shape with a severity word still reads as it did");

    // A caret with nothing after it is somebody underlining, not an error.
    editor::Diagnostic bare = editor::parseDiagnostic("/tmp/a.c:3: int x = ;\n        ^\n");
    check(!bare.present, "a caret with nothing to say is not a diagnostic");
}

// A link that fails is a compiler that ran, and used to be reported as one
// that could not be started: "ld: symbol(s) not found" contains "not found",
// and the advice that followed - name it with --cc1, put it on PATH - sent
// anybody who read it looking in the wrong place.
void whatALinkFailureSays() {
    std::printf("what a link failure is called\n");

    const char* cc1 = std::getenv("CC1");
    if (cc1 && *cc1 && !editor::path::exists(cc1)) {
        std::printf("  (no cc1 at %s, so nothing is linked)\n", cc1);
        return;
    }
    if (!cc1 || !*cc1) {
        std::printf("  (no $CC1, so nothing is linked)\n");
        return;
    }

    std::string dir = editor::path::join(editor::path::tempDir(), "ed1-link-test");
    editor::path::removeTree(dir);
    editor::path::makeDirectories(dir);
    std::string source = editor::path::join(dir, "calls.c");
    writeSource(source,
                "extern int area(int side);\n"
                "int main(void) { return area(2); }\n");

    editor::Toolchain tool;
    tool.cc1 = cc1;
    // buildProgram, not build: the second stops at compiling, and a link that
    // never happens cannot fail.
    editor::Built made = editor::buildProgram(tool, editor::ToolCc1, source, editor::LangC,
                                              editor::hostArch(), editor::ConfigDebug);
    check(!made.ok, "a function declared and never defined does not link");
    check(made.output.find("could not be run") == std::string::npos,
          "and the compiler is not blamed for not being installed");
    check(made.output.find("area") != std::string::npos,
          "the console names the symbol nothing defined");

    editor::removeProgram(made);
    editor::path::removeTree(dir);
}

void whatTheProjectBuilds() {
    std::printf("what a project says it builds\n");

    file::path dir = file::temp_directory_path() / "ed1-target-test";
    file::remove_all(dir);
    file::create_directories(dir);

    writeSource((dir / "ed1.json").string(),
              "{\n"
              "  \"name\": \"sums\",\n"
              "  \"groups\": {\n"
              "    \"Sources\": [\"add.c\", \"main.c\"],\n"
              "    \"Headers\": [\"add.h\"],\n"
              "    \"Notes\": [\"README.md\"]\n"
              "  },\n"
              "  \"build\": { \"target\": \"sums\", \"groups\": [\"Sources\"] }\n"
              "}\n");

    // The files themselves, because a project that lists what is not there is
    // refused now - which is the case a few checks further down.
    writeSource((dir / "add.c").string(), "int add(int a, int b) { return a + b; }\n");
    writeSource((dir / "main.c").string(), "int main(void) { return 0; }\n");
    writeSource((dir / "add.h").string(), "int add(int a, int b);\n");
    writeSource((dir / "README.md").string(), "notes\n");

    editor::Project project;
    std::string error;
    check(project.load(dir.string(), error), "a project with a build entry loads");
    check(project.builds(), "and says it builds something");
    checkEqual(project.target().name, "sums", "under the name it gives");

    std::vector<std::string> sources;
    editor::Language lang = editor::LangPlain;
    std::string why, detail;
    check(project.targetSources(sources, lang, why, &detail), "its sources come back");
    check(sources.size() == 2, "the two in the group it named");
    check(lang == editor::LangC, "in the language they are in");
    check(why.empty(), "with nothing to complain about");
    check(sources[0].find("add.c") != std::string::npos, "in the order the group has them");

    // The header is in the project to be opened, not to be compiled, and the
    // group of notes is not source at all.
    for (size_t i = 0; i < sources.size(); ++i)
        check(sources[i].find(".h") == std::string::npos, "and no header is handed to a compiler");

    // Both languages at once is the one worth refusing: cc1 compiles the C and
    // cl compiles the C++, and neither can be given the other's files.
    editor::Target both;
    both.name = "mixed";
    both.groups.push_back("Sources");
    project.setTarget(both);
    writeSource((dir / "extra.cpp").string(), "int twice(int n) { return n * 2; }\n");
    check(project.addFile("extra.cpp", "Sources"), "a C++ file joins the group");
    check(!project.targetSources(sources, lang, why, &detail), "and the build is refused");
    check(why.find("C++") != std::string::npos, "saying which two languages");
    check(!detail.empty(), "with the rest of it for the console");
    check(sources.empty(), "and nothing handed back to compile");

    // A file the project lists and the disk has not got. The compiler would
    // say "cannot open" with no line to go to and nothing about the project;
    // this is a fault in the configuration and the editor holds the list.
    editor::Target onlyThere;
    onlyThere.name = "gone";
    onlyThere.groups.push_back("Absent");
    project.setTarget(onlyThere);
    check(project.addFile("nowhere.c", "Absent"), "a file is listed that is not on disk");
    check(!project.targetSources(sources, lang, why, &detail),
          "and the build is refused before a compiler runs");
    check(why.find("nowhere.c") != std::string::npos, "naming the file that is not there");
    check(why.find("not on disk") != std::string::npos, "and saying what is wrong with it");
    check(detail.find("ed1.json") != std::string::npos, "with where the list lives");
    check(sources.empty(), "and nothing handed back to compile");

    // A group that is not there, and a target with no source in it.
    editor::Target missing;
    missing.name = "sums";
    missing.groups.push_back("Nowhere");
    project.setTarget(missing);
    check(!project.targetSources(sources, lang, why, &detail), "an unknown group is refused");
    check(why.find("Nowhere") != std::string::npos, "and named");

    editor::Target empty;
    empty.name = "sums";
    empty.groups.push_back("Notes");
    project.setTarget(empty);
    check(!project.targetSources(sources, lang, why, &detail), "a group with no source is refused");

    // Nothing said at all is not an error to report, only nothing to build.
    editor::Project quiet;
    file::path bare = file::temp_directory_path() / "ed1-target-bare";
    file::remove_all(bare);
    file::create_directories(bare);
    writeSource((bare / "ed1.json").string(),
                "{ \"name\": \"quiet\", \"groups\": { \"Sources\": [] } }\n");
    check(quiet.load(bare.string(), error), "a project with no build entry still loads");
    check(!quiet.builds(), "and says it builds nothing");
    check(!quiet.targetSources(sources, lang, why, &detail), "so there is nothing to hand back");
    check(!why.empty(), "and it says so rather than saying nothing");

    // What it says it builds survives being written and read again.
    editor::Target kept;
    kept.name = "sums";
    kept.groups.push_back("Sources");
    project.setTarget(kept);
    check(project.save(error), "the project writes itself back");

    editor::Project again;
    check(again.load(dir.string(), error), "and reads again");
    check(again.builds(), "still building");
    checkEqual(again.target().name, "sums", "the same program");
    check(again.target().groups.size() == 1 && again.target().groups[0] == "Sources",
          "out of the same groups");

    file::remove_all(dir);
    file::remove_all(bare);
}

int main(int argc, char** argv) {
    paths();
    whereTheProgramIs(argc > 0 ? argv[0] : 0);
    whatTheDebuggerHeard();
    aProjectMadeFromWhatIsThere();
    whatItRemembers();
    talkingToAChild();
    whatADebuggerSays();
    debuggingForReal();
    debuggingCppForReal();
    theSeamTheWindowUses();
    theWindowsProjectBuild();
    diagnostics();
    layout();
    typing();
    colours();
    whatTheBuildMade();
    routing();
    multiByte();
    ranges();
    undoing();
    savedState();
    searching();
    jsonReading();
    projects();
    operations();
    theOtherShapeOfDiagnostic();
    whatALinkFailureSays();
    whatTheProjectBuilds();

    std::printf("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
