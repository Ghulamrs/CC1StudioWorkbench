# 6. The project

A project is one file, `ed1.json`, and there does not have to be one — without
it the pane on the left shows the directory, as it always did.

```json
{
  "name": "mixed",
  "toolchain": "auto",
  "config": "debug",
  "arch": "arm64-darwin",
  "indent": 4,
  "tabs": false,

  "groups": {
    "Sources": ["src/main.c", "src/util.c"],
    "Legacy":  { "files": ["src/old.c"], "toolchain": "c++" },
    "Engine":  ["src/engine.cpp"]
  },

  "build": { "target": "mixed", "groups": ["Sources", "Legacy", "Engine"] }
}
```

Seven keys, flat except the groups, and every one has a default — so `{}` is a
valid project file. Comments with `//` are allowed, because a file people edit
by hand is a file people leave notes in.

## Groups

A group is the project's own arrangement and has nothing to do with
directories: moving a file between groups changes two lists and nothing on
disk. The Project menu makes files, renames them, moves them between groups and
deletes them — the last asks you to type `yes`, because it is the only thing
here that cannot be undone.

**A group is a list of files, or an object that also names a compiler.** The
plain list is not deprecated: a group with nothing to say is written back as a
list, so adding a file to a project written before any of this leaves the file
looking the way its author left it.

## `"build"` — what the project makes

```json
"build": { "target": "mixed", "groups": ["Sources", "Legacy", "Engine"] }
```

- **`target`** is the program's name, without `.exe`. It lands beside
  `ed1.json`, so it is still there when the editor is not.
- **`groups`** is which groups go into it — deliberately not all of them, so a
  project's own tests, examples, headers and notes stay out of its program.

It also **sets the order**: groups are compiled in the order this list names
them, and that is the order the objects reach the linker. For Shalimar it
additionally **picks the program**, since every `.shm` has a `main()` and
nothing inside a file can say it is the one being built.

Saying nothing is not an error. It means the project builds nothing, and
`Ctrl-B` still compiles the file in front of you.

## A compiler per group

**C is the only language with a decision in it.** C++ goes to the machine's C++
compiler — `cl` on Windows, `clang++` on a Mac, `g++` on the Linux box — and
there is nothing to choose. Shalimar goes to `shc`, the only thing that reads
it. C is the one two compilers can both take: **cc1**, which this editor was
written for and which is the default, and the host's.

So a group naming a compiler is, in practice, always a group of C saying it
wants the other one. That is why `Legacy` above is the only group with a
`"toolchain"` in it, and why the C++ group needs none.

The words are `cc1`, `cl` (or `msvc`), `shc`, `c++`, and `auto`. `"c++"` means
*this machine's* C++ compiler rather than g++ specifically — which one that is
is a fact about a machine, and a project file does not get to have an opinion
about it. For the same reason the *paths* to the compilers are not in here
either; they come from `--cc1`, `--cl`, `--cxx`, `$CC1`, `$CXX`, or PATH.

**A group under `auto` holding two languages is split**, one part per language,
rather than refused. A group that names a compiler is one part and that
compiler takes all of it — which is the only way to make `cl` compile C as C++
on purpose.

## Two limits worth knowing

**Shalimar cannot share a target with C or C++.** In one group, because no
compiler takes both. In a group of its own beside them, because of what a
Shalimar object is — see [the Shalimar page](shalimar.md).

**One flat list cannot say "these files on Linux, those on Windows".** This
project's own sources are the example: `terminal.cpp` and `terminal_win.cpp`
are never built together. A project that differs by platform wants a group per
platform and a `build` entry naming the one you are on.

## Where a file may sit

The root, or one directory under it, and no deeper. As many directories as you
like may sit side by side — `src`, `tests`, `examples`, `docs` — but none of
them holds another. It is a rule the project keeps rather than a habit anyone
is asked to remember, because a structure nobody has to explore is one anyone
can read at a glance.
