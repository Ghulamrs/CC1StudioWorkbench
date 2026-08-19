# ed1

A terminal editor that drives [cc1](../Compiler-C), and nothing else. It runs on
Windows, which is what it is for, and on a Mac, which is where it is written.

```
 File   Edit   Build   Target   Help
 factorial.c  broken.c
+------------------------+---------------------------------------+
| + include              |   1 int factorial(int n)              |
| - src                  |   2 {                                 |
|     broken.c           |   3     if (n < 2)                    |
|     factorial.c        |   4         return 1;                 |
|   README               |   5 }                                 |
+------------------------+---------------------------------------+
| Console  Debug  Assembly                             6 lines   |
| $ cc1 -S src/broken.c -arch x86_64-windows                     |
| src/broken.c:3:13: error: expected an expression               |
|         int x = ;                                              |
|                 ^                                              |
+----------------------------------------------------------------+
| broken.c  5 lines        x86_64-windows  3/5  col 13   [text]   |
| 3:13: error: expected an expression                             |
+----------------------------------------------------------------+
```

## What it does

**It lays C out as you type it.** This is the part worth having. Type a function
with no leading space anywhere and it arrives indented: bodies in a step, a
closing brace under the line that opened its group, an unbraced `if` indenting
exactly one statement, `case` labels in their switch's own column, `#` at the
left margin. Braces inside strings, character constants, `//` and `/* */` are
text and are not counted. Tab in the leading space puts the line where it
belongs rather than adding a step; Ctrl-F does the whole file.

The rules come from Shalimar's indenter, which had already settled them, with
four things added that C has and that language did not: escapes inside literals,
block comments that outlive their line, the preprocessor, and switch labels.

**It builds, and lands on the error.** Ctrl-B saves and runs `cc1 -S`. cc1 stops
at the first error - `Source::fail` is `[[noreturn]]` - so this is a fix-one,
build-again rhythm rather than a list of twelve problems, and the editor is
built for that rhythm. The caret goes to the line and column cc1 named, and
cc1's own words, caret line and all, are in the console.

**It runs what it built.** F5, or Run in the Build menu, saves the file and
hands it to the compiler with neither `-S` nor `-c` - so cc1 compiles, assembles
and links it, and cl does the same without `/c` - and then starts the program
and puts everything it prints in the console, under everything the compiler
said. What it returned is shown as a number rather than as success or failure,
because only the program knows which its number meant: `[program returned 3]`.
Its input is emptied rather than left as the editor's own, so a program that
reads does not eat the keyboard.

Only for the target this machine is. Everything else is turned away before
anything is built, with the target to switch to named - the assembler and linker
cc1 hands off to are the host's, so a cross build stops at the assembly and
there is nothing to start. cl is never in that position: it builds for the
machine it was installed on.

**It stops the program on a line and walks through it.** F9 puts a breakpoint
on the line the caret is on - a `*` in the gutter, and no compiler needed to put
it there. F8 builds with `-g`, starts the debugger, sets every breakpoint it has
been given and runs; when the program stops, the caret goes to the line, a `>`
marks it, and the Debug tab says where it is and what is in scope:

```
stopped at stepped.c:11 in main

  total = 0   [int]
  i = 1   [int]
```

F6 steps into a call, F7 over one, F8 carries on, and the Debug menu has those
and step-out. Those variables are cc1's own DWARF, read back by the machine's
own debugger.

It drives that debugger rather than being one: lldb on a Mac, gdb on the Linux
box, both spoken through `src/debugger.cpp`, which is the one place their two
vocabularies differ. `x86_64-windows` has no debugging at all and says so -
cc1 generates MASM there, which carries no line table.

Both front ends have it, from that same core: the window has a Debug menu with
the same keys, a red dot in its gutter where a breakpoint is and an arrow where
the program is standing, and the same words in its Debug tab. There is an
awkwardness worth stating plainly, though. The window only runs on Windows, and
Windows is exactly where there is nothing to debug - so on the one machine that
can run the GUI, F8 will always answer "no debugger here". What it does there is
set breakpoints, which are the editor's own note and need no debugger, and give
the reason for the rest.

That is why `tests/test.cpp` links `winforms/bridge.cpp` and drives the
window's own seam - `ed1_build_program`, `ed1_debugger_start`, the stop and the
locals - on the machines that do have a debugger. Everything the window asks
the core to do is checked there; what is left untested is the Windows Forms
glue around it, and nothing else.

Two things about driving lldb are worth writing down, because both cost an hour
and neither is guessable. It must be put in synchronous mode with `script
lldb.debugger.SetAsync(False)`, or over a pipe it forwards each command to the
program instead of running it, and every answer after `run` is an echo. And the
marker used to know an answer is complete has to be printed in two halves -
`print("<<ed1" + "-done>>")` - because lldb echoes the command that contains it,
so a marker written whole appears before the answer rather than after it, and
every reply read that way is the one before the one asked for.

**It shows the assembly for any of the three targets.** Ctrl-T, or the Target
menu. Two of the three reach `-S` and no further on any given machine, since the
assembler is the host's - which is exactly what the assembly tab is for. A
project that does not name a target gets this machine's, rather than the
`x86_64-windows` it used to get wherever it was opened.

**It colours what it shows, per language.** Keywords, types, strings, character
constants, comments, numbers and the preprocessor, chosen from the file's
suffix: `.c` and `.h` as C, `.cpp` and its family as C++, `.s` and `.asm` as
assembly - which means the assembly tab is coloured too, with cc1's directives,
labels and mnemonics told apart. A keyword inside a string stays a string, an
escaped quote ends nothing, and a block comment opened above the top of the
screen still colours what is on it. Sixteen-colour codes throughout, because
those are the ones a Windows console renders in virtual-terminal mode.

**Selecting, cutting and pasting.** Shift with the arrows, Home, End or the
page keys extends a selection from where the caret was; moving without shift
lets it go. `Ctrl-C`, `Ctrl-X` and `Ctrl-V` copy, cut and paste, and with
nothing selected the first two take the whole line, which is what is nearly
always wanted. Typing or backspacing over a selection replaces it, and a cut is
one undo step. The clipboard is the editor's own, not the machine's.

**Text that is not ASCII.** The buffer stays bytes throughout, so a file opened
and saved comes back byte for byte whatever is in it - but the caret moves by
*characters*, never landing inside one, and backspace removes a whole letter
rather than its last byte. Screen columns are counted properly too: an Urdu or
accented letter is one column though it is two bytes, a Chinese character is
two columns though it is three bytes, and a mark drawn on top of another letter
is none. Tab stops line up on columns rather than bytes for the same reason.

**Undo and redo**, on `Ctrl-Z` and `Ctrl-Y`. A run of typing is one step, so
undoing gives a word back rather than one letter at a time; a newline, a
re-layout and a replace are each a step of their own. Moving the caret ends the
run, which is what stops a whole session collapsing into a single undo. The
caret goes back to where it was, not just the text.

The `*` that marks a modified file follows undo properly: undoing back to the
point the file was written at clears it, because the text and the disk agree
again. That is done by remembering how deep the history stood when the file was
saved rather than by comparing the whole text - and when the saved point falls
off the end of the capped history it says modified, which is the safe way to be
wrong.

It works by keeping snapshots rather than by inverting each operation. A source
file is a few thousand short strings, the history is capped at a hundred steps,
and the simple version is the one that cannot be subtly wrong - which for undo
matters more than for anything else here.

**Find and replace.** `Ctrl-F` asks, `Ctrl-G` moves on to the next, `Ctrl-R`
replaces. Searching wraps round the end of the file and stops when it arrives
back where it began. Replace changes every occurrence and says how many - and
`Ctrl-Z` puts them all back in one step.

**Debug or release**, on `Ctrl-D` or in the Build menu, remembered in the
project file. What each compiler can do about it differs, and the editor says
which rather than pretending they are the same:

| | debug | release |
|---|---|---|
| `cl` | `/Od /D_DEBUG` | `/O2 /DNDEBUG` |
| `cc1`, `x86_64-linux` and `arm64-darwin` | `-g -D_DEBUG=1` | `-DNDEBUG=1` |
| `cc1`, `x86_64-windows` | `-D_DEBUG=1` | `-DNDEBUG=1` |

cc1 still has no `-O`, so for it release is the define and nothing else. That is
not nothing - it is what `assert` and every `#ifdef NDEBUG` in the source are
looking for - but passing it a `-O` it would refuse would be worse than saying
so plainly.

Debug is more than the define now. cc1 writes DWARF for two of its three
targets - line tables, types, objects and lexical blocks, read by both `gdb`
and `lldb` - so a debug build for those asks for `-g` and gets it. The third
does not: cc1 generates MASM for `x86_64-windows`, MASM carries no line table,
and the assembler there cannot spell the relocations CodeView would need. cc1
does take `-g` for that target in the GNU spelling, which routes the DWARF out
of the Linux emitter, but the editor asks each target for the assembly its own
assembler reads. So that target gets the define alone, and no `-g` it would
refuse.

**Line numbers down the left**, in the manner of Shalimar's, with the caret's
own line picked out. `Ctrl-L` turns them off.

**Tabs for the files you have open.** Opening from the project pane adds one;
F2 and F3 move between them. Each tab remembers its own caret and its own
scroll, so coming back to a file puts you where you were rather than at the top.

**The file chooses its own compiler.** cc1 compiles C; C++ goes to cl. That is
the whole routing rule, and it is the default - the status bar shows what will
actually run, with a `*` when the file is what picked it. `Ctrl-K` cycles
through automatic, cc1-for-everything and cl-for-everything when you want to
say so yourself; a choice made by hand is kept rather than quietly overridden,
and a file the chosen compiler cannot take is turned away with a reason instead
of a wall of somebody else's parse errors.

Each compiler is also *told* which language it is being handed - `/TC` or
`/TP /EHsc /std:c++14` - rather than left to infer it from the suffix. C++14
because that is what this arena holds itself to: cc1 is written in it, so the
editor is built in it, and C++ compiled here is compiled as it.

**cl is found without a Developer Command Prompt.** ed1 asks Visual Studio 2022
where it lives, runs `vcvars64` once, and keeps the environment for the rest of
the session. Started from an ordinary console with `cl` nowhere on PATH, it
still builds C++. With `cl` in use the target chooser goes quiet, because cl
builds for the host it was installed as and a menu offering a choice that does
nothing would be the status bar telling a lie.

Both diagnostic spellings are read without being told which to expect:

```
file:line:col: error: message         cc1, and gcc and clang with it
file(line,col): error C2059: message  cl, and ml64
```

so pointing it at a third compiler that speaks either one needs no new code. A
`cl` run wants a Developer Command Prompt - that is where `cl.exe` is on PATH -
and cl's own listing is MASM, which the assembly tab already colours.

## The project

A project is one file, `ed1.json`, and there does not have to be one - without
it the pane on the left shows the directory, as it always did.

```json
{
  "name": "ed1",
  "toolchain": "auto",
  "config": "debug",
  "arch": "x86_64-windows",
  "indent": 4,
  "tabs": false,
  "groups": {
    "Rules": ["src/indent.cpp", "src/syntax.cpp"],
    "Examples": ["examples/hello.c", "examples/smart.cpp"]
  }
}
```

Seven keys, flat except the groups, and every one has a default - so `{}` is a
valid project file. Comments with `//` are allowed, because a file people edit
by hand is a file people leave notes in.

Two things are deliberately *not* in it. Where cc1 and cl live is a fact about
a machine rather than about a project, and a path written into a shared file is
a path that is wrong on the other machine - those come from `--cc1`, `--cl`,
`$CC1` or PATH. And the indent settings are a number and a flag rather than a
nested object, because an object with two members in it is a nest for no gain.

A group is the project's own arrangement, not a directory: moving a file
between groups changes two lists and nothing on disk. The Project menu makes
files, renames them, moves them between groups and deletes them - the last one
asks you to type `yes`, because it is the only thing here that cannot be
undone.

**The structure is limited to two levels**, and that is a rule the project
keeps rather than a habit anyone is asked to remember: a file sits in the root
or in one directory under it, and never deeper. As many directories as you like
may sit side by side - `src`, `tests`, `examples`, `docs` and any others - but
none of them holds another. A structure nobody has to explore is one anyone can
read at a glance.

Settings in the project are what this project always does; anything named on
the command line is applied after it and wins, which is what today needs.

## The panel

Three tabs:

* **Console** - the command, everything cc1 said, and the error. Enter goes to
  the line it named.
* **Debug** - what the build produced, read back out of its own assembly, and a
  line above it saying what debug information this target actually has. This is
  not a debugger and does not pretend to be: cc1 does write DWARF for two of the
  three targets now, but a debugger needs a program to run and nothing here is
  assembled, linked or run - the build stops at `-S`. What there always is, is
  the assembly: which functions came out and how much stack each takes, what is
  exported, what is called but not defined, and what strings ended up in the
  binary. That is what you look at when nothing is running. Both front ends ask
  the core for those words rather than writing them out, which is how the window
  came to be saying there was no debug information a day after there was.

  It reads both spellings, since cc1 writes GNU on two targets and MASM on the
  third and cl writes MASM always - including a string MASM broke across two
  `DB` lines, and the arm64 frame size that is put in a register before it is
  subtracted. C++ names are decorated in a listing, so on Windows
  `src/demangle_win.cpp` reads them back with `UnDecorateSymbolName`:
  `?value@Counter@demo@@QEBAHXZ` shows as `demo::Counter::value(void)const`.
  One file, compiled into both front ends.
* **Assembly** - what `-S` produced.

## Trying it

```
ed1 examples/smart.cpp --project examples --toolchain msvc
```

`examples/smart.cpp` is the one to open first. It is a small owning class - one
object, deleted once, moved rather than copied, copying refused by the compiler
rather than by the destructor - and something that exercises it and prints what
it is doing. It is C++ on purpose: cc1 compiles C, so this is the file that
shows the MSVC backend doing the work. Ctrl-B fills the assembly tab with cl's
listing.

`examples/hello.c` is the C one, for cc1, where Ctrl-T changes which of the
three architectures the assembly is for.

Handing C++ to cc1 is caught before it is run: the editor says so and points at
Ctrl-K, rather than letting a C compiler fail somewhere inside the first class
with a diagnostic that explains nothing.

## Building and checking

On a Mac or on Linux:

```
make
make check
```

Object files go to `src/obj`, so a listing of `src` is the code and nothing
else.

### As a Windows Forms application

```
winforms\ed1gui.vcxproj
```

An ordinary macOS-style menu-and-panes window: the project down the left, the
file in the middle, Console, Debug and Assembly across the bottom. It is C++/CLI
and it **consumes the native core directly** - the same indent.cpp, syntax.cpp,
project.cpp and compile.cpp the terminal editor uses, compiled into the same
binary as native code. Nothing is duplicated: laying a file out, colouring it,
reading ed1.json and choosing between cc1 and cl are all the same code running.

**The project operations** are on the Project menu: new project, save project,
add this file, new file, rename, move to group and delete. None of them is
written twice - `src/workspace.cpp` holds what changing a project actually
involves (check the rule, do the disk work, keep the list in step, write the
project back), and both front ends call it. The terminal asks its questions on
the message line and the window asks them in a dialog; what happens after the
answer is the same code. Deleting asks plainly, with No already chosen.

**Tabs for the files you have open**, one text box each, so every tab keeps its
own caret, scroll position and undo history. Opening a file already open brings
its tab forward rather than opening it twice, and an untouched unnamed tab is
reused rather than left behind. **Line numbers** are painted down the left, with
the caret's own line picked out; the gutter widens for the last line and does
not shrink back as you scroll.

It edits the way the terminal one does. A newline takes the indentation its
place asks for, a typed `}` `#` or `:` puts its own line where it belongs, and
Tab in the leading space lays the line out rather than adding a step. Find,
find-next, find-previous and replace call the same `find.cpp`. Undo, redo, cut,
copy, paste and select-all are the text box's own - it keeps the history the
typing went into, and there is no sense in keeping a second one beside it.

Typed flat into the window, `int twice(int n) {` and four lines after it come
back laid out, with the closing brace snapping to column 0 as it is typed.

`winforms/show.ps1` starts it, optionally builds, and saves a picture of the
window - which is how this front end has been looked at throughout, since it is
written on a Mac and only runs on Windows:

```
.\show.ps1 -Project C:\Users\me\Editor -Files examples\smart.cpp -Build
```

It photographs the editor's own window with `PrintWindow` rather than grabbing
the screen, so it gets the window even when something is in front of it and
captures nothing else that happens to be on the desktop. A window needs a
desktop and an ssh session has none, so from a remote shell it must be run in
the logged-on session - the script's own header gives the `schtasks /it`
incantation for that.

The managed form talks to it through `winforms/bridge.h`, which names no C++
type at all - opaque handles and char pointers, nothing else. That is not
fastidiousness; three separate things about mixed native/managed binaries have
to be respected, and each one was found the hard way:

* A translation unit that includes `<fstream>` gets iostreams' static
  initialisation, and linking it corrupts the heap **before main runs**. The
  core uses stdio now.
* A function-local `static` with a destructor registers an `atexit` handler the
  first time it is reached, and that corrupts the heap too. The three in the
  core are made once and never destroyed.
* The managed side must not instantiate the STL the native side instantiates.

The second of those was found by giving the program its own debugger: there is
no WinDbg on the machine, so `bridge.cpp` installs a vectored exception handler
that walks and symbolises its own stack into `ed1-fault.log`. It printed
`Json::get -> atexit -> register_onexit_function -> RtlSizeHeap` and named the
line.

### In Xcode

```
open Editor.xcodeproj
```

An ordinary macOS command line tool target, compiled by clang++ - `Editor.xcodeproj`
holds nothing but a target, two configurations and the same files `src` already
had. Debug and Release both build, and the binary Xcode produces was put
through the whole session harness rather than merely run once: 70 checks, the
same as make's.

The project file is **generated**, by `make xcodeproj`, and its source list is
read out of the Makefile. A hand-kept project drifts - someone adds a file to
the Makefile, forgets the other one, and Xcode quietly builds yesterday's
editor. There is one source list and it is the Makefile's.

One thing to know before reaching for the Run button: ed1 is a terminal
program, and Xcode's console is a pipe rather than a terminal. Started from
there it cannot put the terminal into raw mode and will print escape sequences
instead of drawing. Build and debug it in Xcode; run it in Terminal, and attach
from Xcode if you want breakpoints while it is running.

On Windows, where there is no make:

```
build
build check
```

`check` runs both halves. `tests/test.cpp` checks the pieces that never see a
terminal. `tests/session.cpp` drives the editor itself - it types into it,
walks its menus, and then looks at what landed on the screen and on the disk:
that a function typed flat comes back laid out, that keywords are coloured,
that New file makes a file and puts it in the project, that a path two
directories deep is refused, that Rename moves it and the project follows, that
Delete answered with anything but `yes` keeps the file, and that undo takes back a run of
typing and redo returns it, that the modified marker comes back when you undo
past a save and goes when you redo to it, that a selection can be copied,
cut, pasted and typed over, that a file of Urdu and accented text survives
being saved and that backspace takes a whole letter out of it, that find lands the caret on the right line and
Ctrl-G moves on, that replace changes the text and quitting
without saving leaves the file alone, and that a build lands the caret on the
line the compiler named and that its two configurations produce different
code. One program for both machines, rather
than a shell script and a PowerShell script that would drift apart.

Set `CC1` to run the cc1 build cases; the cl ones need nothing, since the
editor finds Visual Studio itself.

`build.bat` finds Visual Studio 2022 itself. The search is pinned to `[17.0,18.0)`
on purpose - a bare `vswhere -latest` reaches past 2022 to any newer Visual
Studio on the machine, which is not the toolset this is built with. Both builds
are warnings-as-errors: `-Wall -Wextra -Werror -pedantic` for clang and gcc,
`/W4 /WX` for MSVC. MSVC found a shadowed variable that clang did not, which is
the argument for building both.

## Why it is laid out the way it is

Only `terminal_win.cpp` is Windows-specific, and it is a hundred lines. Windows
10 and later speak the same escape sequences as a Unix terminal once
`ENABLE_VIRTUAL_TERMINAL_PROCESSING` and `ENABLE_VIRTUAL_TERMINAL_INPUT` are
set, so the drawing, the status bar and the key decoding are one piece of code
on both machines. The key decoding in particular lives in `terminal_common.cpp`
rather than in each platform file: two copies of that drifting apart is the
house bug, and this is the one place it was easy to prevent.

**This is C++14, and `src/path.cpp` is what that costs.** cc1 is written in
C++14, so the editor that drives it is too - one standard across the arena,
enforced by all three toolchains rather than agreed and forgotten. The only
thing in here that wanted C++17 was `<filesystem>`, and what was actually used
of it was small: joining and splitting paths, making one absolute or relative
to another, listing a directory, and four operations on files. So it is written
out, `opendir` on one machine and `FindFirstFile` on the other, in one file
behind one set of functions - a single place the two spellings can drift apart,
small enough to read, and with 35 cases of its own. Everything works in forward
slashes and hands them back, which is what the project file already wanted.

`buffer`, `indent`, `menu` and `tree` touch no screen and no OS. `indent`, `syntax`, `json`, `project`, `path` and
the diagnostic parser are the pieces with a contract, and `tests/test.cpp`
checks them - 332 cases, including that a Windows path's drive letter is not
mistaken for a `line:col` separator, that a brace inside a string is not
counted, and that `class` is a keyword in C++ and nothing in particular in C,
and that cl's `bad.c(3,13)` is read as well as cc1's `bad.c:3:13`. They run on
both machines.

One bug in here could only have been found by running it on Windows: `_popen`
hands its string to `cmd /c`, and cmd removes the first and last quote when a
command has both a quoted program and quoted arguments - which every command
here does, since paths hold spaces. The fix is an extra pair around the whole
thing for cmd to eat. Until then the compiler was never reached, and cmd said
"The filename, directory name, or volume label syntax is incorrect" instead.

## Usage

```
ed1 [file.c] [--project dir] [--toolchain cc1|msvc] [--compiler path]
    [--cc1 path] [--width n] [--tabs] [--case-indent]
```

`--cc1` names the compiler; `$CC1` and `--compiler` do the same. Indentation is four spaces
because that is what cc1's own sources use, and they contain no tab at all.

| key | |
|---|---|
| `F10` | the menu |
| `Ctrl-B` | build |
| `F5` | build it and run it |
| `F9` | breakpoint on this line |
| `F8` | start debugging, or carry on |
| `F7` / `F6` | step over / step into |
| `Ctrl-F` / `Ctrl-G` | find / find the next |
| `Ctrl-R` | replace |
| shift + arrows | select |
| `Ctrl-C` / `Ctrl-X` / `Ctrl-V` | copy / cut / paste |
| `Ctrl-Z` / `Ctrl-Y` | undo / redo |
| `Ctrl-A` | lay the whole file out |
| `Tab` | lay this line out, in the leading space |
| `Ctrl-D` | debug or release |
| `F2` / `F3` | previous / next open file |
| `Ctrl-L` | line numbers |
| `Ctrl-K` | cc1 or cl |
| `Ctrl-T` | next target (cc1 only) |
| `Ctrl-W` | next pane |
| `Ctrl-P` / `Ctrl-E` | project pane / bottom panel |
| `Ctrl-S` / `Ctrl-Q` | save / leave |
| `F1` | the keys, in the console |
