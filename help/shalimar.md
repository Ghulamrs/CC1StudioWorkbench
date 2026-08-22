# Shalimar

Shalimar is a small numeric language of our own, interpreted by an iOS app and
compiled by **shc**. The full specification is
[Appendix A](appendix-a-shalimar-language.md).

| | |
| --- | --- |
| suffix | `.shl` and `.shm` — both are Shalimar |
| compiler | `shc` |
| targets | `x86_64-windows`, `x86_64-linux`, `arm64-darwin` |
| debug | `--debug` — links a runtime that can stop, changes no code |
| release | nothing |

`.shm` is what the phone app writes and what every program it ships is; `.shl`
is the desktop name and what a new file gets. The editor treats them the same.

## Laying it out is not the C rules

`n : n + 1` is an **assignment** in Shalimar and a **goto label** in C, and a
label is laid out in its function's own column. A Shalimar program indented by
the C rules therefore walks left one statement at a time. `IndentDialect`
exists for exactly that, which is why the Language menu matters here.

Shalimar also has **no block comment, no escapes in a literal, no character
literal and no preprocessor**. The colouring knows all four.

## No include, and none coming

A call to a function this file does not define is **looked for in the project's
other files**, and what is found is compiled in. There is nothing to declare and
nothing to keep up to date.

- Only functions are looked for. A brought-in function carries its own file's
  globals — the ones it actually reads — and no others.
- Nothing arrives that was not asked for, so a `main()` is never pulled in and
  a directory full of programs is the ordinary case rather than a collision.
- A name **something wants** in two files is refused, naming both. A name
  nobody wants may be in as many files as it likes.

**The project is what says which files those are.** Given one file on a command
line, shc looks beside it; given a project, the editor hands it the group, with
the program first. A directory knows what is next to it and a project knows
what is its own.

Since every `.shm` has a `main()` and nothing inside a file can say it is the
program, **`"target"` in `ed1.json` picks which one it is**. A target named
after none of them is refused rather than guessed at.

## A program that stops itself

There is no debug information — no `-g`, no DWARF, no CodeView — and none is
planned. What there is instead is better for this language: the compiler
already emits `shm_line(unit, line)` before every statement so a runtime error
can name where it happened, and a **debug build offers that same position to a
session inside the program**.

So `F9`, `F8`, `F7` and `F6` work on Shalimar with no debugger installed
anywhere, on all three targets — including `x86_64-windows`, where cc1's own
debugging stops. What it cannot do is read a variable: the compiler emits no
table of a function's names against its frame slots, and the Debug tab says so
rather than showing an empty list.

**A release build cannot be stopped at all.** That is the boundary, not a
limitation of it: the release runtime has no code in it for stopping. Rebuild
in debug — it is the same source and byte-identical compiler output.

## Why Shalimar cannot share a program with C

It is refused twice, and the two refusals say different things.

**In one group with C or C++** — no compiler takes both, so naming one cannot
help. The message names the group, because splitting the list is the fix.

**In a group of its own beside them** — where every other pair of languages now
builds. This one is about what a Shalimar object *is*:

- every unit exports `shm_user_main`, `shm_init_globals` and `shm_name_files`
  whatever file it came from, so two of them collide by construction;
- the runtime archive owns `main`, so a C program with its own cannot link it,
  and a Shalimar object without the runtime has nothing to call;
- the language has no declarations, so a call across a link could not be
  checked — and checking the whole program together is the rule the cross-file
  search exists to keep.

The first two could be fixed. The third is the language. `Compiler-S/docs/LINKING.md`
has it with the actual linker output and with what would have to change.

**A project that wants Shalimar beside C is a project that builds two
programs.**
