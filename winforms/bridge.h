#ifndef ED1_BRIDGE_H
#define ED1_BRIDGE_H

// The seam between the managed form and the native editor, and the reason it
// exists is worth writing down.
//
// A /clr translation unit that instantiates std::string or std::vector, in a
// binary whose native translation units instantiate the same templates,
// corrupts the heap before main is ever reached - exit code 0xC0000374. The
// linker folds the two instantiations together and one side's allocation ends
// up paired with the other side's free. It was reproduced from a bare Windows
// Forms application: it ran, it ran with all nine native files linked in, and
// it died the moment one std::vector<std::string> appeared in the managed file.
//
// So nothing below names a C++ type. No STL, no editor headers, no classes -
// plain C declarations, opaque handles and char pointers. bridge.cpp holds all
// of it on the native side, and the form never sees a template.
//
// Every const char* returned is owned by the thing that returned it and stays
// good until the next call on that thing, or until it is freed. Anything
// returned as char* is the caller's, and ed1_free takes it back.

#ifdef __cplusplus
extern "C" {
#endif

/* Kinds, languages, toolchains and configurations, repeated here as plain
   integers so that the form can speak about them without including the
   headers that define them. bridge.cpp checks they still agree. */
enum {
    ED1_KIND_NORMAL = 0, ED1_KIND_KEYWORD, ED1_KIND_TYPE, ED1_KIND_STRING,
    ED1_KIND_CHAR, ED1_KIND_COMMENT, ED1_KIND_PREPROC, ED1_KIND_NUMBER,
    ED1_KIND_LABEL
};
enum { ED1_LANG_PLAIN = 0, ED1_LANG_C, ED1_LANG_CPP, ED1_LANG_SHALIMAR, ED1_LANG_ASM };

// Which language's punctuation the layout rules follow. Not the same question
// as which language the file is - assembly and plain text are laid out by
// neither - so it is a value of its own rather than a Language.
enum { ED1_DIALECT_C = 0, ED1_DIALECT_SHALIMAR };
enum { ED1_TOOL_AUTO = 0, ED1_TOOL_CC1, ED1_TOOL_MSVC, ED1_TOOL_SHC };
enum { ED1_CONFIG_DEBUG = 0, ED1_CONFIG_RELEASE };

/* Catches a crash and writes the faulting address and a symbolised stack to
   ed1-fault.log. There is no WinDbg on the machine this is built for, and
   dbghelp is, so the program carries its own. Does nothing off Windows. */
void ed1_watch_for_faults(const char* logPath);

/* ---- laying code out ---------------------------------------------------- */

/* The whole buffer in, the whole buffer out, lines separated by \n. */
char* ed1_reindent(const char* text, int width, int tabs, int caseIndent,
                   int dialect);

/* What a newline typed at row and col should be followed by. */
char* ed1_indent_after_newline(const char* text, int row, int col,
                               int width, int tabs, int caseIndent, int dialect);

/* The leading space one line should have, for the tab key and for a line whose
   own layout changed the moment a brace was typed on it. */
char* ed1_indent_for(const char* text, int row, int width, int tabs, int caseIndent,
                     int dialect);

void ed1_free(char* what);

/* ---- finding and replacing ---------------------------------------------- */

/* 1 when found, and where it was written into row and col. Both wrap once and
   stop where they started. */
int ed1_find_next(const char* text, const char* needle, int row, int col,
                  int* foundRow, int* foundCol);
int ed1_find_previous(const char* text, const char* needle, int row, int col,
                      int* foundRow, int* foundCol);

/* The whole text with every occurrence replaced, and how many there were. */
char* ed1_replace_all(const char* text, const char* needle, const char* with,
                      int* howMany);

/* ---- colouring ---------------------------------------------------------- */

/* Rich Edit records a change of colour in its undo buffer exactly as it records
   typing, so colouring a file fills that buffer before anybody has touched the
   text: Ctrl-Z then undoes a colour, moves the caret to it, and marks a file
   modified that was only ever looked at. These suspend and resume the recording
   around a colouring pass, given the editing window's handle.

   Undoing your own typing is unaffected - only what happens between the two
   calls goes unrecorded. Anywhere but Windows, and wherever the interface
   cannot be had, they do nothing and colouring behaves as before. */
/* The font the window last drew code in, as the window spelled it, and a way
   to keep the next choice. Empty when nothing was ever chosen. */
/* Where an unreadable configuration was put, or empty. Worth saying once at
   startup: the settings went back to their defaults and the old file is still
   there to look at. */
const char* ed1_settings_set_aside(void);

const char* ed1_code_font(void);
int ed1_remember_code_font(const char* described);

void ed1_undo_suspend(void* windowHandle);
void ed1_undo_resume(void* windowHandle);

int ed1_language_for(const char* path);

/* Which layout rules a language wants. */
int ed1_dialect_for(int language);

/* One kind per byte of the line, written into kinds. `state` carries the block
   comment across lines and is read and written. Returns how many were set. */
int ed1_highlight(const char* line, int language, int* state,
                  unsigned char* kinds, int kindsSize);

/* ---- what a build produced ---------------------------------------------- */

/* The functions, exports, imports and strings found in an assembly listing,
   already laid out a line at a time. Not debug information, whatever the target
   writes: this build stops at the assembly, so there is nothing running to
   debug. This is what there is to know instead. */
char* ed1_describe_build(const char* assembly);

/* What the panel says above that listing - what debug information this target
   has, and what the listing is instead. The terminal front end shows the same
   words, because both ask here rather than writing them out. */
char* ed1_debug_note(int kind, const char* arch);

/* ---- the project -------------------------------------------------------- */

typedef struct Ed1Project Ed1Project;

Ed1Project* ed1_project_new(void);
void ed1_project_free(Ed1Project* project);

/* 1 when a project was read, 0 when there is none or it is broken; the reason
   goes into error when there is one. */
int ed1_project_load(Ed1Project* project, const char* directory,
                     char* error, int errorSize);

const char* ed1_project_name(Ed1Project* project);
int ed1_project_groups(Ed1Project* project);
const char* ed1_project_group_name(Ed1Project* project, int group);
int ed1_project_files(Ed1Project* project, int group);
const char* ed1_project_file(Ed1Project* project, int group, int file);
const char* ed1_project_absolute(Ed1Project* project, const char* relative);

int ed1_project_indent_width(Ed1Project* project);
int ed1_project_indent_tabs(Ed1Project* project);
int ed1_project_case_indent(Ed1Project* project);
int ed1_project_toolchain(Ed1Project* project);
int ed1_project_config(Ed1Project* project);
const char* ed1_project_arch(Ed1Project* project);

/* ---- changing the project ------------------------------------------------ */

/* The shape a path may have: the root, or one directory under it, and no
   deeper. It comes from the core so that both front ends keep one rule; the
   reason goes into `why` when the answer is no. */
int ed1_project_allows(const char* relative, char* why, int whySize);

int ed1_project_loaded(Ed1Project* project);
const char* ed1_project_root(Ed1Project* project);
void ed1_project_set_root(Ed1Project* project, const char* path);
const char* ed1_project_relative(Ed1Project* project, const char* path);
const char* ed1_project_file_name(void);

/* Each of these does the disk work, keeps the project's list in step and
   writes the project back out - all of it in the core, so the window and the
   terminal do the same thing rather than two similar things. 1 when it worked;
   the message and the path say what happened and stay good until the next one. */
int ed1_create_file(Ed1Project* project, const char* relative, const char* group);
int ed1_rename_file(Ed1Project* project, const char* fromAbsolute, const char* toRelative);
int ed1_delete_file(Ed1Project* project, const char* absolute);
int ed1_move_to_group(Ed1Project* project, const char* absolute, const char* group);
int ed1_add_existing(Ed1Project* project, const char* absolute, const char* group);
/* A project made out of what is already in a directory, for when there is no
   ed1.json to read - and the two things that decide where the editor opens
   when it is given nothing: the project it was last in, and the small one made
   in your own files the first time there is no answer to that.

   All three have been in the core since the terminal half used them; the
   window simply never asked, so it opened on an empty pane where the terminal
   opened on your work. */
int ed1_begin_from_what_is_there(Ed1Project* project, const char* directory);
const char* ed1_last_project(void);
int ed1_remember_project(const char* directory);
const char* ed1_demo_directory(void);

int ed1_begin_project(Ed1Project* project, const char* directory, const char* name,
                      const char* firstFile);
int ed1_save_project(Ed1Project* project);

const char* ed1_outcome_message(Ed1Project* project);
const char* ed1_outcome_path(Ed1Project* project);

/* ---- compilers ---------------------------------------------------------- */

const char* ed1_arch(int index);              /* 0, 1, 2 */
const char* ed1_toolchain_name(int kind);

/* What the core calls a language and a configuration. Here so that both front
   ends say the same words for the same thing - the terminal's status bar has
   had these since the beginning and the window had no way to ask for them. */
const char* ed1_language_name(int language);
const char* ed1_config_name(int config);
int ed1_resolve(int toolchainKind, int language);
int ed1_can_compile(int kind, int language);
const char* ed1_refusal(int kind, int language);
int ed1_uses_arch(int kind);

/* Whether a build for this target can be run on this machine, which is not the
   same question as whether it can be compiled - every target compiles to
   assembly anywhere, and only the host's own goes on to a program. The second
   gives the reason when the answer is no, and an empty string when it is yes. */
int ed1_runs_here(int kind, const char* arch);
const char* ed1_why_not_run(int kind, const char* arch);
const char* ed1_host_arch(void);

const char* ed1_shown_command(const char* cc1, const char* cl, const char* shc, int kind,
                              const char* source, int language, const char* arch,
                              int config);

typedef struct Ed1Build Ed1Build;

Ed1Build* ed1_build(const char* cc1, const char* cl, const char* shc, int kind, const char* source,
                    int language, const char* arch, int config);

/* The project's own program, as against the file in front of you. Which of the
   two you meant is said by which one you asked for; neither needs the other to
   be unavailable, and compiling one file never needs a project at all.

   ed1_project_target_ready works out the sources and answers 1 when there is
   something to build. When it answers 0, ed1_project_target_why is one line
   for a status bar and ed1_project_target_detail is the rest of it for a box
   with room - the refusal that matters being a project holding both C and C++,
   which cc1 and cl cannot be asked to build between them. */
int ed1_project_builds(Ed1Project* project);
int ed1_project_target_ready(Ed1Project* project);
const char* ed1_project_target_why(Ed1Project* project);
const char* ed1_project_target_detail(Ed1Project* project);
int ed1_project_target_language(Ed1Project* project);
int ed1_project_target_sources(Ed1Project* project);
const char* ed1_project_target_source(Ed1Project* project, int index);
const char* ed1_project_target_program(Ed1Project* project);

/* Builds it. Ask ed1_project_target_ready first: this answers null when there
   is nothing to build, and the reason is where that call left it. */
Ed1Build* ed1_build_target(Ed1Project* project, const char* cc1, const char* cl, const char* shc,
                           int kind, const char* arch, int config);
void ed1_build_free(Ed1Build* built);

int ed1_build_ok(Ed1Build* built);
const char* ed1_build_output(Ed1Build* built);
const char* ed1_build_assembly(Ed1Build* built);   /* joined with \n */
int ed1_build_assembly_lines(Ed1Build* built);
int ed1_build_has_error(Ed1Build* built);
/* Which file the error is in. It used to go without saying - a build was one
   file and that was the one you were looking at - and a project build is
   several, so it has to be asked. */
const char* ed1_build_error_file(Ed1Build* built);
int ed1_build_error_line(Ed1Build* built);
int ed1_build_error_column(Ed1Build* built);
const char* ed1_build_error_message(Ed1Build* built);

/* Compiling, linking and running, which is three things that can each go their
   own way: ed1_ran_built says a program came out, ed1_ran_ran says it was
   started, and ed1_ran_status is what it returned once it had. A program that
   returns 1 is not a build that failed. */
typedef struct Ed1Ran Ed1Ran;

Ed1Ran* ed1_run(const char* cc1, const char* cl, const char* shc, int kind, const char* source,
                int language, const char* arch, int config);

/* Runs a program that is already built - the project's, once it has been. */
Ed1Ran* ed1_run_built(const char* program);
void ed1_run_free(Ed1Ran* ran);

int ed1_ran_built(Ed1Ran* ran);
int ed1_ran_ran(Ed1Ran* ran);
int ed1_ran_status(Ed1Ran* ran);
const char* ed1_ran_output(Ed1Ran* ran);
int ed1_ran_has_error(Ed1Ran* ran);
int ed1_ran_error_line(Ed1Ran* ran);
int ed1_ran_error_column(Ed1Ran* ran);
const char* ed1_ran_error_message(Ed1Ran* ran);

const char* ed1_shown_run_command(const char* cc1, const char* cl, const char* shc, int kind,
                                  const char* source, int language, const char* arch,
                                  int config);

/* Who this is and whose it is, a line at a time joined with \n. The window puts
   it in a box and the terminal prints it in its panel; neither writes it. */
char* ed1_about(void);

/* ---- stopping on a line ------------------------------------------------- */

/* A program built and left where it is, which is what a debugger attaches to.
   Freeing the handle removes the program with it. */
typedef struct Ed1Program Ed1Program;

Ed1Program* ed1_build_program(const char* cc1, const char* cl, const char* shc, int kind, const char* source,
                              int language, const char* arch, int config);
void ed1_program_free(Ed1Program* built);

int ed1_program_ok(Ed1Program* built);
const char* ed1_program_path(Ed1Program* built);
const char* ed1_program_output(Ed1Program* built);
int ed1_program_has_error(Ed1Program* built);
int ed1_program_error_line(Ed1Program* built);
int ed1_program_error_column(Ed1Program* built);
const char* ed1_program_error_message(Ed1Program* built);

/* Which debugger can read what this compiler writes for this target: 0 none,
   1 lldb, 2 gdb. Not a question about the machine alone - on Windows a C file
   goes to cc1 and comes out as MASM with no line table, while a C++ file goes
   to cl and comes out with CodeView in a .pdb. */
int ed1_debugger_for(int kind, const char* arch);
const char* ed1_debugger_name(int kind);
const char* ed1_no_debugger_because(int kind, const char* arch);

typedef struct Ed1Debugger Ed1Debugger;

Ed1Debugger* ed1_debugger_new(void);
void ed1_debugger_free(Ed1Debugger* debugger);

int ed1_debugger_start(Ed1Debugger* debugger, int debuggerKind, const char* program);
int ed1_debugger_running(Ed1Debugger* debugger);
void ed1_debugger_stop(Ed1Debugger* debugger);

int ed1_debugger_break(Ed1Debugger* debugger, const char* file, int line);
int ed1_debugger_clear(Ed1Debugger* debugger);

/* Each of these moves the program and keeps what came of it, which is then
   read with the ed1_stop_ calls below. */
void ed1_debugger_run(Ed1Debugger* debugger);
void ed1_debugger_resume(Ed1Debugger* debugger);
void ed1_debugger_step_over(Ed1Debugger* debugger);
void ed1_debugger_step_into(Ed1Debugger* debugger);
void ed1_debugger_step_out(Ed1Debugger* debugger);

int ed1_stop_stopped(Ed1Debugger* debugger);
int ed1_stop_exited(Ed1Debugger* debugger);
int ed1_stop_status(Ed1Debugger* debugger);
const char* ed1_stop_file(Ed1Debugger* debugger);
int ed1_stop_line(Ed1Debugger* debugger);
const char* ed1_stop_function(Ed1Debugger* debugger);

/* What the debugger itself printed on its way to that stop - cdb's or lldb's
   own words, and the debugged program's output along with them, since a
   debuggee writes down the debugger's stream. Empty when it said nothing.

   Six things were readable about a stop here and this was the seventh, kept on
   the native side where the terminal half could reach it and the window could
   not. So a stop the window could not make sense of was reported as "the
   debugger stopped answering" with nothing under it, while the terminal
   printed what had actually been said. */
const char* ed1_stop_said(Ed1Debugger* debugger);

/* And the same thing with the debugger's own words taken out: what the program
   printed on its way to this stop, for the console. Empty when it printed
   nothing. The filtering is dbg_programOutput's, on the native side, so both
   front ends show the same thing. */
const char* ed1_stop_output(Ed1Debugger* debugger);

/* Whether a stop that named no place is a program standing where there is no
   source - stepping off the end of main - rather than a debugger that died.
   The first is carried on from; the second is the end of the session. */
int ed1_stop_no_source(Ed1Debugger* debugger);

/* What is in scope where it stopped, read after a move. */
int ed1_locals_count(Ed1Debugger* debugger);
const char* ed1_local_name(Ed1Debugger* debugger, int index);
const char* ed1_local_type(Ed1Debugger* debugger, int index);
const char* ed1_local_value(Ed1Debugger* debugger, int index);

// How a variable is written in the Debug tab, which variable a line of it is
// about, and the setting of one. The window writes the line with the first and
// reads it back with the second rather than counting rows of its own, exactly
// as it does with the frames - see dbg_variableLine.
//
// ed1_set_variable answers 0 when the debugger would not take it, and
// ed1_set_complaint is what it said about that: its own words name the mistake
// better than any this could invent. A set that worked re-reads the locals, so
// ed1_local_* answer with what is in there afterwards.
const char* ed1_local_text(Ed1Debugger* debugger, int index);
int ed1_locals_on_line(Ed1Debugger* debugger, const char* line);
int ed1_set_variable(Ed1Debugger* debugger, const char* name, const char* value);
const char* ed1_set_complaint(Ed1Debugger* debugger);

// Expressions the debugger keeps answering, read again after every move and
// every change of frame - that being the whole of what a watch is. The list
// lives in the core beside the debugger rather than in either front end, so
// that both refresh them at exactly the same moments.
//
// ed1_watch_set with an empty expression takes that watch away; there is no
// list here that needs a key of its own for removing something.
void ed1_watch_add(Ed1Debugger* debugger, const char* expression);
int ed1_watch_count(Ed1Debugger* debugger);
const char* ed1_watch_text(Ed1Debugger* debugger, int index);
const char* ed1_watch_expression(Ed1Debugger* debugger, int index);
int ed1_watch_on_line(Ed1Debugger* debugger, const char* line);
void ed1_watch_set(Ed1Debugger* debugger, int index, const char* expression);

// And how it got there: the frame it is standing in first, and what called it
// after that. One frame is a program standing in main, which the Debug tab
// says nothing about - see dbg_readFrames for why the stack stops there.
int ed1_stack_count(Ed1Debugger* debugger);
const char* ed1_stack_function(Ed1Debugger* debugger, int index);
const char* ed1_stack_file(Ed1Debugger* debugger, int index);
int ed1_stack_line(Ed1Debugger* debugger, int index);

// How a frame is written in the Debug tab, and which frame a line of that tab
// is about - -1 for a line that is not one. The window writes the line with
// the first and reads it back with the second rather than counting rows of its
// own, so that the row it acts on is the row the core named. See dbg_frameLine.
const char* ed1_stack_text(Ed1Debugger* debugger, int index);
int ed1_stack_on_line(Ed1Debugger* debugger, const char* line);

// Which frame the variables are to be read from. All three debuggers keep a
// current frame and answer with its variables, so this is what makes a
// caller's locals readable at all; ed1_locals_* answer from it afterwards.
// Every move puts it back to 0, the frame the program stopped in.
//
// ed1_looking_text is the line that says whose they are, empty at frame 0
// where the stop's own line already says it.
int ed1_debugger_look_at(Ed1Debugger* debugger, int which);

// The tab's first line, which names the frame the program stopped in. The
// window writes it with this and compares a clicked row against it, so that
// enter on that row is enter on frame 0 - see dbg_stopLine.
const char* ed1_stop_line_text(const char* file, int line, const char* function);
int ed1_looking_at(Ed1Debugger* debugger);
const char* ed1_looking_text(Ed1Debugger* debugger);

#ifdef __cplusplus
}
#endif

#endif
