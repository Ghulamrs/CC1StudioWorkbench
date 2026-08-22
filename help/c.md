# C

C goes to **cc1**, the compiler this editor was written for. It is the only
language with a real choice in it — the host's C compiler will take C too — so
C is the one a group ever names a compiler for.

| | |
| --- | --- |
| suffix | `.c` (and `.h`, which is C by name and still not a source) |
| compiler | `cc1` by default; `cl` or the host's `c++` when a group says so |
| targets | `x86_64-windows`, `x86_64-linux`, `arm64-darwin` |
| debug | `-g -D_DEBUG=1`, and the define alone where there is no line table |
| release | `-DNDEBUG=1` — cc1 has no optimiser |

## Where cc1 is found

`--cc1`, then `$CC1`, then a `cc1` beside the editor, then PATH. Naming one
that is not there is worse than naming none — every case that needs it fails
and none of them says why — so the editor drops it with a word and carries on
as if nothing had been named.

## The three targets, and what reaches a program

cc1 generates for all three. Only the host's own reaches a program, because the
assembler and linker it hands off to are this machine's; a cross target stops
at the assembly, which the Assembly tab shows.

On Windows cc1 writes **MASM**, assembled by `ml64` and linked by `link`. Both
ship with Visual Studio and reach PATH only after `vcvars64.bat` has run — the
editor arranges that for itself, which is why it works from an ordinary
console.

## Debugging

cc1 writes **DWARF** for `x86_64-linux` and `arm64-darwin` — line tables,
types, objects and lexical blocks — and gdb and lldb both read it. So a debug
build stops on a line, steps, and shows variables.

`x86_64-windows` is where that stops. cc1 generates MASM there, MASM carries no
line table, and the assembler cannot spell the relocations CodeView would want.
Six `ml64` spellings were tried and refused. The editor says so plainly rather
than starting a debugger that cannot work: *"cc1 generates MASM for
x86_64-windows, which carries no line table"*.

**A Mac has one wrinkle.** cc1's `arm64-darwin` objects carry no `__eh_frame`
and no `__compact_unwind`, so lldb reconstructs the frame by reading
instructions — and cc1 uses the stack pointer as a scratch stack inside a
function body. lldb therefore ends a step early and reports the same line two
or three times. The editor repeats a step until it has been somewhere; see
[page 8](08-debugging.md). `finish` under lldb fails outright on those objects
for the same missing information.

## Sending C somewhere else

A group of C can name `cl` or `c++` instead:

```json
"Legacy": { "files": ["src/old.c"], "toolchain": "c++" }
```

Reasons to: you want an optimiser, or you want debug information on Windows.
Reasons not to: cc1 is what this editor exists for, and it is much faster — on
423 files of C it beats `gcc -O0` by a factor of about 57, and produces the
same bytes every time.

`"toolchain": "cl"` compiles `.c` as C++ under `/TP`. That is deliberate and it
is the only way to ask for it; it is not what `auto` will ever do on its own.
