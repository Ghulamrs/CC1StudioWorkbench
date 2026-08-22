# C++

C++ needs no decision. Every machine has exactly one C++ compiler worth
calling, so `auto` routes to it and **a C++ group never needs to name one**.

| | |
| --- | --- |
| suffix | `.cpp`, `.cc`, `.cxx` |
| compiler | `cl` on Windows, `clang++` on a Mac, `g++` on Linux |
| targets | none — it builds for the machine it is on |
| debug | `/Od /Zi /D_DEBUG` for cl; `-g -D_DEBUG=1` otherwise |
| release | `/O2 /DNDEBUG` for cl; `-O2 -DNDEBUG=1` otherwise |

**By name, not as "c++".** The console says which compiler ran — `(clang++)` on
a Mac, `(g++)` on the Linux box, `(cl)` on Windows — because which one it is
*is* the information, and the generic alias tells a reader less than the
machine already knows. `--cxx` or `$CXX` names another; a project file never
does, because which C++ compiler a machine has is a fact about the machine.

> This used to route to `cl` on every machine, which meant a C++ file on a Mac
> was sent to a compiler that is not installed there and never could be. A
> project of C and C++ could therefore only ever have been built on Windows.

## Finding cl

`--cl`, `$CL`, or Visual Studio 2022 itself — the editor imports the
environment a Developer Command Prompt would have, so it works from an ordinary
console. The search is pinned to 2022; a bare "latest" would reach past it to a
newer Visual Studio, which is not the toolset this is built with.

## Debugging

**cl writes CodeView into a `.pdb`, and `cdb` reads one.** cdb comes with the
Windows SDK's debugging tools and is not installed by default, so the editor
looks for it rather than assuming — and says *"cl writes a .pdb and cdb reads
one, but cdb is not installed"* when it is missing.

`clang++` and `g++` write DWARF and are read by lldb and gdb like anything
else.

So on Windows, C and C++ are in different positions: a `.c` goes to cc1 and
carries no line table, while a `.cpp` on the same machine goes to cl and
carries everything. That is a fact about the two compilers, not about the
machine, which is why the editor asks `debuggerFor(compiler, target)` and never
`debuggerFor(machine)`.

## Beside C in one program

A target may hold both. Each group compiles to objects with its own compiler
and the editor links them — see [page 7](07-building.md).

One thing the editor has to arrange for you on Windows: **cl is given `/MT`**
there, because cc1's own driver links `libcmt` and two C runtimes in one
program is `LNK4098` at best and two heaps at worst. Nothing else is in a
position to make them agree.
