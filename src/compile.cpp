#include "compile.h"

#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#define POPEN  _popen
#define PCLOSE _pclose
#else
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

Diagnostic parseDiagnostic(const std::string& text) {
    Diagnostic d;

    size_t at = 0;
    while (at <= text.size()) {
        size_t end = text.find('\n', at);
        std::string line = text.substr(at, end == std::string::npos ? std::string::npos
                                                                    : end - at);
        if (!line.empty() && line[line.size() - 1] == '\r') line.resize(line.size() - 1);

        if (parseGnu(line, d) || parseMsvc(line, d)) return d;

        if (end == std::string::npos) break;
        at = end + 1;
    }

    return d;
}

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
    std::string cmd = recipe.command + " 2>&1";

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
    if (!pipe) {
        result.output = std::string("could not run ") + programOf(tool, kind);
        return result;
    }

    char chunk[512];
    std::string pending;
    while (std::fgets(chunk, sizeof chunk, pipe)) {
        result.output += chunk;
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
    result.ok = (status == 0);
    result.diag = parseDiagnostic(result.output);

    // A shell that could not find the program says so in its own words, which
    // differ per platform and explain nothing about how to fix it here.
    if (!result.ok && !result.diag.present &&
        (result.output.find("not found") != std::string::npos ||
         result.output.find("not recognized") != std::string::npos ||
         result.output.find("No such file") != std::string::npos)) {
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

}  // namespace editor
