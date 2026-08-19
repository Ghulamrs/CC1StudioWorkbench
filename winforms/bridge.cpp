// The native side of the seam. All the STL in the Windows Forms build lives
// here and never crosses into the managed translation unit - see bridge.h for
// what happens when it does.
//
// Compiled without /clr, like every other file it calls into.

#include "bridge.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
// After windows.h, which it needs.
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

#include "compile.h"
#include "find.h"
#include "indent.h"
#include "project.h"
#include "syntax.h"
#include "toolchain.h"

namespace {

// The numbers in bridge.h are the editor's own, written out as plain integers
// for a header that may not name a C++ type. If either side is renumbered this
// stops the build rather than mis-colouring a screen.
static_assert(ED1_KIND_KEYWORD == editor::KindKeyword, "kind numbering has drifted");
static_assert(ED1_KIND_LABEL == editor::KindLabel, "kind numbering has drifted");
static_assert(ED1_LANG_CPP == editor::LangCpp, "language numbering has drifted");
static_assert(ED1_LANG_ASM == editor::LangAsm, "language numbering has drifted");
static_assert(ED1_TOOL_MSVC == editor::ToolMsvc, "toolchain numbering has drifted");
static_assert(ED1_CONFIG_RELEASE == editor::ConfigRelease, "config numbering has drifted");

// A copy the caller owns. Allocated and freed on this side of the seam, which
// is the whole point.
char* give(const std::string& text) {
    char* out = static_cast<char*>(std::malloc(text.size() + 1));
    if (!out) return 0;
    std::memcpy(out, text.c_str(), text.size() + 1);
    return out;
}

std::vector<std::string> split(const char* text) {
    std::vector<std::string> lines;
    std::string line;
    for (const char* p = text ? text : ""; *p; ++p) {
        if (*p == '\n') {
            lines.push_back(line);
            line.clear();
        } else if (*p != '\r') {
            line += *p;
        }
    }
    lines.push_back(line);
    return lines;
}

std::string join(const std::vector<std::string>& lines) {
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) out += "\n";
        out += lines[i];
    }
    return out;
}

editor::IndentStyle styleOf(int width, int tabs, int caseIndent) {
    editor::IndentStyle style;
    if (width >= 1 && width <= 16) style.width = static_cast<size_t>(width);
    style.tabs = tabs != 0;
    style.caseIndent = caseIndent ? 1 : 0;
    return style;
}

// Somewhere for the short-lived answers to live. Good until the next call,
// which is what bridge.h promises.
//
// Never destroyed, for the same reason as json.cpp's: a static with a
// destructor registers an atexit handler, and that corrupts the heap here.
std::string& scratch() {
    static std::string* kept = new std::string();
    return *kept;
}

#ifdef _WIN32

char faultLog[MAX_PATH] = "ed1-fault.log";

void write(FILE* f, const char* text) { std::fputs(text, f); }

// What a debugger would print, printed by the program itself: the exception,
// where it happened, and the frames that led there with names and line numbers.
LONG CALLBACK onFault(EXCEPTION_POINTERS* info) {
    static bool inside = false;
    if (inside) return EXCEPTION_CONTINUE_SEARCH;   // a fault while reporting one
    inside = true;

    DWORD code = info->ExceptionRecord->ExceptionCode;
    // Only the ones that are really crashes; C++ exceptions pass through here
    // on their way to a handler and are none of this function's business.
    if (code != EXCEPTION_ACCESS_VIOLATION && code != STATUS_HEAP_CORRUPTION &&
        code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_ILLEGAL_INSTRUCTION) {
        inside = false;
        return EXCEPTION_CONTINUE_SEARCH;
    }

    FILE* f = std::fopen(faultLog, "a");
    if (!f) {
        inside = false;
        return EXCEPTION_CONTINUE_SEARCH;
    }

    std::fprintf(f, "\nexception 0x%08lX at %p\n", static_cast<unsigned long>(code),
                 info->ExceptionRecord->ExceptionAddress);
    if (code == EXCEPTION_ACCESS_VIOLATION &&
        info->ExceptionRecord->NumberParameters >= 2) {
        std::fprintf(f, "  %s address %p\n",
                     info->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
                     reinterpret_cast<void*>(info->ExceptionRecord->ExceptionInformation[1]));
    }

    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, NULL, TRUE);

    void* frames[62];
    USHORT got = RtlCaptureStackBackTrace(0, 62, frames, NULL);

    char room[sizeof(SYMBOL_INFO) + 512];
    SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(room);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 500;

    for (USHORT i = 0; i < got; ++i) {
        DWORD64 address = reinterpret_cast<DWORD64>(frames[i]);
        DWORD64 offset = 0;

        if (SymFromAddr(process, address, &offset, symbol)) {
            IMAGEHLP_LINE64 line;
            line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            DWORD column = 0;
            if (SymGetLineFromAddr64(process, address, &column, &line))
                std::fprintf(f, "  %2u  %s  (%s:%lu)\n", i, symbol->Name, line.FileName,
                             static_cast<unsigned long>(line.LineNumber));
            else
                std::fprintf(f, "  %2u  %s + 0x%llX\n", i, symbol->Name,
                             static_cast<unsigned long long>(offset));
        } else {
            std::fprintf(f, "  %2u  %p\n", i, frames[i]);
        }
    }

    std::fclose(f);
    inside = false;
    return EXCEPTION_CONTINUE_SEARCH;
}

#endif

}  // namespace

struct Ed1Project {
    editor::Project project;
    std::string answer;
};

struct Ed1Build {
    editor::Build built;
    std::string assembly;
};

extern "C" {

void ed1_watch_for_faults(const char* logPath) {
#ifdef _WIN32
    if (logPath && *logPath) {
        std::strncpy(faultLog, logPath, sizeof faultLog - 1);
        faultLog[sizeof faultLog - 1] = '\0';
    }
    AddVectoredExceptionHandler(1, onFault);
#else
    (void)logPath;
#endif
}

char* ed1_reindent(const char* text, int width, int tabs, int caseIndent) {
    return give(join(editor::reindent(split(text), styleOf(width, tabs, caseIndent))));
}

char* ed1_indent_after_newline(const char* text, int row, int col,
                               int width, int tabs, int caseIndent) {
    if (row < 0) row = 0;
    if (col < 0) col = 0;
    return give(editor::indentAfterNewline(split(text), static_cast<size_t>(row),
                                           static_cast<size_t>(col),
                                           styleOf(width, tabs, caseIndent)));
}

char* ed1_indent_for(const char* text, int row, int width, int tabs, int caseIndent) {
    if (row < 0) row = 0;
    std::vector<std::string> lines = split(text);
    return give(editor::indentFor(lines, static_cast<size_t>(row),
                                  styleOf(width, tabs, caseIndent)));
}

void ed1_free(char* what) { std::free(what); }

int ed1_find_next(const char* text, const char* needle, int row, int col,
                  int* foundRow, int* foundCol) {
    if (row < 0) row = 0;
    if (col < 0) col = 0;
    editor::Match match = editor::findNext(split(text), needle ? needle : "",
                                           static_cast<size_t>(row),
                                           static_cast<size_t>(col));
    if (!match.found) return 0;
    if (foundRow) *foundRow = static_cast<int>(match.row);
    if (foundCol) *foundCol = static_cast<int>(match.col);
    return 1;
}

int ed1_find_previous(const char* text, const char* needle, int row, int col,
                      int* foundRow, int* foundCol) {
    if (row < 0) row = 0;
    if (col < 0) col = 0;
    editor::Match match = editor::findPrevious(split(text), needle ? needle : "",
                                               static_cast<size_t>(row),
                                               static_cast<size_t>(col));
    if (!match.found) return 0;
    if (foundRow) *foundRow = static_cast<int>(match.row);
    if (foundCol) *foundCol = static_cast<int>(match.col);
    return 1;
}

char* ed1_replace_all(const char* text, const char* needle, const char* with,
                      int* howMany) {
    std::vector<std::string> lines = split(text);
    size_t count = editor::replaceAll(lines, needle ? needle : "", with ? with : "");
    if (howMany) *howMany = static_cast<int>(count);
    return give(join(lines));
}

int ed1_language_for(const char* path) {
    return static_cast<int>(editor::languageFor(path ? path : ""));
}

int ed1_highlight(const char* line, int language, int* state,
                  unsigned char* kinds, int kindsSize) {
    editor::SyntaxState carried;
    if (state) {
        carried.comment = (*state & 1) != 0;
        carried.string = (*state & 2) != 0;
    }

    std::vector<unsigned char> worked =
        editor::highlight(line ? line : "", static_cast<editor::Language>(language), carried);

    if (state) *state = (carried.comment ? 1 : 0) | (carried.string ? 2 : 0);

    int n = static_cast<int>(worked.size());
    if (n > kindsSize) n = kindsSize;
    for (int i = 0; i < n; ++i) kinds[i] = worked[static_cast<size_t>(i)];
    return n;
}

Ed1Project* ed1_project_new(void) { return new Ed1Project(); }
void ed1_project_free(Ed1Project* project) { delete project; }

int ed1_project_load(Ed1Project* project, const char* directory,
                     char* error, int errorSize) {
    std::string why;
    bool loaded = project->project.load(directory ? directory : ".", why);
    if (error && errorSize > 0) {
        std::strncpy(error, why.c_str(), static_cast<size_t>(errorSize) - 1);
        error[errorSize - 1] = '\0';
    }
    return loaded ? 1 : 0;
}

const char* ed1_project_name(Ed1Project* project) {
    project->answer = project->project.name();
    return project->answer.c_str();
}

int ed1_project_groups(Ed1Project* project) {
    return static_cast<int>(project->project.groups().size());
}

const char* ed1_project_group_name(Ed1Project* project, int group) {
    if (group < 0 || group >= ed1_project_groups(project)) return "";
    project->answer = project->project.groups()[static_cast<size_t>(group)].name;
    return project->answer.c_str();
}

int ed1_project_files(Ed1Project* project, int group) {
    if (group < 0 || group >= ed1_project_groups(project)) return 0;
    return static_cast<int>(
        project->project.groups()[static_cast<size_t>(group)].files.size());
}

const char* ed1_project_file(Ed1Project* project, int group, int file) {
    if (file < 0 || file >= ed1_project_files(project, group)) return "";
    project->answer =
        project->project.groups()[static_cast<size_t>(group)].files[static_cast<size_t>(file)];
    return project->answer.c_str();
}

const char* ed1_project_absolute(Ed1Project* project, const char* relative) {
    project->answer = project->project.absolute(relative ? relative : "");
    return project->answer.c_str();
}

int ed1_project_indent_width(Ed1Project* project) {
    return static_cast<int>(project->project.indent().width);
}
int ed1_project_indent_tabs(Ed1Project* project) {
    return project->project.indent().tabs ? 1 : 0;
}
int ed1_project_case_indent(Ed1Project* project) {
    return static_cast<int>(project->project.indent().caseIndent);
}
int ed1_project_toolchain(Ed1Project* project) {
    return static_cast<int>(project->project.toolchain());
}
int ed1_project_config(Ed1Project* project) {
    return static_cast<int>(project->project.config());
}
const char* ed1_project_arch(Ed1Project* project) {
    project->answer = project->project.arch();
    return project->answer.c_str();
}

const char* ed1_arch(int index) {
    if (index < 0 || index > 2) index = 0;
    return editor::kArches[index];
}

const char* ed1_toolchain_name(int kind) {
    return editor::toolchainName(static_cast<editor::ToolchainKind>(kind));
}

int ed1_resolve(int toolchainKind, int language) {
    editor::Toolchain tool;
    tool.kind = static_cast<editor::ToolchainKind>(toolchainKind);
    return static_cast<int>(editor::resolve(tool, static_cast<editor::Language>(language)));
}

int ed1_can_compile(int kind, int language) {
    return editor::canCompile(static_cast<editor::ToolchainKind>(kind),
                              static_cast<editor::Language>(language))
               ? 1 : 0;
}

const char* ed1_refusal(int kind, int language) {
    scratch() = editor::refusal(static_cast<editor::ToolchainKind>(kind),
                                static_cast<editor::Language>(language));
    return scratch().c_str();
}

int ed1_uses_arch(int kind) {
    return editor::usesArch(static_cast<editor::ToolchainKind>(kind)) ? 1 : 0;
}

const char* ed1_shown_command(const char* cc1, const char* cl, int kind,
                              const char* source, int language, const char* arch,
                              int config) {
    editor::Toolchain tool;
    if (cc1 && *cc1) tool.cc1 = cc1;
    if (cl && *cl) tool.cl = cl;

    scratch() = editor::shownCommand(tool, static_cast<editor::ToolchainKind>(kind),
                                     source ? source : "",
                                     static_cast<editor::Language>(language),
                                     arch ? arch : "",
                                     static_cast<editor::Configuration>(config));
    return scratch().c_str();
}

Ed1Build* ed1_build(const char* cc1, const char* cl, int kind, const char* source,
                    int language, const char* arch, int config) {
    editor::Toolchain tool;
    if (cc1 && *cc1) tool.cc1 = cc1;
    if (cl && *cl) tool.cl = cl;

    Ed1Build* out = new Ed1Build();
    out->built = editor::build(tool, static_cast<editor::ToolchainKind>(kind),
                               source ? source : "",
                               static_cast<editor::Language>(language),
                               arch ? arch : "",
                               static_cast<editor::Configuration>(config));
    out->assembly = join(out->built.asmLines);
    return out;
}

void ed1_build_free(Ed1Build* built) { delete built; }

int ed1_build_ok(Ed1Build* built) { return built->built.ok ? 1 : 0; }
const char* ed1_build_output(Ed1Build* built) { return built->built.output.c_str(); }
const char* ed1_build_assembly(Ed1Build* built) { return built->assembly.c_str(); }
int ed1_build_assembly_lines(Ed1Build* built) {
    return static_cast<int>(built->built.asmLines.size());
}
int ed1_build_has_error(Ed1Build* built) { return built->built.diag.present ? 1 : 0; }
int ed1_build_error_line(Ed1Build* built) {
    return static_cast<int>(built->built.diag.line);
}
int ed1_build_error_column(Ed1Build* built) {
    return static_cast<int>(built->built.diag.col);
}
const char* ed1_build_error_message(Ed1Build* built) {
    return built->built.diag.message.c_str();
}

}  // extern "C"
