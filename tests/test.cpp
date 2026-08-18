// The two pieces of this editor with a contract worth pinning down: the layout
// rules, and the reading of cc1's one diagnostic. Neither needs a terminal, so
// neither is checked by typing into one and looking.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "compile.h"
#include "indent.h"
#include "syntax.h"
#include "find.h"
#include "json.h"
#include "project.h"
#include "toolchain.h"

namespace {

int failures = 0;
int checks = 0;

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
    check(editor::configFlags(editor::ToolMsvc, editor::ConfigDebug).find("/Od") !=
              std::string::npos,
          "cl's debug turns the optimiser off");
    check(editor::configFlags(editor::ToolMsvc, editor::ConfigRelease).find("/O2") !=
              std::string::npos,
          "and its release turns it up");
    check(editor::configFlags(editor::ToolMsvc, editor::ConfigRelease).find("NDEBUG") !=
              std::string::npos,
          "with NDEBUG defined alongside");

    // cc1 has no -O and no -g, so its configuration is the define and nothing
    // else. Passing it a -O it would refuse would be worse than saying so.
    std::string cc1Debug = editor::configFlags(editor::ToolCc1, editor::ConfigDebug);
    std::string cc1Release = editor::configFlags(editor::ToolCc1, editor::ConfigRelease);
    check(cc1Debug.find("_DEBUG") != std::string::npos, "cc1's debug defines _DEBUG");
    check(cc1Release.find("NDEBUG") != std::string::npos, "and its release defines NDEBUG");
    check(cc1Debug.find("-O") == std::string::npos &&
              cc1Release.find("-O") == std::string::npos,
          "and neither passes a -O, which cc1 has not got");
    check(cc1Debug.find("-g") == std::string::npos, "nor a -g, which it has not got either");

    check(editor::optimises(editor::ToolMsvc), "cl optimises");
    check(!editor::optimises(editor::ToolCc1), "cc1 does not, and does not pretend to");

    std::string release = editor::shownCommand(tool, editor::ToolCc1, "a.c",
                                               editor::LangC, "arm64-darwin",
                                               editor::ConfigRelease);
    check(release.find("-DNDEBUG=1") != std::string::npos,
          "and the define reaches the command line");
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

    std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "ed1-project-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

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
    std::filesystem::path bare = dir / "empty";
    std::filesystem::create_directories(bare);
    editor::Project none;
    check(!none.load(bare.string(), error), "a directory with no project file");
    check(error.empty(), "is not an error");

    // The smallest file that works. Everything has a default, so an empty
    // object is a valid project.
    std::filesystem::path tiny = dir / "tiny";
    std::filesystem::create_directories(tiny);
    { std::ofstream f((tiny / "ed1.json").string().c_str()); f << "{}\n"; }
    editor::Project small;
    check(small.load(tiny.string(), error), "an empty object is a project");
    check(error.empty() && small.indent().width == 4 && !small.indent().tabs,
          "and every setting falls back to its default");
    check(small.config() == editor::ConfigDebug,
          "debug being the one you want while the code is still being written");

    std::filesystem::remove_all(dir);
}

}  // namespace

int main() {
    diagnostics();
    layout();
    typing();
    colours();
    routing();
    searching();
    jsonReading();
    projects();

    std::printf("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
