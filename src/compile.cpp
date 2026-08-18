#include "compile.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>

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

Build build(const Toolchain& tool, const std::string& sourcePath,
            const std::string& arch, LineSink sink, void* context) {
    Build result;

    Recipe recipe = assemblyRecipe(tool, sourcePath, arch);
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
        result.output = "could not run " + tool.program;
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

    if (result.ok) {
        std::ifstream in(recipe.assemblyPath.c_str());
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line[line.size() - 1] == '\r') line.resize(line.size() - 1);
            result.asmLines.push_back(line);
        }
    }

    std::remove(recipe.assemblyPath.c_str());
    for (size_t i = 0; i < recipe.leftovers.size(); ++i)
        std::remove(recipe.leftovers[i].c_str());

    return result;
}

}  // namespace editor
