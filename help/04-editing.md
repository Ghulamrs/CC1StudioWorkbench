# 4. Editing

## Laying out as you type

RStudio indents while you type rather than reformatting afterwards. Press
enter and the next line starts where it belongs; type `}` and the line pulls
back to match its `{`.

**`Tab` lays out the line you are on.** `Ctrl-A` lays out the selection, or the
whole file when nothing is selected. Neither changes anything but leading
whitespace.

The rules are brace-directed and were written for Shalimar first, then extended
for C. `case` labels sit in their `switch`'s own column by default; `--case-indent`
puts them one step inside instead.

**Width and tabs** come from the project (`"indent": 4`, `"tabs": false`) and can
be overridden for a run with `--width n` and `--tabs`.

## The one rule that is not decoration

`n : n + 1` is an **assignment** in Shalimar and a **goto label** in C, and a
label is laid out in its function's own column. A Shalimar program indented by
the C rules therefore walks left one statement at a time.

That is why `IndentDialect` exists, and why the Language menu is not cosmetic —
it decides how your file is laid out as well as how it is coloured.

## Text is characters, not bytes

The editor counts in characters throughout: the column in the status bar,
`Home` and `End`, arrow keys, selection, and how a line is wrapped in the
panel. A file of UTF-8 with accents, Arabic or Urdu in it behaves the way a
reader expects and not the way a byte counter does.

## Undo and redo

`Ctrl-Z` undoes, `Ctrl-Y` redoes. The stack holds edits, not keystrokes, so
undoing a paste takes one press.

**The editor knows when the file matches the disk.** Saving puts a marker on
the undo stack; undoing back past it clears the `*` from the status bar, and
redoing forward brings it back. That means "have I actually changed anything?"
is answered by the truth rather than by a flag that was set once.

## Selecting, and the clipboard

Hold shift with the arrows, `Home`, `End`, `PageUp` or `PageDown` to select.
`Ctrl-C` copies, `Ctrl-X` cuts, `Ctrl-V` pastes, and `Edit ▸ Select all` takes
the file.

The clipboard is the editor's own. It is not the system clipboard, and text
does not travel between RStudio and other applications through it — use the
terminal's own copy and paste for that.

## Files

`Ctrl-S` saves. `File ▸ Save As...` saves under a new name and follows it.
`File ▸ New` gives a blank buffer with no name; the first save asks for one.

**Leaving with unsaved work asks.** `Ctrl-Q` with changes outstanding offers
to save them, and nothing is lost by pressing it absent-mindedly.
