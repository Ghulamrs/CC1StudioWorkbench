# 5. Finding

## The keys

| | |
| --- | --- |
| `Ctrl-F` | find — type the text and press enter |
| `Ctrl-G` | find the next one |
| `Edit ▸ Find previous` | find the one before |
| `Ctrl-R` | replace — asks for the text, then for what to put there |

Find starts from the caret and wraps at the end of the file, telling you when
it has wrapped. Not finding anything says so on the message line and leaves the
caret where it was; it does not move you somewhere arbitrary and make you find
your way back.

## What counts as a match

Plain text, matched literally. There are no regular expressions and no
wildcards: what you type is what is looked for, including spaces.

**Matching is by character, not by byte**, so a search for a word with an
accent in it finds the word rather than half of it, and a match never lands the
caret in the middle of a character.

## Replacing

`Ctrl-R` asks twice — what to find, then what to put in its place — and then
works through the file from the caret. Each match is shown before it is
changed, so you can take it or leave it.

**Replace is one undo.** A replacement that went through fifty matches comes
back with one `Ctrl-Z`, because the undo stack holds edits and not keystrokes.

## Finding across the project

There is none, and that is a real limit rather than an oversight. Find works in
the file in front of you. To search a project, use the tool your machine
already has — `grep -rn`, `rg` — which does it better than an editor would and
does not need teaching about your directory layout.

What the editor *does* know about the project is which files it holds, so the
project pane and `F2` / `F3` are how you get between them quickly once you know
where you are going.
