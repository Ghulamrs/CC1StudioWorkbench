#include "toolchain.h"

#include "path.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#define POPEN  _popen
#define PCLOSE _pclose
const char kSep = '\\';
#else
#include <unistd.h>
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

// A directory for /Fo, which wants a separator on the end of it - and that
// separator is a backslash on the machine where /Fo means anything.
//
// A backslash immediately before a closing quote escapes the quote: the C
// runtime's own argument rules say 2n backslashes then a quote is n
// backslashes and a delimiter, so `/Fo"C:\dir\"` reaches cl as one argument
// with everything after it swallowed - and cl answers "D8003: missing source
// filename", which reads as a command with no file in it rather than a
// command with a quote in the wrong place. Doubling it is the whole fix:
// `/Fo"C:\dir\\"` reaches cl as `/FoC:\dir\`.
// What to tell a gcc-style compiler the language is, rather than leaving it to
// the suffix - the same job /TC and /TP do for cl, and needed for the same
// reason: a header or an oddly named file is compiled as whatever the editor
// decided it was. Empty for cc1, which reads C and only C and has no such flag.
std::string languageFlag(ToolchainKind kind, Language lang) {
    if (kind != ToolCxx) return std::string();
    return (lang == LangCpp) ? " -x c++" : " -x c";
}

std::string quoteDirectory(const std::string& s) {
    std::string path = s;
    if (!path.empty() && path[path.size() - 1] == kSep) path += kSep;
    return quote(path);
}

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

const char* hostCxxName() {
#if defined(_WIN32)
    return "cl";
#elif defined(__APPLE__)
    return "clang++";
#else
    return "g++";
#endif
}

ToolchainKind hostCppToolchain() {
#ifdef _WIN32
    return ToolMsvc;
#else
    return ToolCxx;
#endif
}

ToolchainKind resolve(const Toolchain& tool, Language lang) {
    if (tool.kind != ToolAuto) return tool.kind;
    if (lang == LangShalimar) return ToolShc;
    // cc1 is a C compiler and is what this editor was written for, so C is
    // its. C++ goes to whichever C++ compiler this machine has - cl where
    // there is one, the host's otherwise. That used to say ToolMsvc
    // everywhere, which meant a C++ file on a Mac was routed to a compiler
    // that is not installed there and never could be.
    return (lang == LangCpp) ? hostCppToolchain() : ToolCc1;
}

const char* toolchainName(ToolchainKind kind) {
    switch (kind) {
        case ToolMsvc: return "cl";
        case ToolCc1:  return "cc1";
        case ToolShc:  return "shc";
        case ToolCxx:  return "c++";
        default:       return "auto";
    }
}

std::string toolchainShown(const Toolchain& tool, ToolchainKind kind) {
    if (kind != ToolCxx) return toolchainName(kind);
    // The leaf, not the path: --cxx may name /usr/bin/g++-11 and the console
    // has eighty columns.
    return path::filename(tool.cxx);
}

const char* programOf(const Toolchain& tool, ToolchainKind kind) {
    if (kind == ToolMsvc) return tool.cl.c_str();
    if (kind == ToolShc) return tool.shc.c_str();
    if (kind == ToolCxx) return tool.cxx.c_str();
    return tool.cc1.c_str();
}

// cc1 and shc both generate for the same three; cl generates for the one it
// was installed as, and offering a choice that does nothing would be the
// status bar telling a lie.
bool usesArch(ToolchainKind kind) { return kind == ToolCc1 || kind == ToolShc; }

const char* configName(Configuration config) {
    return config == ConfigRelease ? "release" : "debug";
}

bool optimises(ToolchainKind kind) { return kind == ToolMsvc || kind == ToolCxx; }

// The names are written out rather than taken from kArches, which lives above
// this file and cannot be reached from it. There are three of them and they do
// not move.
bool emitsDebugInfo(ToolchainKind kind, const std::string& arch) {
    // cl has always been able to. What was missing was being asked.
    if (kind == ToolMsvc) return true;
    // And the host's C++ compiler under -g, which is where its own debugger
    // reads from - the same DWARF cc1 learned to write.
    if (kind == ToolCxx) return true;
    // shc writes none for any target, and that is settled rather than
    // pending: see the Known limitations in ../Compiler-S/README.md.
    if (kind != ToolCc1) return false;
    return arch == "x86_64-linux" || arch == "arm64-darwin";
}

std::string configFlags(ToolchainKind kind, Configuration config,
                        const std::string& arch) {
    // Shalimar has no preprocessor, so there is no define to set either way,
    // and shc emits no debug information for any target by decision. What it
    // does have is --debug, and it is not a flag about the code: the assembly
    // is byte-identical between the two and what changes is which runtime
    // archive is linked - the debug one can stop the program, the release one
    // has no code for it at all. So the configuration is real here, it is
    // just real about something else.
    //
    // This used to return nothing for shc and say debug and release were the
    // same program. They were, until shc grew --debug; F8 on a Shalimar file
    // then built a release binary and found nothing in it to stop.
    if (kind == ToolShc)
        return config == ConfigDebug ? std::string(" --debug") : std::string();

    // The host's own compiler has both halves for real: an optimiser and
    // DWARF. It is the only one of the four where release means the code is
    // different *and* debug means a debugger can read it.
    if (kind == ToolCxx)
        return config == ConfigRelease ? " -O2 -DNDEBUG=1" : " -g -D_DEBUG=1";

    // /Zi is what makes cl's debug build a debug build. Without it the word
    // meant the optimiser was off and a macro was defined, and nothing that
    // could stop on a line - the same thing that used to be wrong for cc1.
    if (kind == ToolMsvc)
        return config == ConfigRelease ? " /O2 /DNDEBUG" : " /Od /Zi /D_DEBUG";

    // cc1 has no optimiser, so release is the define and nothing else. Debug is
    // more than a define wherever cc1 can write the debug information.
    if (config == ConfigRelease) return " -DNDEBUG=1";
    return emitsDebugInfo(kind, arch) ? " -g -D_DEBUG=1" : " -D_DEBUG=1";
}

std::vector<std::string> debugNote(ToolchainKind kind, const std::string& arch) {
    std::vector<std::string> said;
    if (kind == ToolCxx) {
        said.push_back("This is the machine's own C++ compiler, so a debug build has real");
        said.push_back("DWARF in it and lldb or gdb reads it - there was never a question");
        said.push_back("about that one. What is below is the assembly the build produced,");
        said.push_back("read back out of itself; the editor stops at -S and assembles");
        said.push_back("nothing, so nothing has been linked or run.");
    } else if (emitsDebugInfo(kind, arch)) {
        said.push_back("cc1 writes DWARF for " + arch + " - line tables, types, objects and");
        said.push_back("lexical blocks - so a debugger has something to read here. This");
        said.push_back("editor is not that debugger: it builds to assembly and stops, so");
        said.push_back("nothing has been assembled, linked or run. What the build did leave");
        said.push_back("behind is the assembly, and this is what is in it.");
    } else if (kind == ToolShc) {
        said.push_back("shc writes no debug information for any target, and that is a");
        said.push_back("decision rather than a gap: a Shalimar program carries its own");
        said.push_back("position instead - shm_line before every statement, in every");
        said.push_back("build - which is what names the line of a runtime error and what");
        said.push_back("F8 stops on. So there is a debugger here and no debug format. What");
        said.push_back("it cannot do is read a variable. This is the assembly the build");
        said.push_back("produced, read back out of itself.");
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
    if (lang == LangShalimar) return kind == ToolShc;
    if (kind == ToolShc) return false;   // shc compiles Shalimar and nothing else
    if (lang == LangCpp) return kind == ToolMsvc || kind == ToolCxx;
    if (lang == LangC) return true;
    return false;   // assembly and plain text are not compiled from here
}

std::string refusal(ToolchainKind kind, Language lang) {
    if (lang == LangShalimar && kind != ToolShc)
        return std::string(toolchainName(kind)) +
               " does not compile Shalimar - Ctrl-K for automatic, and it picks shc";
    if (kind == ToolShc && lang != LangShalimar)
        return std::string("shc compiles Shalimar, not ") + languageName(lang) +
               " - Ctrl-K for automatic";
    if (lang == LangCpp && kind == ToolCc1)
        return std::string("cc1 compiles C, not C++ - Ctrl-K for automatic, and it picks ") +
               toolchainName(hostCppToolchain());
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
    // Same for the host's C++ compiler, and for the same reason: it generates
    // for the machine it is on and this editor never hands it a target.
    if (kind == ToolMsvc || kind == ToolCxx) return true;
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

// A working name of this editor's own. The process id is in it because the
// name used to be fixed, and two editors - or an editor and the test suite -
// then wrote to the same file. That is not hypothetical: a screenshot run and
// the suite built at the same moment, one held the program the other was
// linking, and the failure read as a compiler that could not build C++.
std::string mine(const char* what) {
    char id[32];
#ifdef _WIN32
    std::snprintf(id, sizeof id, "%lu", static_cast<unsigned long>(GetCurrentProcessId()));
#else
    std::snprintf(id, sizeof id, "%ld", static_cast<long>(getpid()));
#endif
    return tempDir() + kSep + what + "-" + id;
}

std::string programPath() {
    std::string path = mine("ed1-run");
#ifdef _WIN32
    path += ".exe";
#endif
    return path;
}
}  // namespace

// A program from several sources, left where it was asked for.
Recipe targetRecipe(const Toolchain& tool, ToolchainKind kind,
                    const std::vector<std::string>& sources, Language lang,
                    const std::string& arch, Configuration config,
                    const std::string& program) {
    Recipe recipe;
    recipe.assemblyPath = program;

    std::string named;
    for (size_t i = 0; i < sources.size(); ++i) named += " " + quote(sources[i]);

    if (kind == ToolMsvc) {
        // /Fo has to name a directory when there is more than one input, since
        // one object comes out per source. The objects are named after the
        // sources, so what to remove afterwards is known without looking.
        std::string objects = mine("ed1-objs");
        path::makeDirectories(objects);
        std::string forLanguage = (lang == LangCpp) ? " /TP /EHsc /std:c++14" : " /TC";
        std::string pdb = path::join(objects, "ed1-target.pdb");

        recipe.command = quote(programOf(tool, kind)) + " /nologo /diagnostics:column" +
                         forLanguage + configFlags(kind, config, arch) +
                         (config == ConfigDebug ? " /Fd" + quote(pdb) : std::string()) +
                         " /Fe" + quote(program) +
                         " /Fo" + quoteDirectory(objects + kSep) + named +
                         (config == ConfigDebug ? " /link /DEBUG" : std::string());

        for (size_t i = 0; i < sources.size(); ++i) {
            std::string leaf = path::filename(sources[i]);
            size_t dot = leaf.find_last_of('.');
            if (dot != std::string::npos) leaf.resize(dot);
            recipe.leftovers.push_back(path::join(objects, leaf + ".obj"));
        }
        if (config == ConfigDebug) recipe.leftovers.push_back(pdb);
        recipe.leftovers.push_back(objects);
        return recipe;
    }

    // The program first, then the files it may reach into. shc looks for a
    // function it was not given in the files named after the program, and
    // beside it when none are - so naming the project's own is what stops it
    // finding something in a directory the project does not claim.
    //
    // The project has already narrowed this to one program; the rest of the
    // group is where to look.
    if (kind == ToolShc) {
        recipe.command = quote(programOf(tool, kind)) + named + " -o " + quote(program);
        return recipe;
    }

    // cc1 compiles, assembles and links the lot when it is given neither -S
    // nor -c, and -arch is left off for the same reason as below.
    recipe.command = quote(programOf(tool, kind)) + languageFlag(kind, lang) + named +
                     " -o " + quote(program) + configFlags(kind, config, arch);
    return recipe;
}

// The name of a source with its suffix taken off and its directory dropped:
// what both compilers call the object they make of it, and so what to name and
// what to clear away.
namespace {

std::string objectFor(const std::string& dir, const std::string& source,
                      const char* suffix) {
    std::string leaf = path::filename(source);
    size_t dot = leaf.find_last_of('.');
    if (dot != std::string::npos) leaf.resize(dot);
    return path::join(dir, leaf + suffix);
}

// The same three variables cc1 reads, and for the same reason: whichever
// assembler and linker this machine reaches cc1 through are the ones that have
// to take what cc1 wrote. A machine where cc1 needs to be told is a machine
// where this does too, and two different answers would be worse than none.
const char* hostDriver() {
    const char* named = std::getenv("CC1_CC");
    return (named && *named) ? named : "cc";
}

const char* hostLinker() {
    const char* named = std::getenv("CC1_LD");
    return (named && *named) ? named : "link.exe";
}

// CXX is the variable every make on earth reads for this, so it is the one
// read here. Failing that, the machine's own - clang++ or g++ by name.
const char* hostCppDriver() {
    const char* named = std::getenv("CXX");
    return (named && *named) ? named : hostCxxName();
}

}  // namespace

// Off Windows the link goes through a compiler driver, and which one is not a
// detail: a program with C++ in it needs the C++ runtime and the personality
// routine for exceptions, and cc names neither. c++ does, and links plain C
// perfectly well - so the rule is "c++ when any of it was C++", which is what
// withCpp is for. On Windows the linker is named directly, because cl's objects
// carry /DEFAULTLIB directives that say all of this for themselves.
const char* linkerNameFor(bool windows, bool withCpp) {
    if (windows) return hostLinker();
    if (withCpp) return hostCppDriver();
    return hostDriver();
}

std::string linkerName(bool withCpp) {
#ifdef _WIN32
    return linkerNameFor(true, withCpp);
#else
    return linkerNameFor(false, withCpp);
#endif
}

Recipe objectRecipe(const Toolchain& tool, ToolchainKind kind,
                    const std::vector<std::string>& sources, Language lang,
                    const std::string& arch, Configuration config,
                    const std::string& objectDir, std::vector<std::string>& objects) {
    Recipe recipe;
    objects.clear();

    std::string named;
    for (size_t i = 0; i < sources.size(); ++i) named += " " + quote(sources[i]);

    if (kind == ToolMsvc) {
        // /MT rather than the default /MD, because these objects are going to
        // be linked beside cc1's and cc1's driver names libcmt. Two CRTs in one
        // program is LNK4098 at best and two heaps at worst, and the editor is
        // the only thing here in a position to make them agree.
        std::string forLanguage = (lang == LangCpp) ? " /TP /EHsc /std:c++14" : " /TC";
        std::string crt = (config == ConfigDebug) ? " /MTd" : " /MT";
        std::string pdb = path::join(objectDir, "ed1-target.pdb");

        recipe.command = quote(programOf(tool, kind)) + " /nologo /diagnostics:column /c" +
                         forLanguage + crt + configFlags(kind, config, arch) +
                         (config == ConfigDebug ? " /Fd" + quote(pdb) : std::string()) +
                         " /Fo" + quoteDirectory(objectDir + kSep) + named;

        for (size_t i = 0; i < sources.size(); ++i)
            objects.push_back(objectFor(objectDir, sources[i], ".obj"));
        recipe.leftovers = objects;
        if (config == ConfigDebug) recipe.leftovers.push_back(pdb);
        return recipe;
    }

    // cc1 -c writes one object per input, named after the input, in the
    // *current directory* - which is the editor's, not the one wanted here. So
    // the compiler is run from the object directory rather than told about it,
    // which is the one thing it has no flag for. The sources are absolute, so
    // moving the working directory does not lose them. cc and c++ do exactly
    // the same thing with -c and several inputs, which is where cc1 got it.
    recipe.command = "cd " + quote(objectDir) + " && " +
                     quote(programOf(tool, kind)) + " -c" +
                     languageFlag(kind, lang) + named +
                     configFlags(kind, config, arch);

    for (size_t i = 0; i < sources.size(); ++i)
        objects.push_back(objectFor(objectDir, sources[i], ".o"));
    recipe.leftovers = objects;
    return recipe;
}

Recipe linkRecipe(const Toolchain& tool, const std::vector<std::string>& objects,
                  bool withCpp, const std::string& arch, Configuration config,
                  const std::string& program) {
    (void)tool;
    (void)arch;
    Recipe recipe;
    recipe.assemblyPath = program;

    std::string named;
    for (size_t i = 0; i < objects.size(); ++i) named += " " + quote(objects[i]);

#ifdef _WIN32
    (void)withCpp;
    // The C runtime is named because cc1's objects do not name it. cl's do -
    // it writes /DEFAULTLIB directives into every object - and the linker takes
    // both without complaint as long as they agree, which is what /MT above is
    // for. legacy_stdio_definitions is not optional for anything that formats
    // into a buffer: the UCRT made printf an inline wrapper over
    // __stdio_common_*, and a compiler that declares it as the ordinary
    // function C says it is - which cc1 does, correctly - has nothing to link
    // against without it.
    const char* crt = (config == ConfigDebug)
                          ? " libcmtd.lib libucrtd.lib libvcruntimed.lib"
                          : " libcmt.lib libucrt.lib libvcruntime.lib";
    recipe.command = quote(linkerNameFor(true, withCpp)) +
                     " /nologo /subsystem:console" +
                     (config == ConfigDebug ? std::string(" /DEBUG") : std::string()) +
                     " /out:" + quote(program) + named + crt +
                     " kernel32.lib legacy_stdio_definitions.lib";
#else
    // -g here is not about this link's own output: it is what makes the host
    // driver run dsymutil, which gathers the DWARF out of the objects into a
    // .dSYM. Without it a Mac's debug map points at objects that are removed
    // the moment the link finishes, and the program is not debuggable however
    // good the assembly was. -lm is passed always rather than guessed at,
    // which is what cc1's own driver does.
    recipe.command = quote(linkerNameFor(false, withCpp)) +
                     (config == ConfigDebug ? std::string(" -g") : std::string()) +
                     named + " -o " + quote(program) + " -lm";
#endif
    return recipe;
}

Recipe programRecipe(const Toolchain& tool, ToolchainKind kind,
                     const std::string& source, Language lang,
                     const std::string& arch, Configuration config) {
    Recipe recipe;
    std::string program = programOf(tool, kind);
    recipe.assemblyPath = programPath();   // the program, which is what this makes

    if (kind == ToolMsvc) {
        // Without /c, cl compiles and links. /Fe names the program and /Fo the
        // object it goes through; the object is the editor's mess to clear up.
        std::string obj = mine("ed1-run") + ".obj";
        std::string forLanguage = (lang == LangCpp) ? " /TP /EHsc /std:c++14" : " /TC";
        // The .pdb goes where the program goes rather than beside the source,
        // and the linker is told as well as the compiler - /Zi alone describes
        // the object, and /DEBUG is what puts it in the program.
        std::string pdb = mine("ed1-run") + ".pdb";
        recipe.command = quote(program) + " /nologo /diagnostics:column" + forLanguage +
                         configFlags(kind, config, arch) +
                         (config == ConfigDebug ? " /Fd" + quote(pdb) : std::string()) +
                         " /Fe" + quote(recipe.assemblyPath) +
                         " /Fo" + quote(obj) + " " + quote(source) +
                         (config == ConfigDebug ? " /link /DEBUG" : std::string());
        recipe.leftovers.push_back(obj);
        if (config == ConfigDebug) {
            recipe.leftovers.push_back(pdb);
            recipe.leftovers.push_back(mine("ed1-run") + ".ilk");
        }
        return recipe;
    }

    // With neither -S nor -c, cc1 and shc both compile, assemble and link.
    // The target is left off rather than passed as the host's own: the host is
    // what either does by default, and naming it would only invite a cross
    // target to be named here too, which would stop at the assembly and
    // produce nothing to run.
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
               ((lang == LangCpp) ? " /TP /EHsc /std:c++14" : " /TC") +
               configFlags(kind, config, arch) + " /Feed1-run " + source;
    return program + " " + source + " -o ed1-run" + configFlags(kind, config, arch);
}

Recipe assemblyRecipe(const Toolchain& tool, ToolchainKind kind,
                      const std::string& source, Language lang,
                      const std::string& arch, Configuration config) {
    Recipe recipe;
    std::string stem = mine("ed1-build");
    std::string program = programOf(tool, kind);

    if (kind == ToolMsvc) {
        recipe.assemblyPath = stem + ".asm";
        std::string obj = stem + ".obj";

        // The language is stated rather than left to the suffix. /TC and /TP
        // are what stop cl guessing, and they let a header or an oddly named
        // file be compiled as whatever the editor decided it was.
        std::string forLanguage = (lang == LangCpp) ? " /TP /EHsc /std:c++14" : " /TC";

        // /diagnostics:column is what turns 'bad.c(3)' into 'bad.c(3,13)'; the
        // editor wants the column, and cl gives none without being asked.
        recipe.command = quote(program) + " /nologo /c /diagnostics:column /FAs" +
                         forLanguage + configFlags(kind, config, arch) +
                         " /Fa" + quote(recipe.assemblyPath) +
                         " /Fo" + quote(obj) + " " + quote(source);
        recipe.leftovers.push_back(obj);
        return recipe;
    }

    // shc spells the target --target= where cc1 spells it -arch, and writes
    // MASM under the extension ml64 expects rather than under .s. Nothing else
    // about the two invocations differs.
    if (kind == ToolShc) {
        recipe.assemblyPath = stem + (arch == "x86_64-windows" ? ".asm" : ".s");
        recipe.command = quote(program) + " -S " + quote(source) + " -o " +
                         quote(recipe.assemblyPath) + " --target=" + arch;
        return recipe;
    }

    // The host's C++ compiler takes no target from here - it generates for the
    // machine it is on - so -arch is not passed to it. Everything else about
    // the invocation is cc1's, which is the point: cc1 was written to be
    // driven the way this one is.
    recipe.assemblyPath = stem + ".s";
    recipe.command = quote(program) + " -S" + languageFlag(kind, lang) + " " +
                     quote(source) + " -o " + quote(recipe.assemblyPath) +
                     (usesArch(kind) ? " -arch " + arch : std::string()) +
                     configFlags(kind, config, arch);
    return recipe;
}

std::string shownCommand(const Toolchain& tool, ToolchainKind kind,
                         const std::string& source, Language lang,
                         const std::string& arch, Configuration config) {
    std::string program = programOf(tool, kind);
    if (kind == ToolMsvc)
        return program + " /c /diagnostics:column /FAs" +
               ((lang == LangCpp) ? " /TP /EHsc /std:c++14" : " /TC") +
               configFlags(kind, config, arch) + " " + source;
    if (kind == ToolShc)
        return program + " -S " + source + " --target=" + arch;
    return program + " -S " + source + " -arch " + arch +
           configFlags(kind, config, arch);
}

bool prepareFor(ToolchainKind kind) {
#ifdef _WIN32
    if (kind == ToolMsvc) return importMsvcEnvironment();

    // cc1 needs it too, for ml64 and link. Its answer is not passed on: cl
    // without Visual Studio cannot run at all, while cc1 without it still
    // compiles - and what it then cannot do, it says for itself.
    importMsvcEnvironment();
    return true;
#else
    (void)kind;
    return true;
#endif
}

}  // namespace editor
