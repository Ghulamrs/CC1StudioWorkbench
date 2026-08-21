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
// Rich Edit's own interfaces, for suspending its undo recording. richole.h
// wants richedit.h before it, and tom.h wants both.
#include <richedit.h>
#include <richole.h>
#include <tom.h>
// SendMessage, for the one call below. The window's own project links this
// already; the test build does not, and linked nothing else that needed it.
#pragma comment(lib, "user32.lib")
#endif

#include "about.h"
#include "compile.h"
#include "debugger.h"
#include "find.h"
#include "indent.h"
#include "project.h"
#include "symbols.h"
#include "syntax.h"
#include "toolchain.h"
#include "settings.h"
#include "workspace.h"

namespace {

// The numbers in bridge.h are the editor's own, written out as plain integers
// for a header that may not name a C++ type. If either side is renumbered this
// stops the build rather than mis-colouring a screen.
//
// The cast is not decoration: one side is an unnamed enum from a C header and
// the other a named C++ one, and gcc refuses to compare the two. It only
// started mattering when the tests began linking this file, which is the first
// time anything but MSVC and clang had read it.
static_assert(ED1_KIND_KEYWORD == static_cast<int>(editor::KindKeyword), "kind numbering has drifted");
static_assert(ED1_KIND_LABEL == static_cast<int>(editor::KindLabel), "kind numbering has drifted");
static_assert(ED1_LANG_CPP == static_cast<int>(editor::LangCpp), "language numbering has drifted");
static_assert(ED1_LANG_ASM == static_cast<int>(editor::LangAsm), "language numbering has drifted");
static_assert(ED1_TOOL_MSVC == static_cast<int>(editor::ToolMsvc), "toolchain numbering has drifted");
static_assert(ED1_CONFIG_RELEASE == static_cast<int>(editor::ConfigRelease), "config numbering has drifted");

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
    editor::Outcome last;   // what the most recent change had to say

    // What the last look at the project's target found: its sources, or the
    // reason there are none to hand back. Kept here because the managed side
    // reads them one string at a time and a returned pointer has to outlive
    // the call that gave it.
    std::vector<std::string> sources;
    std::string program;
    std::string why;
    std::string detail;
    int language;
};

struct Ed1Build {
    editor::Build built;
    std::string assembly;
};

struct Ed1Ran {
    editor::Ran ran;
};

struct Ed1Program {
    editor::Built built;
};

// The debugger, what it last said, and what was in scope when it said it. All
// three are kept together because the managed side reads them one string at a
// time and must not have to hold any of them itself.
struct Ed1Debugger {
    editor::Debugger debugger;
    editor::Stop stop;
    std::vector<editor::Variable> locals;
    std::string answer;
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

#ifdef _WIN32
// tomSuspend stacks: two suspends want two resumes. The colouring passes are
// not nested, but the count is what the interface promises, not the caller.
static void undoRecording(void* windowHandle, long how) {
    HWND window = reinterpret_cast<HWND>(windowHandle);
    if (!window) return;

    IRichEditOle* ole = 0;
    SendMessage(window, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(&ole));
    if (!ole) return;

    ITextDocument* document = 0;
    if (SUCCEEDED(ole->QueryInterface(__uuidof(ITextDocument),
                                      reinterpret_cast<void**>(&document))) &&
        document) {
        document->Undo(how, 0);
        document->Release();
    }
    ole->Release();
}
#endif

void ed1_undo_suspend(void* windowHandle) {
#ifdef _WIN32
    undoRecording(windowHandle, tomSuspend);
#else
    (void)windowHandle;
#endif
}

void ed1_undo_resume(void* windowHandle) {
#ifdef _WIN32
    undoRecording(windowHandle, tomResume);
#else
    (void)windowHandle;
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

char* ed1_about(void) { return give(join(editor::about::lines())); }

char* ed1_describe_build(const char* assembly) {
    return give(join(editor::describe(editor::symbolsIn(split(assembly)))));
}

char* ed1_debug_note(int kind, const char* arch) {
    return give(join(editor::debugNote(static_cast<editor::ToolchainKind>(kind),
                                       arch ? arch : "")));
}

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

int ed1_project_allows(const char* relative, char* why, int whySize) {
    std::string reason;
    bool fine = editor::Project::allows(relative ? relative : "", reason);
    if (why && whySize > 0) {
        std::strncpy(why, reason.c_str(), static_cast<size_t>(whySize) - 1);
        why[whySize - 1] = '\0';
    }
    return fine ? 1 : 0;
}

int ed1_project_loaded(Ed1Project* project) { return project->project.loaded() ? 1 : 0; }

const char* ed1_project_root(Ed1Project* project) {
    project->answer = project->project.root();
    return project->answer.c_str();
}

void ed1_project_set_root(Ed1Project* project, const char* path) {
    project->project.setRoot(path ? path : ".");
}

const char* ed1_project_relative(Ed1Project* project, const char* path) {
    project->answer = project->project.relative(path ? path : "");
    return project->answer.c_str();
}

const char* ed1_project_file_name(void) { return editor::Project::fileName(); }

const char* ed1_outcome_message(Ed1Project* project) {
    return project->last.message.c_str();
}

const char* ed1_outcome_path(Ed1Project* project) { return project->last.path.c_str(); }

int ed1_create_file(Ed1Project* project, const char* relative, const char* group) {
    project->last = editor::createFile(project->project, relative ? relative : "",
                                       group ? group : "");
    return project->last.ok ? 1 : 0;
}

int ed1_rename_file(Ed1Project* project, const char* fromAbsolute, const char* toRelative) {
    project->last = editor::renameFile(project->project, fromAbsolute ? fromAbsolute : "",
                                       toRelative ? toRelative : "");
    return project->last.ok ? 1 : 0;
}

int ed1_delete_file(Ed1Project* project, const char* absolute) {
    project->last = editor::deleteFile(project->project, absolute ? absolute : "");
    return project->last.ok ? 1 : 0;
}

int ed1_move_to_group(Ed1Project* project, const char* absolute, const char* group) {
    project->last = editor::moveToGroup(project->project, absolute ? absolute : "",
                                        group ? group : "");
    return project->last.ok ? 1 : 0;
}

int ed1_add_existing(Ed1Project* project, const char* absolute, const char* group) {
    project->last = editor::addExisting(project->project, absolute ? absolute : "",
                                        group ? group : "");
    return project->last.ok ? 1 : 0;
}

int ed1_begin_project(Ed1Project* project, const char* directory, const char* name,
                      const char* firstFile) {
    project->last = editor::beginProject(project->project, directory ? directory : ".",
                                         name ? name : "Project",
                                         firstFile ? firstFile : "");
    return project->last.ok ? 1 : 0;
}

int ed1_save_project(Ed1Project* project) {
    project->last = editor::saveProject(project->project);
    return project->last.ok ? 1 : 0;
}

const char* ed1_arch(int index) {
    if (index < 0 || index > 2) index = 0;
    return editor::kArches[index];
}

const char* ed1_toolchain_name(int kind) {
    return editor::toolchainName(static_cast<editor::ToolchainKind>(kind));
}

const char* ed1_language_name(int language) {
    return editor::languageName(static_cast<editor::Language>(language));
}

const char* ed1_config_name(int config) {
    return editor::configName(static_cast<editor::Configuration>(config));
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

int ed1_runs_here(int kind, const char* arch) {
    return editor::runsHere(static_cast<editor::ToolchainKind>(kind), arch ? arch : "") ? 1 : 0;
}

const char* ed1_why_not_run(int kind, const char* arch) {
    scratch() = editor::whyNotRun(static_cast<editor::ToolchainKind>(kind), arch ? arch : "");
    return scratch().c_str();
}

const char* ed1_host_arch(void) { return editor::hostArch(); }

const char* ed1_shown_run_command(const char* cc1, const char* cl, int kind,
                                  const char* source, int language, const char* arch,
                                  int config) {
    editor::Toolchain tool;
    if (cc1 && *cc1) tool.cc1 = cc1;
    if (cl && *cl) tool.cl = cl;

    scratch() = editor::shownProgramCommand(tool, static_cast<editor::ToolchainKind>(kind),
                                            source ? source : "",
                                            static_cast<editor::Language>(language),
                                            arch ? arch : "",
                                            static_cast<editor::Configuration>(config));
    return scratch().c_str();
}

Ed1Ran* ed1_run(const char* cc1, const char* cl, int kind, const char* source,
                int language, const char* arch, int config) {
    editor::Toolchain tool;
    if (cc1 && *cc1) tool.cc1 = cc1;
    if (cl && *cl) tool.cl = cl;

    Ed1Ran* out = new Ed1Ran();
    out->ran = editor::runProgram(tool, static_cast<editor::ToolchainKind>(kind),
                                  source ? source : "",
                                  static_cast<editor::Language>(language),
                                  arch ? arch : "",
                                  static_cast<editor::Configuration>(config));
    return out;
}

void ed1_run_free(Ed1Ran* ran) { delete ran; }

int ed1_ran_built(Ed1Ran* ran) { return ran->ran.built ? 1 : 0; }
int ed1_ran_ran(Ed1Ran* ran) { return ran->ran.ran ? 1 : 0; }
int ed1_ran_status(Ed1Ran* ran) { return ran->ran.status; }
const char* ed1_ran_output(Ed1Ran* ran) { return ran->ran.output.c_str(); }
int ed1_ran_has_error(Ed1Ran* ran) { return ran->ran.diag.present ? 1 : 0; }
int ed1_ran_error_line(Ed1Ran* ran) { return static_cast<int>(ran->ran.diag.line); }
int ed1_ran_error_column(Ed1Ran* ran) { return static_cast<int>(ran->ran.diag.col); }
const char* ed1_ran_error_message(Ed1Ran* ran) { return ran->ran.diag.message.c_str(); }

Ed1Program* ed1_build_program(const char* cc1, const char* cl, int kind, const char* source,
                              int language, const char* arch, int config) {
    editor::Toolchain tool;
    if (cc1 && *cc1) tool.cc1 = cc1;
    if (cl && *cl) tool.cl = cl;

    Ed1Program* out = new Ed1Program();
    out->built = editor::buildProgram(tool, static_cast<editor::ToolchainKind>(kind),
                                      source ? source : "",
                                      static_cast<editor::Language>(language),
                                      arch ? arch : "",
                                      static_cast<editor::Configuration>(config));
    return out;
}

void ed1_program_free(Ed1Program* built) {
    if (!built) return;
    editor::removeProgram(built->built);   // the program goes with the handle
    delete built;
}

int ed1_program_ok(Ed1Program* built) { return built->built.ok ? 1 : 0; }
const char* ed1_program_path(Ed1Program* built) { return built->built.program.c_str(); }
const char* ed1_program_output(Ed1Program* built) { return built->built.output.c_str(); }
int ed1_program_has_error(Ed1Program* built) { return built->built.diag.present ? 1 : 0; }
int ed1_program_error_line(Ed1Program* built) {
    return static_cast<int>(built->built.diag.line);
}
int ed1_program_error_column(Ed1Program* built) {
    return static_cast<int>(built->built.diag.col);
}
const char* ed1_program_error_message(Ed1Program* built) {
    return built->built.diag.message.c_str();
}

int ed1_debugger_for(int kind, const char* arch) {
    return static_cast<int>(editor::dbg_for(static_cast<editor::ToolchainKind>(kind),
                                                arch ? arch : ""));
}

const char* ed1_debugger_name(int kind) {
    return editor::dbg_name(static_cast<editor::DebuggerKind>(kind));
}

const char* ed1_no_debugger_because(int kind, const char* arch) {
    scratch() = editor::dbg_whyNot(static_cast<editor::ToolchainKind>(kind),
                                          arch ? arch : "");
    return scratch().c_str();
}

Ed1Debugger* ed1_debugger_new(void) { return new Ed1Debugger(); }
void ed1_debugger_free(Ed1Debugger* debugger) { delete debugger; }

int ed1_debugger_start(Ed1Debugger* debugger, int debuggerKind, const char* program) {
    debugger->stop = editor::Stop();
    debugger->locals.clear();
    return debugger->debugger.start(static_cast<editor::DebuggerKind>(debuggerKind),
                                    program ? program : "") ? 1 : 0;
}

int ed1_debugger_running(Ed1Debugger* debugger) {
    return debugger->debugger.running() ? 1 : 0;
}

void ed1_debugger_stop(Ed1Debugger* debugger) {
    debugger->debugger.stop();
    debugger->stop = editor::Stop();
    debugger->locals.clear();
}

int ed1_debugger_break(Ed1Debugger* debugger, const char* file, int line) {
    if (line < 1) return 0;
    return debugger->debugger.breakAt(file ? file : "", static_cast<size_t>(line)) ? 1 : 0;
}

int ed1_debugger_clear(Ed1Debugger* debugger) {
    return debugger->debugger.clearBreakpoints() ? 1 : 0;
}

namespace {
// Every move ends the same way: keep where it stopped, and ask what is in
// scope there while it is still standing still.
void afterMoving(Ed1Debugger* debugger, const editor::Stop& stop) {
    debugger->stop = stop;
    debugger->locals.clear();
    if (stop.stopped) debugger->locals = debugger->debugger.locals();
}
}  // namespace

void ed1_debugger_run(Ed1Debugger* debugger) { afterMoving(debugger, debugger->debugger.run()); }
void ed1_debugger_resume(Ed1Debugger* debugger) { afterMoving(debugger, debugger->debugger.resume()); }
void ed1_debugger_step_over(Ed1Debugger* debugger) { afterMoving(debugger, debugger->debugger.stepOver()); }
void ed1_debugger_step_into(Ed1Debugger* debugger) { afterMoving(debugger, debugger->debugger.stepInto()); }
void ed1_debugger_step_out(Ed1Debugger* debugger) { afterMoving(debugger, debugger->debugger.stepOut()); }

int ed1_stop_stopped(Ed1Debugger* debugger) { return debugger->stop.stopped ? 1 : 0; }
int ed1_stop_exited(Ed1Debugger* debugger) { return debugger->stop.exited ? 1 : 0; }
int ed1_stop_status(Ed1Debugger* debugger) { return debugger->stop.status; }
const char* ed1_stop_file(Ed1Debugger* debugger) { return debugger->stop.file.c_str(); }
int ed1_stop_line(Ed1Debugger* debugger) { return static_cast<int>(debugger->stop.line); }
const char* ed1_stop_function(Ed1Debugger* debugger) { return debugger->stop.function.c_str(); }

int ed1_locals_count(Ed1Debugger* debugger) {
    return static_cast<int>(debugger->locals.size());
}

namespace {
bool holds(Ed1Debugger* debugger, int index) {
    return index >= 0 && static_cast<size_t>(index) < debugger->locals.size();
}
}  // namespace

const char* ed1_local_name(Ed1Debugger* debugger, int index) {
    return holds(debugger, index) ? debugger->locals[index].name.c_str() : "";
}
const char* ed1_local_type(Ed1Debugger* debugger, int index) {
    return holds(debugger, index) ? debugger->locals[index].type.c_str() : "";
}
const char* ed1_local_value(Ed1Debugger* debugger, int index) {
    return holds(debugger, index) ? debugger->locals[index].value.c_str() : "";
}

int ed1_begin_from_what_is_there(Ed1Project* project, const char* directory) {
    if (!project) return 0;
    project->last = editor::beginFromWhatIsThere(project->project, directory ? directory : "");
    project->answer = project->last.message;
    return project->last.ok ? 1 : 0;
}

const char* ed1_last_project(void) {
    // scratch(), not a static string of its own: a function-local static with
    // a destructor registers an atexit handler, and in a mixed binary that
    // corrupts the heap - which it did, on the first call, with the fault log
    // naming atexit under ed1_last_project. The note is on scratch() itself.
    scratch() = editor::settings::lastProject();
    return scratch().c_str();
}

int ed1_remember_project(const char* directory) {
    return editor::settings::rememberProject(directory ? directory : "") ? 1 : 0;
}

const char* ed1_demo_directory(void) {
    scratch() = editor::demoDirectory();
    return scratch().c_str();
}

int ed1_project_builds(Ed1Project* project) {
    return project && project->project.builds() ? 1 : 0;
}

int ed1_project_target_ready(Ed1Project* project) {
    if (!project) return 0;

    editor::Language lang = editor::LangPlain;
    bool ok = project->project.targetSources(project->sources, lang, project->why,
                                             &project->detail);
    project->language = static_cast<int>(lang);
    project->program = ok ? project->project.targetProgram() : std::string();
    return ok ? 1 : 0;
}

const char* ed1_project_target_why(Ed1Project* project) {
    return project ? project->why.c_str() : "";
}

const char* ed1_project_target_detail(Ed1Project* project) {
    return project ? project->detail.c_str() : "";
}

int ed1_project_target_language(Ed1Project* project) {
    return project ? project->language : 0;
}

int ed1_project_target_sources(Ed1Project* project) {
    return project ? static_cast<int>(project->sources.size()) : 0;
}

const char* ed1_project_target_source(Ed1Project* project, int index) {
    if (!project || index < 0 || index >= static_cast<int>(project->sources.size())) return "";
    return project->sources[static_cast<size_t>(index)].c_str();
}

const char* ed1_project_target_program(Ed1Project* project) {
    return project ? project->program.c_str() : "";
}

Ed1Build* ed1_build_target(Ed1Project* project, const char* cc1, const char* cl,
                           int kind, const char* arch, int config) {
    if (!ed1_project_target_ready(project)) return 0;

    editor::Toolchain tool;
    if (cc1 && *cc1) tool.cc1 = cc1;
    if (cl && *cl) tool.cl = cl;

    Ed1Build* out = new Ed1Build();
    editor::Built made = editor::buildTarget(
        tool, static_cast<editor::ToolchainKind>(kind), project->sources,
        static_cast<editor::Language>(project->language), arch ? arch : "",
        static_cast<editor::Configuration>(config), project->program);

    // The window reads a Build, and what a target build produces is a program
    // rather than assembly - so what came of it is carried over and the
    // assembly is left empty, which is what there is.
    out->built.ok = made.ok;
    out->built.diag = made.diag;
    out->built.output = made.output;
    return out;
}

Ed1Ran* ed1_run_built(const char* program) {
    Ed1Ran* out = new Ed1Ran();
    out->ran = editor::runBuilt(program ? program : "");
    return out;
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
const char* ed1_build_error_file(Ed1Build* built) {
    return built ? built->built.diag.file.c_str() : "";
}

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
