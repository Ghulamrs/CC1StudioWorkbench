# A compiler per group

This replaces `NEXT-MIXED-PROJECTS.md`, which was written on 2026-08-22 while
none of it was built. It is all built now, on all three machines. What follows
is what was done, what the plan got wrong, and the two things that are still
limits.

## What it is

The compiler used to be a property of the project. It is a property of a
**group**, because a group is the smallest thing a target is made of. Each
group compiles to objects with its own compiler and the editor links them.

```json
"groups": {
  "Sources": ["src/main.c"],
  "Legacy":  { "files": ["src/legacy.c"], "toolchain": "c++" },
  "Engine":  ["engine/engine.cpp"]
},
"build": { "target": "three", "groups": ["Sources", "Legacy", "Engine"] }
```

`Project::targetParts` splits a target into `Part`s; `objectRecipe` compiles
one; `linkRecipe` links the lot; `buildParts` runs them. One part is still one
command - `buildTarget`, unchanged - so every project that worked before this
does exactly what it did.

## What the plan got wrong

**It thought this was mainly about C and C++.** It is mainly about C. C++ goes
to the machine's C++ compiler and there is nothing to choose; Shalimar goes to
shc, which is the only thing that reads it. **C is the one language two
compilers can both take** - cc1, which this editor was written for, or the
host's - so a group that names a compiler is, in practice, always a group of C
saying it wants the other one. That framing came from the user and it is the
right one; the plan did not have it.

**It assumed C++ meant cl.** `resolve` answered `ToolMsvc` for C++ on every
machine, which meant a C++ file on a Mac was routed to a compiler that is not
installed there and never could be. So "a C and C++ project together" could
only ever have worked on Windows, however well the rest of it was written.
`ToolCxx` is the fix: clang++ on a Mac, g++ on the Linux box, by name rather
than through the generic `c++` alias, because the console says which compiler
ran and "c++" there tells the reader less than the machine already knows.

**It expected the refusal for a mixed group to be worth keeping.** It is not: a
group under `auto` holding both languages is split, one part per language.
Making somebody split the group by hand first would be a worse answer than the
one the compilers already give.

**It was right about Shalimar, and for roughly the right reason.** The honest
answer is that a Shalimar group is a whole program. The evidence is in
`../Compiler-S/docs/LINKING.md` and checked by `Compiler-S/tests/linking.sh`;
briefly, every unit exports the same three startup symbols whatever file it
came from, the runtime archive owns `main`, and the language has no
declarations for a call across a link to be checked against. The first two
could be fixed. The third is the language.

## The two things that cost the most

**No compiler here takes an object as an input.** Hand cc1 a `.o` and it reads
it as C, and complains about a stray byte on line 1. So the editor names the
linker itself - `link` on Windows with the CRT spelled out, because cc1's
objects carry no `/DEFAULTLIB` directive; `cc` or the C++ driver elsewhere,
read from the same `CC1_CC`, `CC1_LD` and `CXX`, because a machine where cc1
has to be told is a machine where this does too.

**A quoted directory ending in a backslash is one argument, not two.** `/Fo`
wants a separator on the end, and a backslash immediately before a closing
quote escapes it - the C runtime's own rule is that 2n backslashes then a quote
is n backslashes and a delimiter. So cl swallows the source filename and
answers `D8003: missing source filename`, which reads as a command with no file
in it. `targetRecipe` had the same shape, so a cl project build on Windows had
been broken for as long as it existed and nothing said so.

## What stopped being a limit

**shc did not run on Windows**, so the editor there had no compiler for a
`.shl` at all and every Shalimar case skipped itself. Compiler-S grew an MSVC
build and an ml64/link path in its driver on 2026-08-22, and the Windows suites
went from 692 unit / 164 session to 704 / 189 - all of that difference being
Shalimar cases that had never run.

Closing it turned up a fault in this suite rather than in either compiler.
`tests/test.cpp` handed `std::system` a command with a quoted program *and*
quoted arguments, and `cmd /c` strips the first and last quote when there is
more than one pair - so the command reached the shell as garbage, ran nothing,
and reported "shc did not build it" with an empty log. `src/compile.cpp` has
had the fix and the explanation since it was written; the suite did not, and
the case had no way to say so because it was throwing the compiler's output
away. It captures it now.

## What is still a limit

**Debug information does not mix.** cl writes CodeView, cc1 writes DWARF on two
targets and nothing on the third, shc writes none anywhere by decision. A
program linked from two compilers has a debugger that can see part of it. The
editor starts the first debugger any part has and names the groups it will not
be able to stop in, in the console, before the build starts - which is the
honest thing, but it is not the same as debugging the whole program.

**One flat list still cannot say "these files on Linux, those on Windows".**
A group per platform and a `build` entry naming the one you are on is still the
answer, and it is still why this repository has no `build` entry of its own.
