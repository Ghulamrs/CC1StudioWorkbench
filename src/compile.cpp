#include "compile.h"

#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#define POPEN  _popen
#define PCLOSE _pclose
#else
#include <sys/wait.h>
#define POPEN  popen
#define PCLOSE pclose
#endif

namespace editor {

const char* const kArches[3] = {"x86_64-windows", "x86_64-linux", "arm64-darwin"};

namespace {

// file:line:col: error: message
//
// Read from the right rather than by splitting on every colon: a Windows path
// starts 'C:\', so the first colon on the line is not a separator - but
// ': error: ' only ever appears where the compiler put it.
bool parseGnu(const std::string& line, Diagnostic& d) {
    const std::string marker = ": error: ";
    size_t at = line.find(marker);
    if (at == std::string::npos) return false;

    std::string where = line.substr(0, at);
    size_t colAt = where.rfind(':');
    if (colAt == std::string::npos || colAt == 0) return false;
    size_t lineAt = where.rfind(':', colAt - 1);
    if (lineAt == std::string::npos) return false;

    size_t lineNo = static_cast<size_t>(std::atol(where.c_str() + lineAt + 1));
    size_t colNo = static_cast<size_t>(std::atol(where.c_str() + colAt + 1));
    if (lineNo == 0 || colNo == 0) return false;

    d.file = where.substr(0, lineAt);
    d.line = lineNo;
    d.col = colNo;
    d.message = line.substr(at + marker.size());
    d.present = true;
    return true;
}

// file(line,col): error C2059: message, and the form with no column at all -
// which is what cl gives without /diagnostics:column, and what ml64 gives
// always.
bool parseMsvc(const std::string& line, Diagnostic& d) {
    size_t at = line.find("): ");
    if (at == std::string::npos) return false;

    std::string rest = line.substr(at + 3);
    if (rest.compare(0, 6, "error ") != 0 && rest.compare(0, 12, "fatal error ") != 0)
        return false;

    size_t open = line.rfind('(', at);
    if (open == std::string::npos) return false;

    std::string inside = line.substr(open + 1, at - open - 1);
    size_t comma = inside.find(',');
    size_t lineNo = static_cast<size_t>(std::atol(inside.c_str()));
    if (lineNo == 0) return false;

    size_t colNo = 1;
    if (comma != std::string::npos)
        colNo = static_cast<size_t>(std::atol(inside.c_str() + comma + 1));
    if (colNo == 0) colNo = 1;

    d.file = line.substr(0, open);
    d.line = lineNo;
    d.col = colNo;
    // The word 'error' is dropped from the message: whoever shows this puts it
    // back, and 'error: error C2059' reads like a stutter.
    d.message = rest.compare(0, 12, "fatal error ") == 0 ? rest.substr(12) : rest.substr(6);
    d.present = true;
    return true;
}

}  // namespace

// cc1 says it in two shapes and this only ever knew one of them.
//
// The parser's shape is what parseGnu reads: file:line:col: error: message.
// The preprocessor's is two lines, with no severity word and no column at all -
// it echoes the offending line and points at it:
//
//     /path/main.c:1: #include "shapes.h"
//                               ^ cannot find "shapes.h" - looked in ...
//
// The column is recoverable even so: the caret sits under the character it
// means, and the echoed source began at the width of the "file:line: " prefix,
// so the difference between them is the column. A missing header is the most
// ordinary mistake there is, and until this the editor could not take you to it.
bool parseCc1Preprocessor(const std::string& first, const std::string& caretLine,
                          Diagnostic& d) {
    if (first.empty()) return false;

    size_t caret = caretLine.find('^');
    if (caret == std::string::npos) return false;
    for (size_t i = 0; i < caret; ++i)
        if (caretLine[i] != ' ' && caretLine[i] != '\t') return false;   // not a caret line

    // "file:line: " - found from the right, so a Windows drive letter's colon
    // is never mistaken for the one that separates the line number.
    size_t after = first.rfind(": ");
    if (after == std::string::npos || after == 0) return false;
    std::string where = first.substr(0, after);
    size_t lineAt = where.rfind(':');
    if (lineAt == std::string::npos || lineAt == 0) return false;

    for (size_t i = lineAt + 1; i < where.size(); ++i)
        if (where[i] < '0' || where[i] > '9') return false;
    if (lineAt + 1 >= where.size()) return false;

    size_t lineNo = static_cast<size_t>(std::atol(where.c_str() + lineAt + 1));
    if (lineNo == 0) return false;

    size_t prefix = after + 2;                 // where the echoed source starts
    size_t col = caret >= prefix ? caret - prefix + 1 : 1;

    std::string message = caretLine.substr(caret + 1);
    size_t begin = message.find_first_not_of(" \t");
    message = (begin == std::string::npos) ? std::string() : message.substr(begin);
    if (message.empty()) return false;         // a caret with nothing to say is not one

    d.file = where.substr(0, lineAt);
    d.line = lineNo;
    d.col = col;
    d.message = message;
    d.present = true;
    return true;
}

Diagnostic parseDiagnostic(const std::string& text) {
    Diagnostic d;

    std::string previous;
    size_t at = 0;
    while (at <= text.size()) {
        size_t end = text.find('\n', at);
        std::string line = text.substr(at, end == std::string::npos ? std::string::npos
                                                                    : end - at);
        if (!line.empty() && line[line.size() - 1] == '\r') line.resize(line.size() - 1);

        if (parseGnu(line, d) || parseMsvc(line, d)) return d;
        if (parseCc1Preprocessor(previous, line, d)) return d;

        previous = line;
        if (end == std::string::npos) break;
        at = end + 1;
    }

    return d;
}

namespace {

// Runs a command with its errors joined to its output, handing the sink each
// line as it arrives, and gives back what the command exited with - or -1 when
// it could not be started at all. Everything this editor runs, compiler and
// built program alike, is run through here.
int runCaptured(const std::string& command, std::string& output,
                LineSink sink, void* context) {
    std::string cmd = command + " 2>&1";

#ifdef _WIN32
    // _popen hands the string to cmd /c, and cmd removes the first and last
    // quote when a command has both a quoted program and quoted arguments -
    // which every command here does, since paths hold spaces. An extra pair
    // around the whole thing is what cmd then eats, leaving the real ones
    // alone. Without this the compiler is never reached and cmd complains
    // instead: "The filename, directory name, or volume label syntax is
    // incorrect."
    cmd = "\"" + cmd + "\"";
#endif

    FILE* pipe = POPEN(cmd.c_str(), "r");
    if (!pipe) return -1;

    char chunk[512];
    std::string pending;
    while (std::fgets(chunk, sizeof chunk, pipe)) {
        output += chunk;
        if (!sink) continue;
        for (const char* p = chunk; *p; ++p) {
            if (*p == '\n') {
                sink(context, pending);
                pending.clear();
            } else if (*p != '\r') {
                pending += *p;
            }
        }
    }
    if (sink && !pending.empty()) sink(context, pending);

    int status = PCLOSE(pipe);
#ifndef _WIN32
    // pclose hands back what wait handed it, which is not the number the
    // program returned: the exit code is in the upper bits, and a program
    // killed by a signal never had one.
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
#endif
    return status;
}

// The shell reads its own words when a program is not there, and they differ
// per platform and explain nothing about how to fix it here.
// Whether the compiler could not be *started*, as against having run and
// failed. It used to look for "not found" anywhere in the output, which every
// undefined symbol says: "ld: symbol(s) not found for architecture arm64" got
// a program that had just run perfectly diagnosed as one that was not
// installed, and the advice that followed - name it with --cc1, put it on PATH
// - sent people looking in the wrong place entirely.
//
// So: the shell's own words, and nothing that only a running compiler or
// linker could have said.
bool looksLikeMissingProgram(const std::string& output) {
    bool shellSaidSo =
        output.find("command not found") != std::string::npos ||
        output.find("not recognized as an internal or external command") != std::string::npos ||
        output.find(": No such file or directory") != std::string::npos;
    if (!shellSaidSo) return false;

    const char* ranAfterAll[] = {"Undefined symbols", "symbol(s) not found", "ld: ",
                                 "LNK", "error:", "warning:"};
    for (size_t i = 0; i < sizeof ranAfterAll / sizeof *ranAfterAll; ++i)
        if (output.find(ranAfterAll[i]) != std::string::npos) return false;
    return true;
}

}  // namespace

Build build(const Toolchain& tool, ToolchainKind kind, const std::string& sourcePath,
            Language lang, const std::string& arch, Configuration config,
            LineSink sink, void* context) {
    Build result;

    // Puts this process into a Developer Command Prompt's environment if it is
    // not already in one, so that cl can be found when the editor was started
    // from an ordinary console. Nothing happens off Windows.
    if (!prepareFor(kind)) {
        result.output = "no Visual Studio 2022 found - cl cannot be run\n";
        if (sink) sink(context, "no Visual Studio 2022 found - cl cannot be run");
        return result;
    }

    Recipe recipe = assemblyRecipe(tool, kind, sourcePath, lang, arch, config);
    int status = runCaptured(recipe.command, result.output, sink, context);
    if (status < 0) {
        result.output = std::string("could not run ") + programOf(tool, kind);
        return result;
    }

    result.ok = (status == 0);
    result.diag = parseDiagnostic(result.output);

    if (!result.ok && !result.diag.present && looksLikeMissingProgram(result.output)) {
        std::string hint = std::string(programOf(tool, kind)) +
                           " could not be run - name it with --cc1 or --cl, or put it on PATH";
        result.output += hint + "\n";
        if (sink) sink(context, hint);
    }

    if (result.ok) {
        // stdio, not <fstream> - see the note in buffer.cpp: iostreams'
        // static initialisation makes a mixed native/managed binary die on
        // load, and reading a file of lines needs nothing streams provide.
        FILE* assembly = std::fopen(recipe.assemblyPath.c_str(), "rb");
        if (assembly) {
            std::string line;
            for (;;) {
                int c = std::fgetc(assembly);
                if (c == EOF) {
                    if (!line.empty()) result.asmLines.push_back(line);
                    break;
                }
                if (c == '\n') {
                    if (!line.empty() && line[line.size() - 1] == '\r')
                        line.resize(line.size() - 1);
                    result.asmLines.push_back(line);
                    line.clear();
                    continue;
                }
                line += static_cast<char>(c);
            }
            std::fclose(assembly);
        }
    }

    std::remove(recipe.assemblyPath.c_str());
    for (size_t i = 0; i < recipe.leftovers.size(); ++i)
        std::remove(recipe.leftovers[i].c_str());

    return result;
}

Built buildProgram(const Toolchain& tool, ToolchainKind kind, const std::string& sourcePath,
                   Language lang, const std::string& arch, Configuration config,
                   LineSink sink, void* context) {
    Built result;

    if (!prepareFor(kind)) {
        result.output = "no Visual Studio 2022 found - cl cannot be run\n";
        if (sink) sink(context, "no Visual Studio 2022 found - cl cannot be run");
        return result;
    }

    Recipe recipe = programRecipe(tool, kind, sourcePath, lang, arch, config);
    result.program = recipe.assemblyPath;
    result.leftovers = recipe.leftovers;

    int made = runCaptured(recipe.command, result.output, sink, context);
    if (made < 0) {
        result.output = std::string("could not run ") + programOf(tool, kind);
        return result;
    }

    result.diag = parseDiagnostic(result.output);
    result.ok = (made == 0);

    if (!result.ok && !result.diag.present && looksLikeMissingProgram(result.output)) {
        std::string hint = std::string(programOf(tool, kind)) +
                           " could not be run - name it with --cc1 or --cl, or put it on PATH";
        result.output += hint + "\n";
        if (sink) sink(context, hint);
    }
    return result;
}

Built buildTarget(const Toolchain& tool, ToolchainKind kind,
                  const std::vector<std::string>& sources, Language lang,
                  const std::string& arch, Configuration config,
                  const std::string& program, LineSink sink, void* context) {
    Built result;

    if (sources.empty()) {
        result.output = "nothing to build\n";
        return result;
    }

    if (!prepareFor(kind)) {
        result.output = "no Visual Studio 2022 found - cl cannot be run\n";
        if (sink) sink(context, "no Visual Studio 2022 found - cl cannot be run");
        return result;
    }

    Recipe recipe = targetRecipe(tool, kind, sources, lang, arch, config, program);
    result.program = recipe.assemblyPath;
    result.leftovers = recipe.leftovers;

    int made = runCaptured(recipe.command, result.output, sink, context);
    if (made < 0) {
        result.output = std::string("could not run ") + programOf(tool, kind);
        return result;
    }

    result.diag = parseDiagnostic(result.output);
    result.ok = (made == 0);

    if (!result.ok && !result.diag.present && looksLikeMissingProgram(result.output)) {
        std::string hint = std::string(programOf(tool, kind)) +
                           " could not be run - name it with --cc1 or --cl, or put it on PATH";
        result.output += hint + "\n";
        if (sink) sink(context, hint);
    }

    // The objects and the directory they went in are the editor's mess; the
    // program is the point and stays where it was asked for.
    for (size_t i = 0; i < result.leftovers.size(); ++i)
        std::remove(result.leftovers[i].c_str());
    result.leftovers.clear();
    return result;
}

Ran runBuilt(const std::string& program, LineSink sink, void* context) {
    Ran result;
    if (program.empty()) return result;

    // Its input is emptied for the same reason as above: a program that reads
    // would otherwise eat the editor's own keys.
#ifdef _WIN32
    const char* noInput = " < NUL";
#else
    const char* noInput = " < /dev/null";
#endif
    result.built = true;
    result.ran = true;
    result.status = runCaptured("\"" + program + "\"" + noInput, result.output, sink, context);
    return result;
}

void removeProgram(const Built& built) {
    if (!built.program.empty()) std::remove(built.program.c_str());
    for (size_t i = 0; i < built.leftovers.size(); ++i)
        std::remove(built.leftovers[i].c_str());
}

Ran runProgram(const Toolchain& tool, ToolchainKind kind, const std::string& sourcePath,
               Language lang, const std::string& arch, Configuration config,
               LineSink sink, void* context) {
    Ran result;

    Built made = buildProgram(tool, kind, sourcePath, lang, arch, config, sink, context);
    result.output = made.output;
    result.diag = made.diag;
    result.built = made.ok;

    if (result.built) {
        // Its input is emptied rather than left as the editor's own, which in a
        // terminal is the keyboard and under a test is the rest of the keys.
        // A program that reads would otherwise eat what it was never sent.
#ifdef _WIN32
        const char* noInput = " < NUL";
#else
        const char* noInput = " < /dev/null";
#endif
        result.ran = true;
        result.status = runCaptured("\"" + made.program + "\"" + noInput,
                                    result.output, sink, context);
    }

    removeProgram(made);
    return result;
}

}  // namespace editor
