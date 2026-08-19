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
enum { ED1_LANG_PLAIN = 0, ED1_LANG_C, ED1_LANG_CPP, ED1_LANG_ASM };
enum { ED1_TOOL_AUTO = 0, ED1_TOOL_CC1, ED1_TOOL_MSVC };
enum { ED1_CONFIG_DEBUG = 0, ED1_CONFIG_RELEASE };

/* Catches a crash and writes the faulting address and a symbolised stack to
   ed1-fault.log. There is no WinDbg on the machine this is built for, and
   dbghelp is, so the program carries its own. Does nothing off Windows. */
void ed1_watch_for_faults(const char* logPath);

/* ---- laying code out ---------------------------------------------------- */

/* The whole buffer in, the whole buffer out, lines separated by \n. */
char* ed1_reindent(const char* text, int width, int tabs, int caseIndent);

/* What a newline typed at row and col should be followed by. */
char* ed1_indent_after_newline(const char* text, int row, int col,
                               int width, int tabs, int caseIndent);

/* The leading space one line should have, for the tab key and for a line whose
   own layout changed the moment a brace was typed on it. */
char* ed1_indent_for(const char* text, int row, int width, int tabs, int caseIndent);

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

int ed1_language_for(const char* path);

/* One kind per byte of the line, written into kinds. `state` carries the block
   comment across lines and is read and written. Returns how many were set. */
int ed1_highlight(const char* line, int language, int* state,
                  unsigned char* kinds, int kindsSize);

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
int ed1_begin_project(Ed1Project* project, const char* directory, const char* name,
                      const char* firstFile);
int ed1_save_project(Ed1Project* project);

const char* ed1_outcome_message(Ed1Project* project);
const char* ed1_outcome_path(Ed1Project* project);

/* ---- compilers ---------------------------------------------------------- */

const char* ed1_arch(int index);              /* 0, 1, 2 */
const char* ed1_toolchain_name(int kind);
int ed1_resolve(int toolchainKind, int language);
int ed1_can_compile(int kind, int language);
const char* ed1_refusal(int kind, int language);
int ed1_uses_arch(int kind);

const char* ed1_shown_command(const char* cc1, const char* cl, int kind,
                              const char* source, int language, const char* arch,
                              int config);

typedef struct Ed1Build Ed1Build;

Ed1Build* ed1_build(const char* cc1, const char* cl, int kind, const char* source,
                    int language, const char* arch, int config);
void ed1_build_free(Ed1Build* built);

int ed1_build_ok(Ed1Build* built);
const char* ed1_build_output(Ed1Build* built);
const char* ed1_build_assembly(Ed1Build* built);   /* joined with \n */
int ed1_build_assembly_lines(Ed1Build* built);
int ed1_build_has_error(Ed1Build* built);
int ed1_build_error_line(Ed1Build* built);
int ed1_build_error_column(Ed1Build* built);
const char* ed1_build_error_message(Ed1Build* built);

#ifdef __cplusplus
}
#endif

#endif
