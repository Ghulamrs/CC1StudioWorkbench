# 2. Getting started

## Running it

```
ed1                          open the project you were last in
ed1 file.c                   open a file
ed1 --project some/dir       open a directory as the project
```

On Windows the terminal editor is `winconsole.exe` and the window is
`ed1gui.exe`; both take the same arguments.

**With nothing named it opens the project it was last in**, and failing that
it makes one. The first file of the project is opened too, so you arrive
looking at code rather than at an empty buffer.

## The first run

If there is no project to return to, RStudio writes `~/cc1-demo` — one C
program with a loop and a call in it, which is enough to set a breakpoint and
stop inside. The demo is projectile motion, and it is deliberately small: it
exists so that `F8` does something on a machine where nothing has been written
yet.

## Opening a directory that has no project

A directory with no `ed1.json` gets one written from what is already in it, in
two groups — **Headers** and **Sources**. Nothing is moved on disk; the file
just describes what is there.

**A project file that will not parse is never written over.** That is
somebody's work, and an editor that silently replaced it with its own guess
would be the worst kind of helpful. You get told, and the pane shows the
directory instead.

## Where things land

| | |
| --- | --- |
| the project's program | beside `ed1.json`, named by the target |
| a single file's program | a temporary, removed when you are done with it |
| object files | a temporary directory of the editor's own, cleared after |
| the assembly | shown in the Assembly tab, not left on disk |

The distinction that matters: **the project's program is the project's** and
stays where it was built, so it is still there when the editor is not. A
single file's is a scratch thing the editor made in order to run it.

## Two configuration files, two different questions

- **`ed1.json`**, in a directory, says what *that project* is: its name, its
  groups, what it builds, how it indents.
- **`~/.ed1config.json`** says what *this machine* had: the last project you
  were in, and whatever else earns a place.

The second could not be folded into the first. Every `ed1.json` on disk would
then claim to have been the most recent one.

## Getting help without leaving

- **`F1`** — the keys, on one screen.
- **`Help ▸ Contents`** — these pages, a line each, in the panel.
- **`Help ▸ About`** — the name, the version, and who wrote it.
