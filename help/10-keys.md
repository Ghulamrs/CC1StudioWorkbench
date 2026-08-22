# 10. Keys, menus and the command line

## Every key

| | | | |
| --- | --- | --- | --- |
| `F1` | the keys | `Ctrl-B` | compile this file |
| `F10` | the menu | `F5` | run this file |
| `F2` / `F3` | previous / next file | `F4` | build the project |
| `F9` | breakpoint | `F8` | debug — start or continue |
| `F7` / `F6` | step over / into | `Ctrl-D` | debug or release |
| `Ctrl-Up` / `Ctrl-Down` | up / down the stack | `Ctrl-T` | next target |
| `Ctrl-K` | next compiler | `Ctrl-L` | line numbers |
| `Ctrl-W` | next pane | `Ctrl-P` | project pane |
| `Ctrl-E` | bottom panel | `Tab` | lay this line out |
| `Ctrl-A` | re-indent selection | `Ctrl-F` | find |
| `Ctrl-G` | find next | `Ctrl-R` | replace |
| `Ctrl-Z` / `Ctrl-Y` | undo / redo | `Ctrl-S` | save |
| `Ctrl-C` / `Ctrl-X` / `Ctrl-V` | copy / cut / paste | `Ctrl-Q` | leave |
| shift + arrows | select | | |

In the project pane, enter opens. In the panel, left and right change tab; on
Console, enter goes to the line the compiler named; on Debug, enter on a frame
looks at it and enter on the top line goes back.

**`RStudio.exe` on Windows differs on ten keys deliberately** — `Ctrl+PageDown` and
`Ctrl+PageUp` move between files there, because `F2` and `F3` are Rename and
Find next in a Windows application and pretending otherwise would be worse.

## The menus

**File** — New, Open, Save, Save As, Close, Next file, Previous file, Quit.
**Edit** — Undo, Redo, Cut, Copy, Paste, Select all, Find, Find next, Find
previous, Replace, Re-indent, Project pane, Bottom panel, Line numbers, Plain
frame.
**Project** — New project, Save project, Add this file, New file, Rename, Move
to group, Delete.
**Build** — Compile file, Run file, Build project, Run project, Debug, Release,
Console, Debug, Assembly.
**Debug** — Start / continue, Debug project, Toggle breakpoint, Step over, Step
into, Step out, Up the stack, Down the stack, Watch expression, Stop debugging.
**Target** — the three architectures.
**Language** — By extension, C, C++, Shalimar.
**Tools** — By language, cc1, shc, MSVC (cl), C++ (host). Ours first, then the
machine's.
**Help** — Contents, Keys, About.

## The command line

```
RStudio.exe [file] [--project dir] [--toolchain auto|cc1|msvc|shc|c++]
    [--config debug|release] [--cc1 path] [--cl path] [--shc path] [--cxx path]
    [--width n] [--tabs] [--case-indent] [--plain]
```

| | |
| --- | --- |
| `--toolchain` | use one compiler for everything; it says so where it cannot take the file |
| `--config` | `debug` (the default) or `release` |
| `--cc1`, `--cl`, `--shc`, `--cxx` | the programs to run |
| `--project` | what the pane on the left shows |
| `--width n` | columns per indent step (4) |
| `--tabs` | indent with tabs instead of spaces |
| `--case-indent` | `case` one step inside its `switch` rather than in its own column |
| `--plain` | frame the screen with `-`, `|` and `+` |

`$CC1`, `$SHC` and `$CXX` name the first, third and fourth when the flags do
not; `cl` is also found through Visual Studio 2022 itself, so no Developer
Command Prompt is needed.

**Settings in the project are what that project always does; anything on the
command line is applied after and wins.**

## The three machines

RStudio is built and checked on three, and each says something the others
cannot.

| | |
| --- | --- |
| a Mac | where it is written; `arm64-darwin` natively; clang++ |
| the Linux box | real g++, which is the only thing that can say whether the sources are ISO C++14; `x86_64-linux`; the only place a C++ group goes to g++ |
| the Windows box | the only machine with `cl`, and so the only one where a C and C++ target meets that linker; MSVC at `/W4 /WX` |

`tools/to-linux.sh` and `tools/to-windows.sh` relay this working tree to the
other two and build it from clean. Neither uses the checkout that may already
be on those machines: a clone only has what has been pushed, and those scripts
exist to check what is in front of you before it is committed.

## Building RStudio itself

```
make                                     the terminal editor
make check CC1=$HOME/... SHC=$HOME/...   both suites
build.bat check                          the same on Windows
build.bat gui                            the window, as RStudio.exe
```

Spell those paths with `$HOME` and never `~`: make hands a literal `~` to the
test process, where nothing expands it, and the build then fails with a
compiler that works perfectly when you run it by hand.
