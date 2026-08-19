#include "toolchain.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#define POPEN  _popen
#define PCLOSE _pclose
const char kSep = '\\';
#else
#define POPEN  popen
#define PCLOSE pclose
const char kSep = '/';
#endif

namespace editor {

namespace {

std::string tempDir() {
#ifdef _WIN32
    const char* t = std::getenv("TEMP");
    if (!t) t = std::getenv("TMP");
    return t ? t : ".";
#else
    const char* t = std::getenv("TMPDIR");
    return t ? t : "/tmp";
#endif
}

std::string quote(const std::string& s) { return "\"" + s + "\""; }

#ifdef _WIN32
// cmd removes the first and last quote when a command has both a quoted program
// and quoted arguments. An extra pair around the whole thing is what it eats
// instead, leaving the real ones alone.
std::string forCmd(const std::string& s) { return "\"" + s + "\""; }

std::string firstLineOf(const std::string& command) {
    FILE* pipe = POPEN(command.c_str(), "r");
    if (!pipe) return std::string();

    char buffer[1024];
    std::string line;
    if (std::fgets(buffer, sizeof buffer, pipe)) line = buffer;
    PCLOSE(pipe);

    while (!line.empty() && (line[line.size() - 1] == '\n' || line[line.size() - 1] == '\r'))
        line.resize(line.size() - 1);
    return line;
}

std::string findVcvars() {
    const char* programFiles = std::getenv("ProgramFiles(x86)");
    if (!programFiles) return std::string();

    std::string vswhere = std::string(programFiles) +
                          "\\Microsoft Visual Studio\\Installer\\vswhere.exe";

    // Pinned to Visual Studio 2022, exactly as build.bat is: a bare -latest
    // reaches past it to a newer Visual Studio if one is installed, and that is
    // not the toolset any of this is built with.
    std::string where = firstLineOf(forCmd(
        quote(vswhere) + " -latest -products * -version \"[17.0,18.0)\""
                         " -property installationPath"));
    if (where.empty()) return std::string();

    return where + "\\VC\\Auxiliary\\Build\\vcvars64.bat";
}

// Runs vcvars64 once and copies the environment it produced into this process.
// The alternative - calling vcvars in front of every build - costs a second or
// two on each one, for a result that never changes while the editor is open.
bool importMsvcEnvironment() {
    static int done = 0;   // 0 not tried, 1 succeeded, -1 failed
    if (done != 0) return done == 1;

    if (std::getenv("VSCMD_ARG_TGT_ARCH")) {   // already in a Developer prompt
        done = 1;
        return true;
    }

    std::string bat = findVcvars();
    if (bat.empty()) {
        done = -1;
        return false;
    }

    std::string command = forCmd("call " + quote(bat) + " >nul && set");
    FILE* pipe = POPEN(command.c_str(), "r");
    if (!pipe) {
        done = -1;
        return false;
    }

    char line[4096];
    int taken = 0;
    while (std::fgets(line, sizeof line, pipe)) {
        char* eq = std::strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';

        char* value = eq + 1;
        size_t n = std::strlen(value);
        while (n > 0 && (value[n - 1] == '\n' || value[n - 1] == '\r')) value[--n] = '\0';

        if (_putenv_s(line, value) == 0) ++taken;
    }
    PCLOSE(pipe);

    done = (taken > 0) ? 1 : -1;
    return done == 1;
}
#endif

}  // namespace

ToolchainKind resolve(const Toolchain& tool, Language lang) {
    if (tool.kind != ToolAuto) return tool.kind;
    // cc1 is a C compiler. Everything it cannot take goes to the one that can.
    return (lang == LangCpp) ? ToolMsvc : ToolCc1;
}

const char* toolchainName(ToolchainKind kind) {
    switch (kind) {
        case ToolMsvc: return "cl";
        case ToolCc1:  return "cc1";
        default:       return "auto";
    }
}

const char* programOf(const Toolchain& tool, ToolchainKind kind) {
    return (kind == ToolMsvc) ? tool.cl.c_str() : tool.cc1.c_str();
}

bool usesArch(ToolchainKind kind) { return kind == ToolCc1; }

const char* configName(Configuration config) {
    return config == ConfigRelease ? "release" : "debug";
}

bool optimises(ToolchainKind kind) { return kind == ToolMsvc; }

// The names are written out rather than taken from kArches, which lives above
// this file and cannot be reached from it. There are three of them and they do
// not move.
bool emitsDebugInfo(ToolchainKind kind, const std::string& arch) {
    if (kind != ToolCc1) return false;
    return arch == "x86_64-linux" || arch == "arm64-darwin";
}

std::string configFlags(ToolchainKind kind, Configuration config,
                        const std::string& arch) {
    if (kind == ToolMsvc)
        return config == ConfigRelease ? " /O2 /DNDEBUG" : " /Od /D_DEBUG";

    // cc1 has no optimiser, so release is the define and nothing else. Debug is
    // more than a define wherever cc1 can write the debug information.
    if (config == ConfigRelease) return " -DNDEBUG=1";
    return emitsDebugInfo(kind, arch) ? " -g -D_DEBUG=1" : " -D_DEBUG=1";
}

std::vector<std::string> debugNote(ToolchainKind kind, const std::string& arch) {
    std::vector<std::string> said;
    if (emitsDebugInfo(kind, arch)) {
        said.push_back("cc1 writes DWARF for " + arch + " - line tables, types, objects and");
        said.push_back("lexical blocks - so a debugger has something to read here. This");
        said.push_back("editor is not that debugger: it builds to assembly and stops, so");
        said.push_back("nothing has been assembled, linked or run. What the build did leave");
        said.push_back("behind is the assembly, and this is what is in it.");
    } else if (kind == ToolCc1) {
        said.push_back("cc1 writes no debug information for " + arch + ": it generates MASM");
        said.push_back("there, and MASM carries no line table. So there is nothing to step");
        said.push_back("through. This is what the build produced, read back out of its own");
        said.push_back("assembly.");
    } else {
        said.push_back("cl is not asked for /Zi here, so this build carries no debug");
        said.push_back("information either. This is what it produced, read back out of its");
        said.push_back("own assembly.");
    }
    return said;
}

bool canCompile(ToolchainKind kind, Language lang) {
    if (lang == LangCpp) return kind == ToolMsvc;
    if (lang == LangC) return true;
    return false;   // assembly and plain text are not compiled from here
}

std::string refusal(ToolchainKind kind, Language lang) {
    if (lang == LangCpp && kind == ToolCc1)
        return "cc1 compiles C, not C++ - Ctrl-K for automatic, and it picks cl";
    if (lang != LangC && lang != LangCpp)
        return std::string("nothing to compile: this is ") + languageName(lang) +
               ", not C or C++";
    return "cannot compile this file";
}

const char* hostArch() {
#if defined(_WIN32)
    return "x86_64-windows";
#elif defined(__APPLE__)
    return "arm64-darwin";
#else
    return "x86_64-linux";
#endif
}

bool runsHere(ToolchainKind kind, const std::string& arch) {
    // cl generates for the machine it was installed on and takes no target
    // from this editor at all, so whatever the target menu says, what cl
    // builds is what this machine runs.
    if (kind == ToolMsvc) return true;
    return arch == hostArch();
}

std::string whyNotRun(ToolchainKind kind, const std::string& arch) {
    if (runsHere(kind, arch)) return std::string();
    return arch + " only reaches -S here - switch to " + hostArch() + " to run it";
}

// Where a built program is put, and what it is called. Beside the assembly
// rather than beside the source: a directory that is checked into something
// should not fill up with what the editor made while looking at it.
namespace {
std::string programPath() {
    std::string path = tempDir() + kSep + "ed1-run";
#ifdef _WIN32
    path += ".exe";
#endif
    return path;
}
}  // namespace

Recipe programRecipe(const Toolchain& tool, ToolchainKind kind,
                     const std::string& source, Language lang,
                     const std::string& arch, Configuration config) {
    Recipe recipe;
    std::string program = programOf(tool, kind);
    recipe.assemblyPath = programPath();   // the program, which is what this makes

    if (kind == ToolMsvc) {
        // Without /c, cl compiles and links. /Fe names the program and /Fo the
        // object it goes through; the object is the editor's mess to clear up.
        std::string obj = tempDir() + kSep + "ed1-run.obj";
        std::string forLanguage = (lang == LangCpp) ? " /TP /EHsc /std:c++17" : " /TC";
        recipe.command = quote(program) + " /nologo /diagnostics:column" + forLanguage +
                         configFlags(kind, config, arch) +
                         " /Fe" + quote(recipe.assemblyPath) +
                         " /Fo" + quote(obj) + " " + quote(source);
        recipe.leftovers.push_back(obj);
        return recipe;
    }

    // With neither -S nor -c, cc1 compiles, assembles and links. -arch is left
    // off rather than passed as the host's own: the host is what it does by
    // default, and naming it would only invite a cross target to be named here
    // too, which would stop at the assembly and produce nothing to run.
    recipe.command = quote(program) + " " + quote(source) + " -o " +
                     quote(recipe.assemblyPath) + configFlags(kind, config, arch);
    return recipe;
}

std::string shownProgramCommand(const Toolchain& tool, ToolchainKind kind,
                                const std::string& source, Language lang,
                                const std::string& arch, Configuration config) {
    std::string program = programOf(tool, kind);
    if (kind == ToolMsvc)
        return program + " /diagnostics:column" +
               ((lang == LangCpp) ? " /TP /EHsc /std:c++17" : " /TC") +
               configFlags(kind, config, arch) + " /Feed1-run " + source;
    return program + " " + source + " -o ed1-run" + configFlags(kind, config, arch);
}

Recipe assemblyRecipe(const Toolchain& tool, ToolchainKind kind,
                      const std::string& source, Language lang,
                      const std::string& arch, Configuration config) {
    Recipe recipe;
    std::string stem = tempDir() + kSep + "ed1-build";
    std::string program = programOf(tool, kind);

    if (kind == ToolMsvc) {
        recipe.assemblyPath = stem + ".asm";
        std::string obj = stem + ".obj";

        // The language is stated rather than left to the suffix. /TC and /TP
        // are what stop cl guessing, and they let a header or an oddly named
        // file be compiled as whatever the editor decided it was.
        std::string forLanguage = (lang == LangCpp) ? " /TP /EHsc /std:c++17" : " /TC";

        // /diagnostics:column is what turns 'bad.c(3)' into 'bad.c(3,13)'; the
        // editor wants the column, and cl gives none without being asked.
        recipe.command = quote(program) + " /nologo /c /diagnostics:column /FAs" +
                         forLanguage + configFlags(kind, config, arch) +
                         " /Fa" + quote(recipe.assemblyPath) +
                         " /Fo" + quote(obj) + " " + quote(source);
        recipe.leftovers.push_back(obj);
        return recipe;
    }

    recipe.assemblyPath = stem + ".s";
    recipe.command = quote(program) + " -S " + quote(source) + " -o " +
                     quote(recipe.assemblyPath) + " -arch " + arch +
                     configFlags(kind, config, arch);
    return recipe;
}

std::string shownCommand(const Toolchain& tool, ToolchainKind kind,
                         const std::string& source, Language lang,
                         const std::string& arch, Configuration config) {
    std::string program = programOf(tool, kind);
    if (kind == ToolMsvc)
        return program + " /c /diagnostics:column /FAs" +
               ((lang == LangCpp) ? " /TP /EHsc /std:c++17" : " /TC") +
               configFlags(kind, config, arch) + " " + source;
    return program + " -S " + source + " -arch " + arch +
           configFlags(kind, config, arch);
}

bool prepareFor(ToolchainKind kind) {
#ifdef _WIN32
    if (kind == ToolMsvc) return importMsvcEnvironment();
    return true;
#else
    (void)kind;
    return true;
#endif
}

}  // namespace editor
