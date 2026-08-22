# 3. The screen

```
 File   Edit   Project   Build   Debug   Target   Language   Tools   Help
┌──────────────────────┬─ main.c ─ sum.c ──────────────────────────────────┐
│- Sources             │  1 #include <stdio.h>                             │
│    src/main.c        │  2                                                │
│    src/sum.c         │  3 int main(void)                                 │
│- Headers             │  4 {                                              │
│    src/sum.h         │  5     printf("answer %d\n", addUp(2, 40));       │
│                      │  6     return 0;                                  │
│                      │  7 }                                              │
├──────────────────────┴─ Console ─ Debug ─ Assembly ───────────── 8 lines ─┤
│$ cc1 2 sources -o sums                                                    │
│[built /home/you/work/sums]                                                │
└───────────────────────────────────────────────────────────────────────────┘
 main.c  7 lines            C  debug  cc1* arm64-darwin  5/7  col 5  [text]
```

## The four regions

**The menu bar**, across the top. `F10` opens it; left and right move between
columns, up and down between items, enter chooses, escape leaves. The menu
**reopens on the column it was left on**, which is worth knowing when you are
driving it by muscle memory.

**A dot marks where you already are.** Language, Tools, Target, debug and
release, and the switches on the Edit menu are all states rather than commands,
so the item naming the one you are in carries a `•` (a `*` under `--plain`):

```
┌─────────────────────┐
│• By language  Ctrl-K │
│  cc1                 │
│  shc                 │
└─────────────────────┘
```

The status bar carries some of the same news and not all of it. `cc1*` says the
language chose that compiler and `cc1` says you did — but the language field
reads `C` for a `.c` file whether that came from its name or from your picking
`Language ▸ C` by hand, and the menu is the only place that distinguishes them.

**The project pane**, down the left. The groups of the project and the files in
them — the project's own arrangement, not the directory's. `Ctrl-P` shows and
hides it, enter opens the file under the cursor.

**The edit view**, in the middle. Its tab strip names the open files; the
current one is highlighted. `F3` and `F2` move to the next and previous file.

**The panel**, along the bottom, with three tabs — Console, Debug, Assembly.
`Ctrl-E` shows and hides it, and left and right change tab when the cursor is
in it. Page 9 is about what each tab holds.

`Ctrl-W` moves the cursor between the three: text, project pane, panel. The
status bar's last field says where it is.

## The status bar

```
 main.c  7 lines            C  debug  cc1* arm64-darwin  5/7  col 5  [text]
```

| field | means |
| --- | --- |
| `main.c` | the file, with a `*` after it when it has unsaved changes |
| `7 lines` | how long it is |
| `C` | the language, which decides the colouring and the compiler |
| `debug` | the configuration — `Ctrl-D` toggles it with `release` |
| `cc1*` | the compiler; the `*` means it was chosen by language, not by you |
| `arm64-darwin` | the target — `Ctrl-T` moves to the next one |
| `5/7` | the line the caret is on, of how many |
| `col 5` | the column, counted in characters and not in bytes |
| `[text]` | which region has the cursor: `text`, `tree` or `panel` |

**The `*` on the compiler is the useful one.** Without it, somebody named that
compiler by hand — with `Ctrl-K`, the Tools menu, `--toolchain`, or a group in
`ed1.json` — and the file's own language is not deciding.

## The message line

Under the status bar, one line wide. It says what just happened: what was
saved, what was built, where the program stopped, or why something was
refused. **It is one line and it clips**, so anything that needs more room says
the short version here and the rest in the Console tab.

## Drawing

The frame uses box-drawing characters. Some consoles take those from a second
font and break the lines at every join; `Edit ▸ Plain frame` (or `--plain`)
draws it with `-`, `|` and `+` instead.
