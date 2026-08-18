// The two pieces of this editor with a contract worth pinning down: the layout
// rules, and the reading of cc1's one diagnostic. Neither needs a terminal, so
// neither is checked by typing into one and looking.

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "compile.h"
#include "indent.h"
#include "syntax.h"

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

}  // namespace

int main() {
    diagnostics();
    layout();
    typing();
    colours();

    std::printf("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
