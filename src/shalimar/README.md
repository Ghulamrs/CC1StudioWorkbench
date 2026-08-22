# The Shalimar half

Everything in this directory is about one language and touches nothing that
serves the other two. `src/debugger.cpp` drives gdb, lldb and cdb, and has
nothing to say to any of this; none of this has anything to say to it.

That is not tidiness. A Shalimar program **stops itself**. The compiler emits
`shm_line(unit, line)` before every statement so that a runtime error can name
where it happened, and a debug build offers that same position to a session
inside the program. There is no debug format to read, no `.pdb`, no DWARF
unit, and no debugger to find or install — so there is nothing here that the
three-debuggers file could have been extended to do.

| | |
| --- | --- |
| `channel.h/.cpp` | a child with its three streams kept **apart** |
| `session.h/.cpp` | the protocol, in the editor's own vocabulary |

**Why not `editor::Process`.** That one joins a child's error output to its
ordinary output on purpose — a debugger says useful things on both and the
editor shows one console. Here it is the opposite: the session talks on
standard error and the program prints on standard output, and joining them
would put a `#stop` in the middle of a line the program was half way through
writing. So `Channel` keeps them separate, and is otherwise smaller than
`Process`: no console variant, and no marker discipline, because a protocol
line is a line and arrives whole.

**What is borrowed.** `editor::Stop` and `editor::StackFrame`, from
`debugger.h`. "Where the program is" is one idea and the editor draws it one
way; duplicating the words would be worse than including the header. Borrowing
vocabulary is not sharing machinery.

The protocol itself, and the debug-versus-release boundary it depends on, are
in `../../../Compiler-S/docs/DEBUGGING.md`.

## Where this stands

Built, linked into `ed1`, and driven against a real program by
`steppingShalimar()` in `tests/test.cpp`: a breakpoint by file and line, a
stop, a step in, a step out, the program's own printing coming back with it,
and a release build refusing to be stopped because it has no code for it.

**Not yet wired to the Debug menu.** The editor still routes every `Debug ▸
Start` to `debugger_`. The routing is a handful of lines in `editor.cpp` and
is the next thing; it was left undone rather than done in a hurry.
