# 8. Debugging

## The keys

| | |
| --- | --- |
| `F9` | set or clear a breakpoint on this line |
| `F8` | start, or carry on from where it stopped |
| `F7` | step over |
| `F6` | step into |
| `Debug ▸ Step out` | run until this call returns |
| `Ctrl-Up` / `Ctrl-Down` | up the call stack, and back down |
| `Debug ▸ Watch expression...` | keep asking about an expression |
| `Debug ▸ Stop debugging` | end the session |

**`F8` debugs the file in front of you; `Debug ▸ Debug project` debugs the
program the project builds.** The same two things `Ctrl-B` and `F4` choose
between, asked the same way.

A breakpoint is the editor's own note and needs no compiler: you can set one,
see it in the gutter, and take it away with nothing installed at all. It is
filed under the file rather than the buffer, so it survives the file being
closed and opened, and it follows the file when you rename it.

## Where it stopped

The gutter marks the line with `>`; a breakpoint is `*`, and the arrow wins
when both are on the same line. **The file it stopped in is opened if it was
not already** — stepping out of one file of a project into another used to say
"stopped at main.c:13" while showing `circle.c`, which is a stranger lie than
the one being avoided.

The Debug tab shows the line, the function, what is in scope, and who is
waiting for it. `Ctrl-W` twice puts the cursor in the panel; enter on a
variable sets it, enter on a frame goes to that frame, and enter on the top
line comes back to the stop.

## What can be debugged, and what cannot

| | |
| --- | --- |
| cc1 on `arm64-darwin`, `x86_64-linux` | lldb or gdb, reading cc1's own DWARF |
| cc1 on `x86_64-windows` | **no** — MASM carries no line table |
| `cl` | cdb, reading CodeView from the `.pdb` |
| `clang++`, `g++` | lldb or gdb |
| `shc` | the program stops **itself** — no debugger at all |

**Debug information does not mix.** A program linked from two compilers has a
debugger that can see part of it. The editor starts the first debugger any part
has and names the groups it will not be able to stop in, in the console, before
the build starts — rather than letting you find out by pressing `F8`.

**Release cannot be debugged**, and the message says the true reason for the
compiler you are using: `-g` for cc1, and for shc that release links a runtime
with no debugger in it.

## Shalimar is different, and it is not a lesser version

A Shalimar program **stops itself**. The compiler already emits
`shm_line(unit, line)` before every statement so a runtime error can name where
it happened, and a debug build offers that same position to a session inside
the program. There is no debug format, nothing to install, and no gdb, lldb or
cdb involved.

What that buys: statement granularity rather than an approximation, and the
same behaviour on all three targets — including `x86_64-windows`, where cc1's
own debugging stops.

What it cannot do is **read a variable**. The compiler emits no table of a
function's names against its frame slots, so the Debug tab says
*"a Shalimar program says where it is, not what is in it"* rather than showing
an empty list. Watches and walking the stack refuse in the same voice, and the
tab does not offer keys for them.

## A step that appears to do nothing

On a Mac, stepping used to need three presses of `F7` where Linux needed one.
The reason is underneath both: cc1's `arm64-darwin` objects carry no
`__eh_frame` and no `__compact_unwind`, so lldb works the frame out by reading
instructions — and cc1 uses the stack pointer as a scratch stack inside the
body, so the frame it computes moves mid-function and lldb ends the step early.

The editor now repeats a step until it has actually been somewhere. The test is
narrow on purpose: same file, same line, same function, with the address
further on. Recursion on one line is on that same line too, but its address
goes *back* to the callee's prologue, so it reads as the arrival it is.
