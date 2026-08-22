# 9. The panel

Three tabs along the bottom. `Ctrl-E` shows and hides the panel; with the
cursor in it, left and right change tab.

## Console

The command, everything the compiler said, and the program's own output when
you ran it. **Enter on a line the compiler named goes to that line**, which is
the fastest way through a build with several errors in it.

A build that runs more than one compiler says each group as it starts, and
names the linker when it links. The panel holds nine rows, so a long build
scrolls — what you are reading is the end of it, and the beginning is above.

## Debug

Where the program is standing, what is in scope, and how it got there:

```
stopped at sum.c:3 in addUp

  a = 2      [int]
  b = 40     [int]

watching
  a + b = 42

called from
  main   sum.c:11

F8 carries on   F7 steps over   F6 steps into   F9 sets a breakpoint
Ctrl-W puts the cursor in the panel; Enter on a variable sets it
```

- **Enter on a variable** asks for a new value and writes it back. What the
  debugger says about a value it will not take is what the message line says.
- **Enter on a frame** goes to that frame: its variables replace the ones shown
  and the caret goes to the line waiting for the call. The tab says whose
  variables it is showing, since the line at the top still names where the
  program stopped.
- **Enter on the top line** comes back down to the stop.
- **A watch is read again wherever the program gets to.** The list belongs to
  the debugger rather than to either front end, so both windows agree about it.

When a build has produced no program, this tab shows what the build *did*
produce by way of debug information, and says plainly when there is none and
why — cc1's MASM on Windows carries no line table, and shc writes none anywhere
by decision.

## Assembly

What the compiler emitted, read back out of its own output and coloured. This
is the tab that makes a cross target useful: building for a machine you are not
on stops at the assembly, and this is where the assembly is.

It is also how you check what a compiler did without leaving the editor —
`Ctrl-B`, then right to Assembly.

## Reaching them from the menu

`Build ▸ Console`, `Build ▸ Debug` and `Build ▸ Assembly` show each tab
directly, for when the panel is closed or on another tab.
