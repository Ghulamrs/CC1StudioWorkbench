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

**It shows the assembly for any of the three targets.** Ctrl-T, or the Target
menu. Two of the three reach `-S` and no further on any given machine, since the
assembler is the host's - which is exactly what the assembly tab is for.

**It colours what it shows, per language.** Keywords, types, strings, character
constants, comments, numbers and the preprocessor, chosen from the file's
suffix: `.c` and `.h` as C, `.cpp` and its family as C++, `.s` and `.asm` as
assembly - which means the assembly tab is coloured too, with cc1's directives,
labels and mnemonics told apart. A keyword inside a string stays a string, an
escaped quote ends nothing, and a block comment opened above the top of the
screen still colours what is on it. Sixteen-colour codes throughout, because
those are the ones a Windows console renders in virtual-terminal mode.

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
`/TP /EHsc /std:c++17` - rather than left to infer it from the suffix.

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
  "arch": "x86_64-windows",
  "indent": 4,
  "tabs": false,
  "groups": {
    "Rules": ["src/indent.cpp", "src/syntax.cpp"],
    "Examples": ["examples/hello.c", "examples/smart.cpp"]
  }
}
```

Six keys, flat except the groups, and every one has a default - so `{}` is a
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
* **Debug** - variables, watches and the call stack, when there are any. There
  are none today: cc1 emits no debug information at all - no `-g`, no DWARF, no
  CodeView - so nothing can read symbols out of what it produces. The tab says
  so rather than sitting blank, and filling it is compiler work.
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
Delete answered with anything but `yes` keeps the file, and that a build lands
the caret on the line the compiler named. One program for both machines, rather
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

`buffer`, `indent`, `menu` and `tree` touch no screen and no OS. `indent`, `syntax`, `json`, `project` and
the diagnostic parser are the pieces with a contract, and `tests/test.cpp`
checks them - 138 cases, including that a Windows path's drive letter is not
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
| `Ctrl-B` | build with cc1 |
| `Ctrl-F` | lay the whole file out |
| `Tab` | lay this line out, in the leading space |
| `F2` / `F3` | previous / next open file |
| `Ctrl-L` | line numbers |
| `Ctrl-K` | cc1 or cl |
| `Ctrl-T` | next target (cc1 only) |
| `Ctrl-W` | next pane |
| `Ctrl-P` / `Ctrl-E` | project pane / bottom panel |
| `Ctrl-S` / `Ctrl-Q` | save / leave |
| `F1` | the keys, in the console |
