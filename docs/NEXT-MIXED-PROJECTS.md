# Next: a compiler per group

**Nothing below is built.** This is where the work stopped on 2026-08-22 and
what the next person should read first. Written while it was fresh rather
than reconstructed later.

## What is asked for

Two shapes, and the second is the first with one more language in it:

1. **A C and C++ project together.** Today a target holding both is refused —
   *"this project holds both C and C++, which cannot make one program"* —
   because cc1 compiles the C, `cl` compiles the C++, and there is no one
   compiler to hand a program halfway between them to. But there does not
   have to be: each group could name its own compiler, and the linker takes
   the objects at the end.

2. **Shalimar beside C and C++.** The same, with `shc` as a third. The
   assembler and linker are common to all of them at the end.

So: **the compiler moves from being a property of the project to being a
property of a group**, and the groups gain the granularity to make that
worth saying.

## Where it lands

The shape is already most of the way there, which is the encouraging part.

`Project` has groups and a `build` entry naming which of them make the target
(`src/project.h`). What it does not have is a compiler per group — the
language of a target is worked out from the suffixes it holds and refused
where they disagree (`Project::targetSources`). That refusal is the thing to
replace.

`Toolchain`/`ToolchainKind` (`src/toolchain.h`) is already a value rather than
a global: `resolve(tool, lang)` answers per file. Nothing there assumes one
compiler per build. `assemblyRecipe` and `programRecipe` already take a kind.

What is missing is a step between them: something that takes a target, splits
its groups by compiler, compiles each group to objects, and links the lot.
Today `targetRecipe` builds a program in one command per compiler, which is
exactly what has to stop.

## The order I would do it in

1. **A compiler per group in the project file.** `"groups": { "Sources": {
   "files": [...], "toolchain": "cc1" } }` beside the plain array that is
   there now, so every existing `ed1.json` keeps working. `Project` gains
   `toolchainFor(group)`, defaulting to by-language as now.

2. **Compile to objects, then link.** `targetRecipe` becomes two things: a
   recipe per group producing objects, and one link command. `-c` and `/c`
   already exist in both recipes; what does not exist is the link step as
   something the editor names on its own. This is the real work.

3. **Only then, more than one language in a target.** With 1 and 2 in place
   the refusal in `Project::targetSources` becomes a check that the *object
   formats* agree, which is a different and much smaller question.

4. **Shalimar in the mixture, last.** It is the one with a genuine
   restriction rather than a mere inconvenience — see below.

## What to settle before writing any of it

**Shalimar produces one program, not an object to link.** `shc` compiles,
assembles and links in one step, and the language has no separate compilation
at all: the cross-file rule in `Compiler-S/docs/CROSSFILE.md` moves other
files' *functions into the program* before checking, so the whole program is
typed together. For Shalimar to be one group among several, `shc -c` has to
mean something, and then a Shalimar function has to be callable from C — which
means deciding how a Shalimar `real[]` looks to a C caller, and whether it can
be called at all. **That is a language question, not an editor one.** It may
be that the honest answer is that a Shalimar group is a whole program and
cannot be linked with C, in which case shape 2 above is really "a project that
builds several programs" rather than one.

**Which ABI wins on Windows.** cc1's MASM output and `cl`'s objects already
link (the Compiler-C notes say so). `shc` emits MASM there too, so this is
probably free — but it is worth checking before it is assumed.

**Debug information does not mix.** cl writes CodeView, cc1 writes DWARF on
two targets and nothing on the third, and shc writes none by decision. A
program linked from all three has a debugger that can see parts of it. The
editor already says which compilers can carry what (`emitsDebugInfo`,
`debugNote`); it would have to say it per group.

## What not to do

**Do not make it work by having one compiler swallow another's language.**
`cl` will compile C, and it would be easy to make a mixed project by sending
everything to it. That throws away the reason cc1 exists.

**Do not concatenate Shalimar files to fake separate compilation.** It
renumbers every line, and every diagnostic — compile time and run time — then
points at a file nobody is looking at. `Compiler-S/docs/CROSSFILE.md` explains
why the pull-what-is-wanted rule was chosen over this.
