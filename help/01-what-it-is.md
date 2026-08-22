# 1. What it is

RStudio is an editor for three languages of our own: **C** and **C++** through
[cc1](../../Compiler-C) and `cl`, and **Shalimar** through
[shc](../../Compiler-S). It edits, builds, runs and debugs, and it does all
four without leaving the keyboard.

It is not a general-purpose editor that happens to know some compilers. It was
written for cc1 and grew the other two, and that shows in what it does well: a
diagnostic puts the caret on the line, a build shows you the assembly it
produced, and a breakpoint works the same whichever of the three you are in.

## Three variants, one core

| | runs on | what it is |
| --- | --- | --- |
| `ed1` | macOS, Linux | the terminal editor |
| `winconsole.exe` | Windows | the same editor, over the Windows console |
| `ed1gui.exe` | Windows | the same editor in a window |

**Every rule lives in `src/` and all three call it.** Laying a line out,
colouring it, reading `ed1.json`, choosing a compiler, driving a debugger —
one implementation each. Only `editor.cpp` with `terminal*.cpp`, and
`winforms/`, are specific to a front end.

That is not tidiness for its own sake. Two editors that behave nearly the same
are worse than one editor with two windows: the "nearly" is where the bugs
live. When something is asked for in one, the answer is to lift it into the
core and rewire the other.

The names `ed1` and `ed1gui` belong to the binaries. **RStudio** is what the
pair is called, and it is what `Help ▸ About` prints.

> Called *CC1 Studio Workbench* until 2026-08-22, when Shalimar became the
> third language and the old name stopped describing it.

It is also not **CC1 Studio**, which is a different thing for the same
compiler: that one is an extension that teaches VS Code about cc1, and this is
an editor of our own.

## Releases

**1.0 — the editor for C and C++.** cc1 and `cl`, the three targets, the
project file and its groups, the panel with its three tabs, and real debugging:
breakpoints, stepping, variables and the call stack. Complete in itself, and
what the name *CC1 Studio Workbench* described.

**1.1 — Shalimar.** A third language, and the release this manual is for.
Shalimar is not C with fewer rules, so it did not arrive as a suffix in a
table: it brought its own indent dialect, because `n : n + 1` is an assignment
here and a label there; its own way of finding the rest of a program, because
it has no `include`; and its own debugger, because a Shalimar program stops
itself and there is nothing to install.

The same release moved the compiler from being a property of the *project* to
being a property of a **group**, so a target can hold C and C++ together —
which is what made C the only language with a decision in it. It also gave C++
a compiler off Windows, which it had never had: `clang++` on a Mac and `g++` on
the Linux box, where `auto` used to route C++ to a `cl` that was not installed.

And it is why the product is called RStudio. Three languages is where *CC1
Studio Workbench* stopped being a description.

## What it will not do

Said here so that the rest of the manual does not have to keep apologising.

- **It does not have a plugin system**, a package manager, or a settings UI.
  Configuration is one JSON file per project and one per machine.
- **It does not guess.** Where two things could be meant, it asks or refuses,
  and the refusal says which file to move or which line to change.
- **It stops at the assembly for a cross target.** Building for a machine you
  are not on produces assembly and nothing else, because the assembler and
  linker it hands off to are this machine's.
- **It has no optimiser of its own.** What optimisation you get is whatever
  the compiler you chose does.
