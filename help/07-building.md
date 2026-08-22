# 7. Building

## Two commands, and neither guesses which you meant

**`Ctrl-B` compiles the file in the edit view; `F5` runs it.** Neither asks the
project anything. A project does not have to be open, does not have to be shut,
and does not have to hold the file — open something from anywhere and `Ctrl-B`
compiles that. This is the fix-one, build-again rhythm the editor is built
around.

**`F4` builds the project; `Build ▸ Run project` builds and runs it.** These
read the file list and never the edit view. What they build is what `"build"`
says.

Which one you meant is said by which one you pressed. There is nothing here
that has to be guessed at.

## What a build shows you

The Console tab gets the command and everything the compiler said. A build of
several groups says each one as it starts:

```
$ cc1 and clang++ 3 sources -o three
    src/main.c
    src/legacy.c
    engine/engine.cpp
$ Sources (cc1)
$ Legacy (clang++)
$ Engine (clang++)
$ linking with clang++
[built /home/you/three/three]
```

**An error in a file nothing has opened opens it.** cc1 stops at the first one,
and in a build of six files that is usually not the file you were looking at,
so the editor opens the one it named before putting the caret on the line and
column. With several groups the diagnostic is looked for among *that group's*
sources, so the caret lands in the file the compiler was complaining about
rather than in whichever file the target happened to list first.

## Targets

`Ctrl-T` moves to the next target; the Target menu names all three.

| | |
| --- | --- |
| `x86_64-windows` | MASM, assembled by `ml64` |
| `x86_64-linux` | GNU assembly |
| `arm64-darwin` | this Mac's own |

**Only the host's own target reaches a program.** cc1 and shc generate for all
three, but the assembler and linker they hand off to are this machine's, so a
cross target stops at the assembly — which is shown in the Assembly tab. The
editor says so rather than failing obscurely.

`cl`, `clang++` and `g++` take no target from this editor at all: they generate
for the machine they were installed on, so what they build is what this machine
runs whatever the Target menu says.

## Debug and release

`Ctrl-D` toggles between them. What each compiler actually does about it
differs, and the editor says which rather than pretending they are the same:

| | debug | release |
| --- | --- | --- |
| `cl` | `/Od /Zi /D_DEBUG` | `/O2 /DNDEBUG` |
| `clang++`, `g++` | `-g -D_DEBUG=1` | `-O2 -DNDEBUG=1` |
| `cc1` | `-g -D_DEBUG=1`, and the define alone where there is no line table | `-DNDEBUG=1` |
| `shc` | `--debug` | nothing |

**cc1 has no optimiser**, so release for it is the define and nothing else. The
status bar says `- cc1 has no -O` when you switch, rather than letting you
believe otherwise.

**`shc --debug` does not change the code.** The assembly is byte-identical
between the two; what changes is which runtime archive is linked, and only the
debug one has any code in it for stopping the program.

## More than one compiler in one program

Where a target's groups do not all go to the same compiler, each group compiles
to objects and the editor links them itself — because no compiler here takes an
object as an input. Hand cc1 a `.o` and it reads it as C.

The objects go in a directory of the editor's own and are removed with it,
whether the link worked or not. What survives is the program.

On Windows the linker is `link` with the C runtime named, because cc1's objects
carry no `/DEFAULTLIB` directive to say which one; `cl` is given `/MT` there to
match. Everywhere else it is the same host driver cc1 hands its own linking to.
