<!-- Everything below the rule is a verbatim copy. Do not edit it here. -->

# Appendix A — The Shalimar Language

**This is a copy, not the original.** The specification lives in the Shalimar
app's own repository as `SHALIMAR_LANGUAGE.md`, and that copy is the one that
wins: where this page and that one disagree, that one is right and this one is
stale.

It is carried here because RStudio is a separate repository. A reader of this
manual has no `../Shalimar` to follow, and a language reference you cannot
reach is not a reference.

| | |
| --- | --- |
| taken from | `Shalimar/SHALIMAR_LANGUAGE.md` |
| at commit | `8dd4232`, 2026-08-12 — the one that last touched it |
| checked by | `tools/check-help.sh`, which diffs this against the original |

**Run `tools/check-help.sh` after touching either.** It is the whole reason
copying this was acceptable: without it, two specifications exist and nothing
says which one somebody read. The house bug in these projects is documentation
that outlived the fact, and a duplicated spec is that bug with a head start.

Everything the *compiler* does differently from the app's interpreter is not
here — it is in `Compiler-S/docs/CONFORMANCE.md`, and the document below is
what both are measured against.

---

# The Shalimar Language

A developer reference for **Shalimar**, the small numeric language interpreted by
`TokenKind.swift` / `Node.swift` / `Parse.swift` / `Check.swift` / `Interpreter.swift` in this
project (Xcode project `Shalimar`, target `Shalimar`). This document is the authoritative
specification: when the interpreter and this document disagree, that is a conformance bug in the
interpreter, not a documentation error — fix the code, or deliberately renegotiate and update this
file, but don't let them silently drift.

This file both teaches the language to someone writing `.shm` programs, and documents the
interpreter's actual internals for whoever maintains it. Sections marked **Implementation note** are
for the latter audience and describe real, verified behavior of the current code — including a few
sharp edges worth knowing before you rely on them.

**This is the 3.0 language.** 2.x had one runtime type (`Double`), untyped parameters, output
*names* in the `<>` list, no arrays, and strings that degraded to `0.0`. All of that is gone. Where
a rule below replaced a 2.x one and the difference is likely to bite someone porting a program, it
says so.

The same material, written for the person using the app rather than maintaining it, is in
`HelpViewController.swift` and reachable from the `?` in the editor's nav bar. When a rule changes,
both files change.

---

## 1. Overview

- A program is a sequence of function definitions and global declarations; execution begins at
  `main()`, which takes no inputs. Definitions may be written in any order, but a global is visible
  only below the line that declares it ([§6](#6-declarations)).
- Four types: `int`, `real`, `char`, and arrays of them. Text is `char[]`. See [§5](#5-types).
- Variables are declared with a type. A *scalar* may also be created by assigning to it; an array
  may not, unless the right-hand side is a literal ([§7.1](#71-assignment)).
- Control flow: `if`/`elseif`/`else`, `while`, two `for` forms, and `break`/`continue`
  inside a loop.
- Functions may return **several** values; the caller captures them with `<a,b> : f(...)`. An array
  cannot be returned — it is passed in and filled, because an array argument is a reference
  ([§8.2](#82-arguments)).
- Output goes through `?` (print with newline) or `??` (print without). Each must be the first thing
  on its line, so there is at most one print command per line. See [§7.8](#78-print).
- Statements have no terminator. The parser uses targeted lookahead to know where one ends and the
  next begins ([§11](#11-statement-boundaries--why-there-are-no-semicolons)). Newlines are
  insignificant *everywhere except* the print rule, which is the one place layout carries meaning.
- There are three stages: lex, parse, and a **checker** that types the whole program before anything
  runs. The checker does not stop at the first problem ([§13](#13-diagnostics)).

### 1.1 A complete example

```
fun <real> = invert(a[][]: real) {
  real tol : 1e-30
  real det : 1.0
  real r : 0.0

  for i < a.row {
    det : det * a[i][i]
    if abs(det) < tol { return det }

    r : 1.0 / a[i][i]
    a[i][i] : 1.0
    for j < a.col {
      a[i][j] : r * a[i][j]
    }

    for k < a.row {
      if k != i & a[k][i] != 0.0 {
        r : a[k][i]
        a[k][i] : 0.0
        for j < a.col {
          a[k][j] -: r * a[i][j]
        }
      }
    }
  }
  return det
}

fun <> = main() {
  real m[3][3] : {{4.0, 7.0, 2.0},
                  {3.0, 6.0, 1.0},
                  {2.0, 5.0, 3.0}}
  ? "inverse, det" invert(m)
  ? m
}
```

---

## 2. Lexical structure

Source is read left to right; whitespace (including newlines) is skipped and otherwise
insignificant. The lexer does count newlines as it discards them and stamps each token with the line
it started on, because one rule needs it: a print command must be the first token on its line
([§7.8](#78-print)). Nothing else in the language consults layout.

### 2.1 Comments

```
Comment ::= "//" { Character } (newline | EOF)
```

`//` may appear at the start of a line or after code on the same line; everything from `//` to the
end of that line is discarded.

**Implementation note:** there is **no block-comment (`/* ... */`) support in the lexer**, despite
`ComputeViewController.swift`'s re-indenting helpers containing logic that recognizes `/* ... */`
when laying out scanned or pasted source. That logic is purely cosmetic and independent of the
grammar. In a real program `/* comment */` is not a comment — `/` lexes as divide, `*` as multiply,
and the words inside as identifiers, producing an error. Don't let the editor's tolerance of `/* */`
fool you.

### 2.2 Identifiers

```
Identifier ::= (Letter | "_") { Letter | Digit | "_" }
Letter     ::= "a".."z" | "A".."Z"
Digit      ::= "0".."9"
```

Case-sensitive. Keywords are recognized case-insensitively at the lexer level (the matched run is
`lowercased()` before the switch) and can never be used as identifiers:

```
if  elseif  else  while  for  to  step  fun  return  break  continue  int  real  char
```

`int`, `real` and `char` are keywords but are also the names of the three conversions, which is
resolved by position — see [§12.1](#121-conversions).

Three further words are **contextual**: they mean something only in one position and remain ordinary
identifiers everywhere else.

| Word | Special only | Ordinary elsewhere |
|---|---|---|
| `row` `col` `dim` | immediately after `.` | `row : 9` declares a variable called `row` |
| `prec` | at the head of a print item, followed by `(` | `prec : 9` declares a variable called `prec` |

The one clash contextual matching cannot resolve is a *function* named `prec`, which the checker
refuses outright rather than leave silently unreachable.

**`Letter` and `Digit` are ASCII only — this is deliberate.** Swift's `isLetter` is true for *every*
Unicode letter, and letting those through is actively dangerous rather than merely permissive:
Cyrillic `х` (U+0445) and Latin `x` (U+0078) are the same glyph on screen but different identifiers,
so `хn : y - 4` assigns to a variable no `xn` in the program will ever read. That is not
hypothetical — it is exactly how a camera-scanned program failed, surfacing as a baffling undefined
variable pointing at a line where the variable was plainly defined. The same trap exists for Greek
`Α`/`Ο`/`Ρ` and for non-ASCII digits.

A non-ASCII character therefore produces a lex error **naming its code point**
([§13.1](#131-lex-errors)), which is the only way to make the difference visible. It is important
that this is an error and not a silent skip: dropping the character would quietly turn `хn` into the
identifier `n` and simply relocate the bug.

The editor also normalizes confusable characters to ASCII on the way in (`asciiConfusables` /
`normalizedToASCII` in `ComputeViewController.swift`) for text that is typed, pasted, or scanned, so
in practice the lexer's check is the backstop rather than the first line of defence — but it is the
authoritative one, since a `.shm` file loaded from disk bypasses the editor entirely. The table
includes the full-stop lookalikes, because since `.row`/`.col`/`.dim(n)` a dot carries syntax
outside numerals too.

### 2.3 Numbers

```
Number   ::= Digit { Digit } [ "." { Digit } ] [ Exponent ]
Exponent ::= ("e" | "E") [ "+" | "-" ] Digit { Digit }
```

**A numeral containing a point or an exponent is a `real`; anything else is an `int`.** `42` is an
int, `42.` and `1e10` and `1.5e-3` are reals. `Digit` is ASCII `0`–`9` only, for the reasons in
[§2.2](#22-identifiers).

An int literal that does not fit `Int32` is a lex error naming the limit and suggesting a real. A
trailing dot is fine: `1.` is `1.0`.

> **2.x note.** Exponent notation did not exist; `1e10` lexed as `1` followed by the identifier
> `e10`. The constant `e` was withheld for a time on the reasoning that the letter now belongs to
> the exponent when it follows a digit; it is back ([§12](#12-built-in-functions-and-constants)),
> because the lexer separates the two cleanly — `2e5` is one number, `? 2 e` is two items, and a
> name like `e5` is untouched. The objection was readability, not ambiguity.

**Implementation note — the scan is looser than the grammar.** The lexer consumes any run of digits
and dots, and an exponent whose digits may be *absent* (`[0-9]*`, not `[0-9]+`). So `1.2.3`, `1e` and
`1e-` all arrive whole at the conversion step, where `Double` refuses them and they are reported as
`Malformed number '...'`. Scanning strictly would leave `1e` to split into the number `1` and an
identifier `e`, desyncing every token after it and surfacing as a parse error pointing somewhere
else. `Double` is what actually arbitrates; the grammar above is the intent.

### 2.4 String literals

```
StringLiteral ::= '"' { Character except '"' or newline } '"'
```

A string literal is a `char[]` holding the text plus a terminating `char(0)`. There are no escapes —
the lexer's pattern is `"[^"\n]*"`, so the first closing quote always ends it, and a literal cannot
contain a `"`. Non-ASCII *inside* a literal is allowed: the ASCII rule of [§2.2](#22-identifiers)
governs identifiers, not text.

**A literal must be closed on the line it opens**, or it is `Unclosed string - add '"'`
([§13.1](#131-lex-errors)), reported on that line. Both halves of that pattern earn their place, and
both were once missing: with the closing quote optional a single stray quote swallowed the rest of
the file, and with the body able to match a newline the opening quote reached forward to the next
quote anywhere below it - turning a program into a string and reporting the failure on a line that
was not wrong.

### 2.5 Operators and punctuation

| Token(s) | Meaning |
|---|---|
| `+` `-` `*` `/` `%` `^` | arithmetic: add, subtract, multiply, divide, modulus, power |
| `:` | assignment (`x : expr`) / separator (`for i : 0 to 10`, parameter types, multi-assign) |
| `=` | **overloaded**, see below |
| `!=` | not-equal |
| `<=` `>=` | less-or-equal, greater-or-equal |
| `<` `>` | **overloaded**, see below |
| `&` `\|` | logical and / or (operate on truthiness, [§5.5](#55-truthiness)) |
| `+:` `-:` | compound assign (`x +: 1` ⇔ `x : x + 1`); `+:` also appends to a string |
| `(` `)` | grouping / argument list / `.dim(n)` axis / `prec(n)` |
| `{` `}` | block delimiters / array literal |
| `[` `]` | array size in a declaration, index in an expression, rank in a parameter |
| `,` | list separator |
| `.` | array dimensions: `.row` `.col` `.dim(n)` |
| `?` | print with trailing newline (only when *not* immediately followed by a second `?`) |
| `??` | print with no trailing newline |
| `!` | **not a token on its own** — only the first half of `!=`; a bare `!` is a lex error |
| `"` | string literal delimiter |

**Implementation note — the dot rule sits below the number rule.** Token patterns are tried in
order, and `[0-9][0-9.]*...` is tried first, so the dot in `1.5` is eaten as part of the numeral and
only a dot that cannot start a number reaches the `.` rule. That is exactly the dot of `A.row`.
Moving the dot rule above the number rule would break every real literal in the language.

**Implementation note — `EndOfInput` is never emitted.** `TokenKind` has the case, but `tokenize()`
never appends one: the array contains only real tokens. End of input is represented *virtually*, by
`peekCurrentToken()` manufacturing one once `index` runs past the end. This is what lets
`consume`/`match` report cleanly at end of input instead of indexing out of bounds. Loops that detect
end of input via `index < tokens.count` (`parseBody` among them) depend on there being no terminator
token.

**`=` is overloaded three ways**, disambiguated purely by grammatical position:
1. Inside an expression, the **equality comparison** (`if d = 0 { ... }`).
2. At the start of a statement, `identifier = expr` is a **fallback assignment**, behaving exactly
   like `identifier : expr`. It is accepted silently.
3. In a function header, `fun <outputs> = name(inputs) { ... }`, the separator between the output
   list and the name.

**`<` / `>` are overloaded three ways**:
1. Inside an expression, less-than / greater-than.
2. As angle brackets around a list: multi-assign's `<a,b> : f(...)` and a definition's output list.
3. Immediately after a `for` loop's name, `for i < n`, the count-loop form ([§7.7](#77-for)).

The parser disambiguates `<` by lookahead (`looksLikeMultiAssignHeader`): wherever it could start a
statement or continue an expression, it peeks for the exact shape `Identifier {"," Identifier} ">"
":"` before committing. The `for` case needs no lookahead — a `<` in that position was a parse error
in every earlier version, so nothing legal changed meaning.

**`>=` and the function header.** Because the lexer is context-free and matches the longest operator
first, `fun <>= main()` — the closing `>` of the output list written hard against the separator `=` —
arrives as a single `>=` token. That spelling parsed before `>=` was an operator, so `parseDefinition`
accepts a `>=` there and reads it as the `>` plus the `=`. The alternative was to require the space
and silently break programs over an operator they do not use. Note that this is a symptom, not a
design: the real cause is `=` doing duty as a separator at all ([§2.5](#25-operators-and-punctuation),
overload 3), and it would not arise if the header used `:` like every other separator in the language.

---

## 3. Grammar (EBNF)

```
Program        ::= { Declaration | FunctionDef }

Declaration    ::= Type Identifier { "[" Expression "]" } [ ":" Initializer ]
Type           ::= "int" | "real" | "char"
Initializer    ::= ArrayLiteral | Expression
ArrayLiteral   ::= "{" [ Slot { "," Slot } ] "}"
Slot           ::= Initializer | (* empty - an omitted entry, see 7.1.1 *)

FunctionDef    ::= "fun" OutputList "=" Identifier "(" [ ParamList ] ")" Block
OutputList     ::= "<" [ Type { "," Type } ] ">"
ParamList      ::= Parameter { "," Parameter }
Parameter      ::= [ "&" ] Identifier { "[" "]" } ":" Type

Block          ::= "{" { Statement } "}"

Statement      ::= Declaration            (* only at the top level of a function body *)
                 | Assignment
                 | CompoundAssign
                 | MultiAssign
                 | ReturnStmt
                 | FunctionCall
                 | IfStmt
                 | WhileStmt
                 | ForStmt
                 | PrintStmt
                 | "break"                (* only inside a loop *)
                 | "continue"             (* only inside a loop *)

LValue         ::= Identifier { "[" Expression "]" }
Assignment     ::= LValue (":" | "=") Initializer
CompoundAssign ::= LValue ("+:" | "-:") Expression
MultiAssign    ::= "<" Identifier { "," Identifier } ">" ":" FunctionCall

ReturnStmt     ::= "return" [ Expression | "(" Expression { "," Expression } ")" ]

IfStmt         ::= "if" Expression Block { "elseif" Expression Block } [ "else" Block ]
WhileStmt      ::= "while" Expression Block
ForStmt        ::= "for" Identifier ":" Expression "to" Expression [ "step" Expression ] Block
                 | "for" Identifier "<" Expression [ "step" Expression ] Block

PrintStmt      ::= ("?" | "??") { PrintItem }      (* must be first token on its line *)
PrintItem      ::= "prec" "(" Expression ")" | Expression

Expression     ::= OrExpr
OrExpr         ::= AndExpr { "|" AndExpr }
AndExpr        ::= CompareExpr { "&" CompareExpr }
CompareExpr    ::= AddExpr { ("=" | "!=" | "<" | ">" | "<=" | ">=") AddExpr }
AddExpr        ::= MulExpr { ("+" | "-") MulExpr }
MulExpr        ::= PowExpr { ("*" | "/" | "%") PowExpr }
PowExpr        ::= Postfix [ "^" PowExpr ]
Postfix        ::= Primary { "[" Expression "]" | "." Attribute }
Attribute      ::= "row" | "col" | "dim" "(" Expression ")"
Primary        ::= Number | StringLiteral | FunctionCall | Identifier
                 | Conversion | "-" Primary | "(" Expression ")"
Conversion     ::= Type "(" Expression ")"
FunctionCall   ::= Identifier "(" [ Expression { "," Expression } ] ")"
```

`{ "[" ... }` and `{ "." Attribute }` share one postfix loop, so they chain in any order:
`M[k].row`, `A.dim(i)`.

---

## 4. Operator precedence

Loosest-binding to tightest:

| Tier | Operators | Associativity |
|---|---|---|
| 1 (loosest) | `\|` | left |
| 2 | `&` | left |
| 3 | `=` `!=` `<` `>` `<=` `>=` | left |
| 4 | `+` `-` | left |
| 5 | `*` `/` `%` | left |
| 6 | `^` | **right** |
| 7 (tightest) | postfix `[...]` and `.row`/`.col`/`.dim(n)`, unary `-`, literals, calls, `(...)` | — |

`^` is right-associative: `2^3^2` is `2^(3^2) = 512`, not `(2^3)^2 = 64`.

### 4.1 Unary minus vs. `^` — a real gotcha

Unary minus folds the negation into its operand immediately and returns it as a single term, before
the caller can see a following `^`. So **when a negated expression is the base of `^`, the negation
applies first**:

```
-2^2   evaluates as (-2)^2  =  4      (not -4, unlike Python's -2**2 == -4)
-a^2   evaluates as (-a)^2
```

On the exponent side it behaves as ordinary maths notation, since the parser recurses
right-associatively there:

```
2^-2   evaluates as 2^(-2)  =  0.25
```

For `-(a^2)`, write the parentheses.

---

## 5. Types

```
int     32-bit signed whole number         (-2147483648 .. 2147483647)
real    64-bit floating point
char    one byte, 0..255
T[]     array of T, any rank
```

`char[]` is text ([§10](#10-strings)). There are no other types, no booleans, no closures, and no
array of strings — `char` is one-dimensional by rule, so `char g[2][4]` is refused.

### 5.1 Where each is written

A type appears in exactly three places: a declaration ([§6](#6-declarations)), a parameter, and a
function's output list. Nowhere else — there is no cast syntax, only the conversion functions of
[§12.1](#121-conversions).

### 5.2 Widening and narrowing

`int` → `real` is **automatic** wherever a real is required: assignment to a declared real, an
argument to a real parameter, an operand beside a real, an element of an array literal being fitted
into a real array.

`real` → `int` is **automatic at a declared destination and silent**:

```
real x : 2.7
int  k : x        // k is 2 - the fraction is dropped, with no diagnostic
```

The truncation is toward zero, so `-2.7` gives `-2`, never `-3`. Both directions go through the same
`ConvertNode` the checker inserts.

> **This silence is the language's sharpest edge.** It is why a quadratic solver written with int
> coefficients returns plausible wrong roots: `d : b^2-4*a*c` makes `d` an int, `d : sqrt(d)` narrows
> the real result back into it, and `/` on two ints is integer division. See item 3 in
> [§15](#15-known-limitations--maintainer-notes).

Mixing an operand of each in an expression widens to `real`. Two ints give an int — **including
division**: `7/2` is `3`, and `7./2.` is `3.5`.

### 5.3 What does not convert

`char` never joins arithmetic. It compares and orders against other chars, and converts on request
via `int(c)` / `char(n)`, but `s[0] + 1` is an error. That wall is what keeps `? c` printing a letter
rather than a number.

Arrays never convert. A reference argument must match its parameter's type exactly.

### 5.4 Value representation

**Implementation note.** A runtime value is a Swift enum with cases `int(Int32)`, `real(Double)`,
`char(UInt8)` and `array(ArrayRef)`. A tagged union is sized by its largest case, so **a `char`
occupies the same 16 bytes as a `real`** — `char[1000]` and `int[1000]` cost exactly the same.
`char` is the element of text, not a small integer; using it to save memory saves nothing. A packed
byte array would be a representation change in `ArrayRef`, not a typing decision.

`ArrayRef` is a class, which is what makes an array a reference ([§8.2](#82-arguments)).

### 5.5 Truthiness

A condition may be any scalar. Zero is false; anything else is true. An array is not a scalar and is
refused as a condition — including a string, so `if "a"` is an error rather than silently true.

---

## 6. Declarations

```
int   n
int   n : 5
real  x : 1.5
char  c
char  name[20] : "Akhtar"
real  v[10]
real  m[3][3] : {{1.,2.},{3.,4.}}
int   cube[2][3][5]
```

**A declaration may appear only at the top level of a function body, or at global scope.** Inside an
`if`, `while` or `for` body it is a parse error naming the variable. The rule keeps every local's
lifetime the whole call, which is what lets the checker type a function in one pass.

Only declarations and `fun` definitions may appear at global scope; a statement there is an error.

**A global is visible below the line that declares it, and nowhere above it.** Functions are not
ordered this way — they are collected in one pass before any body is checked, so `main()` may call
something written after it — but a global cannot be, because the interpreter creates the globals in
file order as well. While the checker treated them as an unordered set and the interpreter as a
sequence, a program the checker had passed could still fail at run time on a name it accepted:

```
int a : f()                       // runs before b exists
fun <int> = f() { return b }
int b : 5
```

That is now `'b' is a global declared later, on line 3`, reported before anything runs. A name that
is not in the file at all still reports as `Undefined variable`; the other message is only for one
that is present, spelled correctly, and further down. One case is still left to the run: a global
whose own initializer calls a function that reads *that* global — a cycle, not an ordering — reports
`Undefined variable` at run time, because the box does not exist until the initializer finishes.

An extent must be an `int` and at least `1`. A size the checker can fold is refused before the
program runs; a size that genuinely depends on a variable is checked at run time and reports the
value it computed, which the static message can afford to omit but the runtime one cannot — the
number is nowhere in the source.

An initializer that is shorter than the declared array fills what it covers and leaves the rest
zero. For a `char` array the last slot is reserved for the terminator, so `char s[4]` holds three
characters and `"abcdefg"` truncates to `abc`.

---

## 7. Statements

### 7.1 Assignment

`x : expr` evaluates `expr` and stores it. `x = expr` is a fallback spelling accepted silently.

A **scalar** may be created by assigning to it; its type is inferred from the expression.

An **array** may be created by assignment only when the right-hand side is a *literal*. The rule
against creation-by-assignment exists because extents cannot be known from an arbitrary expression —
but a literal carries its own shape, so there is nothing left to infer:

```
B : {{1.,2.,3.},{4.,5.,6.}}   // creates a 2x3 real array
C : A                          // Error - "Declare the array 'C' first"
```

**`char[]` is the exception**, and deliberately so: a string carries its own length wherever it
comes from, so there is nothing to infer in any of these cases.

```
s : "hello"        // creates a char[6]
t : s              // creates a copy of s
u : s + " there"   // sized to the result
```

Without this the `+` of [§10](#10-strings) would be close to unusable - every joined string would
need a declaration carrying a capacity guessed before the join.

Assigning an array into a variable that already holds one **copies into the storage already there**
rather than rebinding. Extents are fixed at declaration, and an array may be shared by reference with
a caller — swapping the storage would resize it underneath them. So a short literal fills part of a
larger array rather than shrinking it, and this works:

```
fun <> = fill(M[][]: real) {
  M : {{9.,9.},{9.,9.}}       // fills the caller's array
}
```

**The literal becomes the array's whole new value: every cell the literal does not reach is set to
zero**, not left holding what was there before.

```
real M[3][3] : {{5.,5.,5.},{5.,5.,5.},{5.,5.,5.}}
M : {{9.,9.},{9.,9.}}         // 9 9 0 / 9 9 0 / 0 0 0, not 9 9 5 / 9 9 5 / 5 5 5
```

Only the *contents* are reset — the storage and extents are untouched, which is what keeps the
by-reference fill above working. Clearing is not rebinding.

*Element* assignment is different and deliberately so: `A[0] : {1.,2.}` **replaces** the row. That is
what makes an array's dimensions measured rather than declared ([§9](#9-arrays)).

### 7.1.1 Omitted entries

A slot left empty inside a literal is an **omitted entry**, and stands for the zero of the element
type. The commas alone fix the shape, so nothing is lost by leaving a value out:

```
M : {{1.0,,},{,1.0,},{,,1.0}}    // the 3x3 identity
K : {{,,},{,,},{1.,1.,1.}}       // only the last row set, the rest zero
```

This is what makes a sparse matrix writable in one line on a phone keyboard, without spelling out
every `0.` — the motivating case being the rotation matrices in `Examples/rotations.shm`, where the
zeros outnumber the values and sit in a different place in each of ROX/ROY/ROZ.

Three consequences worth being explicit about:

- **Absence means zero however it arises.** A gap inside the literal and a literal that stops short
  of the extents do the same thing. That is the point of the zero-fill rule above: without it,
  `{{9.,9.},{9.,9.}}` and `{{9.,9.,},{9.,9.,},{,,}}` would differ on an array that already held
  data, and nothing on the page would say which you had written.
- **A blank carries no type.** The element type is read from the first slot that holds something, so
  a leading gap does not make `{,1.5,}` an int literal. A literal that is blank all the way down has
  a shape but no type, so it cannot *create* an array — `Z : {{,,},{,,}}` is
  `An all-blank literal cannot create 'Z'`. It is perfectly legal once the array is declared, where
  the declaration supplies the type: `real Z[2][2] : {{,},{,}}` is a 2x2 of zeros.
- **A trailing comma is now a slot, not an error.** `{1.,2.,}` is three entries, the last one zero.
  This follows from counting slots by commas and cannot be special-cased away without also breaking
  `{1.0,,}`, which needs its trailing gap to mean a third entry.

### 7.2 Compound assignment

`x +: expr` ⇔ `x : x + expr`; `x -: expr` likewise. Both need a scalar target, with one exception:
`+:` on a `char[]` **appends** ([§10](#10-strings)). `-:` on a string is an error.

### 7.3 Multi-assign

```
<a,b> : f(...)
```

The only way to consume more than one returned value. The count must match the function's output
list exactly; a mismatch is a check error. Targets that do not exist are created with the declared
output types.

This works for a built-in too — `<s> : sqrt(16.)` — which returns exactly one value.

> **2.x note.** Arity here was a warning and the program ran on, silently discarding surplus values
> and leaving surplus variables untouched. It is an error now.

### 7.4 Return

```
return                         // from a function with no outputs
return expr                    // one output
return (expr, expr, ...)       // two or more - the parentheses are required
```

A function that declares outputs must return them on every path; falling off the end is a check
error. The number returned must match the output list.

### 7.5 `if` / `elseif` / `else`

```
if cond { ... } elseif cond { ... } else { ... }
```

Any number of `elseif` branches, at most one `else`. The condition must be a scalar.

### 7.6 `while`

```
while cond { ... }
```

### 7.7 `for`

Two forms:

```
for i : start to end [ step s ] { ... }     // inclusive of end
for i < n [ step s ] { ... }                // 0 to n-1
```

The second is **sugar for the first** — it expands to exactly `for i : 0 to n - 1`, which is why
`step` rides along unchanged and why nothing downstream knows the difference. It is the loop that
walks an array:

```
for i < A.row { ... }
```

`step` defaults to `1`. A step of `0` is an error. The counter belongs to the loop: it is created in
a fresh scope and is gone afterwards, so it never collides with an outer variable of the same name.

If every bound is `int` the loop runs in ints; a `real` anywhere widens the counter and the loop runs
in reals. Because the short form expands before that decision, `for i < 2.9` runs `0 to 1.9` and
stops at `1.0`.

A real bound that no `Int32` can represent — `nan`, `±inf`, or too large — is an error naming which
bound and its value. This matters because such bounds arrive from ordinary arithmetic:
`for i : 1. to sqrt(0.-1.)`, `to 1./0.`, `to pow(10.,400.)`.

**Implementation note:** the conversion is `Int32(exactly: value.rounded(.towardZero))`, never a bare
`Int32(value)`. The bare form *traps* on those cases, and a Swift trap is not a catchable `Error` —
it bypasses `run`'s `do`/`catch` and takes the app down with an empty console.

### 7.7.1 `break` and `continue`

```
break       // leave the innermost enclosing loop
continue    // abandon this pass and take the next
```

Both bind to the **innermost** enclosing loop and there are no labels, so neither can
leave two loops at once. An `if` is not a loop: `break` inside an `if` inside a `for`
leaves the `for`, and `break` inside an `if` that is not inside any loop is a parse
error ([§13.2](#132-parse-errors)).

In a `for`, `continue` still advances the counter - the step belongs to the loop, not to
the body, so it skips the rest of the pass rather than the pass itself:

```
for i < 5 {
  if i % 2 = 0 { continue }
  ? i                          // 1 3
}
```

**Implementation note.** `Interpreter.Flow` carries `.broke` and `.continued` beside
`.returned`. Anything but `.normal` stops the enclosing statement list and travels
outward until something claims it - a loop claims the first two, a call claims the
third. Because the parser refuses `break` and `continue` outside a loop, neither can
reach a call boundary unclaimed, which is why `call` needs no case for them.

> **2.x and early 3.0 note.** Neither word existed, and the absence shaped the programs
> written against it: a search scanned its whole range behind a guard that kept the
> first hit, rather than stopping when it found one. `Examples/prime.shm` and
> `Examples/strsplit.shm` both carried that shape and no longer do.

### 7.8 Print

```
? expr expr ...      // with trailing newline
?? expr expr ...     // no trailing newline
```

**A print command must be the first token on its line.** Indentation doesn't count — the lexer has
already dropped it — so an indented `? x` inside a block is fine. What is rejected is any other token
before it on the same line:

```
? x                               // fine, and indented
fun <> = main() { x : 1 ? x }     // Error: '?' must start its line
? x ?? y                          // Error: '??' must start its line
```

The rule's real purpose is **at most one print command per line**, since a second one necessarily has
the first's items before it.

Each item is printed followed by a single space, then the newline is appended once at the end. So `?`
always leaves a trailing space before its newline.

A numeric **array prints as a grid**, right-aligned in columns, so a program needs no display
function of its own. A `char[]` is not a grid — it prints as text, inline. A multi-row grid starts on
its own line so a label before it does not push the first row out of column.

#### Precision

A `real` prints to a fixed number of decimal places: **7 for a scalar, 6 inside a grid**. Fixed, not
Swift's shortest round-trip description, because a column is read down its digits and `1.0` beside
`0.3333333333333333` cannot be. The grid gets one place fewer because a matrix row has to fit the
editor's line, which holds about 47 characters — at 6 places a 4-column matrix fits and at 7 it
does not.

Past `1e15` a `Double` has no significant digits left to land after the point, so values at or beyond
it — and the non-finite ones — keep the compact spelling instead. Without that, `1e300` would print
as 309 characters.

`prec(n)` overrides both. It is a **print-list directive, not a value**: it prints nothing itself and
applies from that point on, including the rest of its own line.

```
? prec(12) 1./3.      0.333333333333
? prec(3)  1./3.      0.333
? prec(-1) 1./3.      0.3333333   (the default restored)
```

The argument runs `-1` through `24` and **clamps to that range at both ends**, so nothing is ever
refused: `prec(500)` gives 24, `prec(-5)` restores the default. 24 is past the ~17 significant digits
a `Double` carries, while still leaving room to read a `1e-20` tolerance.

Display *rounds* — `prec(0)` on `2.7` prints `3`. That is deliberately unlike `int(2.7)`, which
truncates to `2`. Display and conversion are separate rules.

**Implementation note.** The precision lives in static storage on `Value`, because `text` and `cell`
are context-free computed properties reached from everywhere. `Interpreter.run` resets it on entry so
one program's setting cannot leak into the next run in the same process.

#### Interaction with camera scanning

Line breaks carry meaning, so the scanner must reproduce them faithfully. Vision returns text regions
with bounding boxes and does not promise one region per printed line in either direction, so
`ScanLayout.lines(from:)` reconstructs lines from geometry rather than trusting the order or count of
observations:

- **Splits are repaired.** A wide gap in a printed line can come back as two regions. Regions whose
  vertical centres are within half a line height are grouped and ordered left to right. This is the
  case that matters most: emitting them as two lines would turn a program the scanner should
  *reject* into one that quietly runs.
- **Merges are reported, not repaired.** Two printed lines read as one region cannot be undone by
  grouping, and splitting on an interior `?` is not an option — `? x ? y` typed deliberately has to
  stay the error this section makes it. `ScanLayout.linesWithLateCommand(in:)` finds lines whose
  `?`/`??` isn't first (ignoring string contents and comments) and the post-scan alert names the
  first one.

**Implementation note — why this needs `Token.line`.** Newlines are whitespace to the lexer, so
`? x ?? y` and the same two commands on two lines produce *identical* token streams. Nothing in the
sequence distinguishes them. Enforcing the rule required tokens to remember their source line —
`Token.line`, read only by `Parser.startsLine(at:)`. It is the sole piece of layout the language
preserves.

**Implementation note — where the item list stops.** `parsePrintItems` keeps consuming while the next
token could begin a term (`startsTerm`) and doesn't look like a new statement. That makes
`startsTerm` load-bearing rather than a convenience: a token missing from it doesn't produce an
error, it silently ends the list. It takes a *position* rather than a kind because one case needs
lookahead — `int` opens a term when it is `int(x)` and does not when it is `int k : 5`. If you extend
the expression grammar with a new prefix token, add it here too, or printing it will quietly produce
nothing.

---

## 8. Functions

### 8.1 Definitions

```
fun <outputs> = name(inputs) { body }
```

**The output list holds types, not names**:

```
fun <int>      = square(n: int)            { return n * n }
fun <int,int>  = divide(a: int, b: int)    { return (a / b, a % b) }
fun <>         = show(M[][]: real)         { ? M }
```

> **2.x note.** `<r>` named a *variable*; the function could fall off its end and whatever that
> variable held was returned. Both are gone: `<r>` is now a parse error (`r` is not a type), and a
> declared output must be returned.

A parameter is `name: type`, with `[]` pairs for rank: `M[][]: real` is a two-dimensional real
array. A function may not take the name of a built-in, nor `prec`. `main()` must take no inputs.

A function defined and never called produces a warning, not an error.

### 8.2 Arguments

A **scalar** is passed by value. Assigning to the parameter inside the function does not affect the
caller.

An **array is always passed by reference** — the function works on the caller's storage. This is how
a function hands an array back, since an array cannot appear in the output list:

```
fun <> = fill(v[]: real) {
  for i < v.row { v[i] : 1.0 }
}
```

A **scalar** may be passed by reference too, by marking the parameter `&`:

```
fun <> = bump(&n: int) { n : n + 1 }

fun <> = main() {
  x : 5
  bump(x)
  ? x            // 6
}
```

Unlike an array, this is a copy-in/copy-back: the callee works on its own box and the value is
written back to the caller's variable — or element, since `bump(v[1])` writes back to `v[1]` — when
the call returns.

A reference argument of either kind must be addressable (a variable or an element, not a computed
value: `bump(1+2)` is refused) and must match the parameter's type **exactly** — a reference is never
converted, because a converted copy would silently stop being the caller's.

Argument count is checked exactly. Too few and too many are both errors.

> **2.x note.** Surplus arguments were evaluated and dropped with a warning, so a call with its
> arguments in the wrong order still ran and returned a wrong number.

### 8.3 Recursion depth limits

Unbounded recursion exhausts the native stack as a `SIGSEGV`, which is uncatchable and killed the app
outright. Two ceilings prevent it:

- **Per function:** `256 / (inputs + 1)` frames — 256 for a no-argument function, 128 for one
  argument, 64 for three.
- **Overall:** 1024 frames, which catches mutual recursion where no single function approaches its
  own cap.

Both are deliberate under-approximations of what the stack could take: `fact(150)` is rejected
despite being runnable. Both are one-line changes in `Interpreter.swift`.

The per-function counter is decremented on the throwing path too. It is a per-frame depth, not a
lifetime tally — otherwise one caught-and-reported failure would poison every later call.

---

## 9. Arrays

An array is declared with its extents and indexed from `0`. Indices must be `int`; an out-of-range
index is a runtime error naming the range.

### 9.1 Dimensions

```
A.row       size of dimension 0
A.col       size of dimension 1
A.dim(n)    size of dimension n
len(A)      same as A.row
```

`.row` and `.col` are just the two axes a matrix uses often enough to deserve names; they work at any
rank. **An axis the array does not have answers `-1`** rather than failing, so asking a vector for
its columns is a legal question with a recognizable answer:

```
real v[10]
? v.row v.col        // 10 -1
```

The same holds for `.dim(n)` past the rank, and for a negative axis, so the three spellings never
disagree. Asking a *scalar* is an error, because that is a type confusion rather than a rank one.

The axis may be any int expression, so `.dim` covers ranks above two. The result is always `int`.

**The dimensions are measured, not declared.** They are read from the value at run time, which is
what lets a function measure an array it was given, and what makes a row replaced at run time report
its real length:

```
real A[2][5]
A[0] : {1.,2.}
? A[0].row           // 2
```

These are read-only; assigning to one is a parse error.

---

## 10. Strings

Text is `char[]`. The declared size is capacity; the text inside may be shorter, terminated by
`char(0)`.

```
char a[20] : "alice"
char b[128] : "bob"
```

Seven operators work on two strings:

| | |
|---|---|
| `=` `!=` | content equality |
| `<` `>` `<=` `>=` | lexicographic order |
| `+` | join, producing a new string sized to the result |

and `+:` appends, which is how one is built in a loop.

**Comparison reads the text up to the terminator, never the declared capacity**, so the same name in
a `char[20]` and a `char[128]` is equal. Without that, buffer size would leak into meaning.

**Ordering is by character code**, so every capital sorts before every lower-case letter: `"Zoe"` is
before `"adam"`. That is the conventional rule and the reason a name list wants folding to one case
before sorting.

Joining into a fixed array fits that array; anything past the end is dropped.

No other operator applies, and none of them mix a string with a number.

### 10.1 Characters

Indexing text gives one `char`, which is a genuine scalar: it can be declared, held, compared,
ordered, passed, and **returned** — unlike an array.

There is no character literal. The only ways to obtain a `char` are to index a string or to call
`char(n)`; `char(0)` names the terminator, which is how the length of the text inside is found.

Because `char` has no arithmetic, anything character-specific goes through the conversions:

```
char s[8] : "abc"
? int(s[0])              // 97
? char(int(s[0]) - 32)   // A
```

### 10.2 What is not provided

There are no string built-ins — no length-of-contents, no search, no split, no case folding. They are
writable in Shalimar itself and are left to the programmer, in the same way `rad()` in
`Examples/rotmat.shm` is. One thing still shapes how they come out: there is no array of strings, so
a list of names cannot be held or sorted. `Examples/strsplit.shm` is the worked example.

---

## 11. Statement boundaries — why there are no semicolons

Statements have no terminator. The parser decides where one ends by *what it is looking at*, not by
punctuation, and two helpers carry that weight:

- `looksLikeNewStatement(at:)` recognizes the shapes that can only begin a statement — an identifier
  followed by `:`, `+:`, `-:` or `=`, and a multi-assign header. A print/return item list stops when
  it sees one.
- `startsLine(at:)` compares a token's line with its predecessor's, used only for the print rule
  ([§7.8](#78-print)).

The known gap: `looksLikeNewStatement` does not recognize `identifier(` as the start of a statement,
so a bare call on the line after a print would be read as another item. The *line* check is what
saves it — the item list also stops at a line boundary — which is why both halves of the print rule
exist.

---

## 12. Built-in functions and constants

| Function | Args | Notes |
|---|---|---|
| `abs(x)` | 1 | int in, int out; otherwise real |
| `sqrt(x)` | 1 | `nan` for negative `x`, not an error |
| `log(x)` | 1 | natural log |
| `exp(x)` | 1 | inverse of `log`; `exp(1.)` is `e` |
| `hypot(x,y)` | 2 | `sqrt(x*x + y*y)` without the overflow |
| `sin(x)` `cos(x)` `tan(x)` | 1 | radians |
| `asin(x)` `acos(x)` `atan(x)` | 1 | radians |
| `atan2(y,x)` | 2 | |
| `pow(x,y)` | 2 | same as `x^y` |
| `round(x)` | 1 | half away from zero |
| `ceil(x)` `floor(x)` | 1 | |
| `trunc(x)` | 1 | toward zero, but stays `real` — see below |
| `max(a,b)` `min(a,b)` | 2 | int in, int out; otherwise real |
| `len(A)` | 1 | element count of the first dimension; capacity for a string |
| `int(x)` `real(x)` `char(x)` | 1 | conversions, see below |

Argument counts are enforced exactly, for built-ins as for user functions. A user function may not
take a built-in's name.

`len` on a string is **capacity**, not content length, so it is never `0` for a declared array and
`real w[len(s)]` is always legal.

| Constant | Value |
|---|---|
| `pi` | 3.141592653589793 |
| `e` | 2.718281828459045 |

`pi` and `e` are **read-only and reserved**: neither can be declared, assigned, taken as a parameter
name, or used as a loop counter.

**`trunc` is not `int`.** Both truncate toward zero, but `int()` returns an `int` and fails outside
its range, while `trunc` stays `real` and so handles any magnitude:

```
? int(2.7) int(-2.7)      // 2 -2
? trunc(2.7) trunc(-2.7)  // 2.0000000 -2.0000000
? trunc(3000000000.7)     // 3000000000.0000000
? int(3000000000.)        // Error - Cannot convert 3000000000.0 to int
```

There is no `fmod`: the `%` operator already does it on reals, with C's sign-follows-the-dividend
rule. There is no `modf` either — builtins return exactly one value, and with `trunc` present it is
two lines: `i : trunc(x)` then `f : x - i`.

> **2.x note.** Constants were resolved *last*, behind locals and globals, so a variable of the same
> name shadowed them. That does not survive a checker which defines a variable on first assignment —
> `pi : 3` would quietly overwrite the constant rather than shadow it. One name, one meaning.

### 12.1 Conversions

```
int(x)      drop the fraction, toward zero
real(x)     exact
char(x)     the character with that code
```

Each takes **any scalar** and produces the type it is named for. `int(2.7)` is `2` and `int(-2.7)` is
`-2`; it never rounds. `char` is one byte, so a value outside `0..255` is an error rather than being
wrapped — a wrapped code point is a wrong character that looks like a right one.

**Implementation note — two traps in one feature.** These were implemented on both sides and
*unreachable*: the lexer makes `int` a keyword before the parser can read `int(` as a call. Making
them parse then exposed a second bug — each declares an input type, so `real(2.7)` was coerced
through `int` on the way in and came back `2.0`, truncating the value the call exists to preserve.
They now take their argument untouched and carry their own target. If you add a fourth conversion,
both hazards apply to it.

---

## 13. Diagnostics

Every message names its line and takes one of two forms:

```
Error: line 3: ...          the program does not run
Warning: line 3: ...        the program runs anyway
```

There is deliberately **no stage name** in the message — no "Lex error", "Parse error" or "Runtime
error". Which stage caught a problem is an implementation detail, and the vocabulary of a compiler's
internals has no place in a diagnostic aimed at someone writing a program. For the same reason no
message quotes a token kind, an AST node type, or a byte width.

Messages are kept short on purpose. The console line holds about 47 characters, so a long message
wraps two or three times and pushes the program's own output off screen; the detail lives in this
document and in the in-app reference instead.

The console colours by severity: output near-black, warnings amber, errors red. The interpreter has a
separate diagnostic channel from its output callback, so a runtime failure can be told from a
program's own printing without inspecting the text.

### 13.1 Lex errors

Tokenizing stops at the offending character, so the token stream is truncated and nothing downstream
is trustworthy — the lexer's error is reported alone.

- `Malformed number '1.2.3'` — also `'1e'`, `'1e-'` ([§2.3](#23-numbers)).
- `'99999999999' is too big for int - add '.'`
- `'!' is not a command - use '??' or '!='`
- `Unclosed string - add '"'` — on the line the quote opens ([§2.4](#24-string-literals)).
- `Unexpected character 'х' (U+0445)` — the code point is not internals; it is the only way to tell a
  Cyrillic `х` from a Latin `x` on screen ([§2.2](#22-identifiers)).

### 13.2 Parse errors

Reported one at a time; parsing stops at the first.

- `Unexpected '*'`
- `Program ends unfinished` — running off the end has no spelling to quote, so it gets a sentence.
- `Missing '{' to start block` / `Missing '}' to close block`
- `'?' must start its line`
- `'x': declare it at the top of the function`
- `'return' outside a function`
- `'break' outside a loop` / `'continue' outside a loop`
- `'?' must be inside a function`
- `No '.size' - use .row, .col or .dim(n)` / `'.row' is read-only` / `'.dim' needs an axis, as
  .dim(0)`

### 13.3 Check errors and warnings

**The checker does not stop at the first problem.** It types the whole program and reports everything
it finds, then refuses to run if any of them is an error. This is why several diagnostics can appear
at once, and why warnings appear for programs that still run.

Errors here cover undefined names, type mismatches, arity, array extents that can be folded, reserved
names, returns that do not match the output list, arithmetic on a `char`
(`'+' does not apply to char`, [§5.3](#53-what-does-not-convert)), and the two ways an array cannot
be created by assignment — `Declare the array 'C' first` for a non-literal right-hand side, and
`An all-blank literal cannot create 'Z'` for a literal with no entry to take a type from
([§7.1.1](#711-omitted-entries)).

The three warnings:

- `Function 'f' is defined but never called`
- `'x' hides a global`
- `Loop never runs: 'i' starts at 10 and step 1 moves away from 1`

The last is reported only where the start, the end and the step all fold to numbers, which
is where the direction is written into the source and nothing else could have been meant. A
loop with a computed bound is left alone: an empty pass may be exactly what that run intends,
and `for j < v.col` over a vector — which expands to `0 to -2` — is the language's own example
of one.

### 13.4 Runtime errors

Anything that cannot be known statically: division by zero, `int` overflow, an out-of-range index or
conversion, a non-finite loop bound, a computed array extent below 1, and the recursion ceilings.

Output already printed survives — the error follows it rather than replacing it.

**The class to watch when extending the interpreter** is the Swift *trap*: an uncatchable failure
that bypasses `run`'s `do`/`catch` and kills the app with an empty console. Force-unwraps, raw array
subscripts, `Int32(someDouble)` and unchecked arithmetic are all in it. Every conversion in the
interpreter uses the `exactly:` or `...ReportingOverflow` form for this reason.

---

## 14. Limits

| | |
|---|---|
| `int` range | -2147483648 .. 2147483647 |
| Array extent | 1 or more; fixed at declaration |
| `char` | 0..255 |
| Recursion, per function | `256 / (inputs + 1)` frames |
| Recursion, overall | 1024 frames |
| `prec(n)` | -1 .. 24, clamped |
| Real printing | fixed places; compact past 1e15 and for non-finite |

Passing an `int` limit is always an error, never a wrapped value that looks right.

---

## 15. Known limitations & maintainer notes

Real, verified behavior of the current interpreter, worth knowing before extending it.

1. **An array cannot be returned in `<>`.** Outputs are scalars by construction — the output list is
   parsed with `parseScalarType()`, which has no bracket loop. Pass one in and fill it
   ([§8.2](#82-arguments)). Adding it is mechanically cheap (`ArrayRef` is a class, so returning one
   costs nothing) but turns on an unsettled question: whether `<M> : build()` **rebinds** M to the
   callee's array — introducing aliasing that nothing at the call site announces — or **copies** into
   M's existing storage, keeping one owner but letting M's declared size silently truncate.
2. **There is no array of strings.** `char` is one-dimensional by rule, so a list of names cannot be
   held or sorted, even though two names compare and swap correctly. Lifting it means deciding what
   `? names` should print for a `char[3][20]`: three lines of text, or a character grid.
3. **`real` → `int` narrowing is silent** ([§5.2](#52-widening-and-narrowing)). Now that `int()` is
   callable, requiring the narrowing to be written explicitly is a viable third option; it would want
   measuring as a warning first, since the blast radius across existing programs is unknown.
4. **A loop cannot be left from more than one level at once.** `break` and `continue` bind to the
   innermost loop and there are no labels, so escaping two loops needs a flag or a `return`
   ([§7.7.1](#771-break-and-continue)).
5. **There is no input.** A program only prints.
6. **A print/return item list can swallow a following bare call** — `looksLikeNewStatement` doesn't
   recognize `identifier(`. The line check is what covers it in practice
   ([§11](#11-statement-boundaries--why-there-are-no-semicolons)).
7. **Unary minus binds tighter than `^` on the base side** — `-2^2 == 4`
   ([§4.1](#41-unary-minus-vs---a-real-gotcha)).
8. **Recursion is capped well below what the stack could take** ([§8.3](#83-recursion-depth-limits)).
9. **Runtime lines are per statement, not per expression.** Only statement nodes carry a line, so an
   error inside a long expression names the statement containing it. `Interpreter.currentLine` is a
   single running value, not a call stack: it names the innermost statement executing, which is the
   useful answer, but there is no traceback.
10. **Precision is static, mutable state.** `Value.scalarPlaces`/`gridPlaces` are statics because
    `text` and `cell` are context-free computed properties. `run()` resets them on entry, which is
    what keeps one program's `prec(n)` out of the next.
11. **`.col` returning -1 meets `for i < n` quietly.** `for j < v.col` on a vector becomes
    `for j : 0 to -2` and runs zero times with no diagnostic. That is the intended reading — iterating
    the columns of something with no columns is a no-op — but it does mean a rank mistake does nothing
    rather than complaining.
12. **The editor's re-indenting logic assumes `/* */` block comments exist**, for indentation only,
    even though the lexer doesn't recognize them ([§2.1](#21-comments)). If block comments are ever
    added, that file needs revisiting too.
13. **A scanned line that Vision merges with its neighbour can only be reported, not recovered**
    ([§7.8](#78-print)). Automatic splitting is deliberately not attempted — it would silently rewrite
    a program whose one-line print command is a real error.
14. **The keyboard's full stop is undone after the fact, not refused.** iOS turns two spaces in a row
    into `". "`, closing what it takes to be a sentence. In prose that is right; here the period lands
    against whatever name precedes it and the lexer reads `name.` as the head of an attribute
    ([§9.1](#91-dimensions)), so a line being typed correctly stops parsing. It is a keyboard setting
    rather than an autocorrection one, so the `UITextInputTraits` set in `viewDidLoad` — which do
    switch off smart quotes, smart dashes and spell checking — leave it on.

    It also cannot be intercepted. An instrumented build logged every route into the text and the
    substitution came through none of them: `textView(_:shouldChangeTextIn:replacementText:)` saw the
    second space as an ordinary space, and a `UITextView` subclass overriding `insertText`, `replace`,
    `deleteBackward` and the marked-text pair saw only the ordinary keystrokes. The character before
    the caret turned into a period that nothing was told about.

    So the editor repairs it instead. The second space *is* reported, so `shouldChangeTextIn` notes
    the index of the space it is about to land beside (`Indent.spaceAtRiskOfPeriod`) and
    `textViewDidChange` reads that character back: if it has become a period, it goes back to being a
    space. Being armed by the keystroke rather than by hunting the text for periods is what makes it
    exact — a period the typist wrote is never touched, so `1.5` and `A.row` type normally.

    **The assumption to watch** is that the substitution lands in the same run-loop turn as the change
    notification. It does today, which is the only reason the repair sees a period rather than a
    space; if a future iOS deferred it by a turn, the repair would quietly stop firing and the periods
    would come back. Nothing in `Tests/regression.sh` would notice — the suite compiles no UIKit, and
    the shortcut cannot be triggered by injected text at all, only by a thumb on a space bar. This is
    the one behaviour in the project whose only test is a person typing.

    Only the editor is affected. A program arriving from a file or the scanner never passes through
    the keyboard.

---

## 16. Architecture map

| Stage | File | Responsibility |
|---|---|---|
| Lex | `TokenKind.swift` | source `String` → `[Token]`, each stamped with its source line; owns the token patterns and their **order** |
| AST | `Node.swift` | node type definitions and `ShalimarType` |
| Parse | `Parse.swift` | `[Token]` → `[Node]`; no type knowledge |
| Check | `Check.swift` | types the whole program, inserts conversions, reports every problem it finds rather than the first |
| Evaluate | `Interpreter.swift` | walks the checked AST, maintains per-call scopes, resolves built-ins vs. user functions, drives all program `output` |
| UI wiring | `ComputeViewController.swift` | owns the program/console text views, runs lex → parse → check → interpret, colours the console by severity, plus save/load and OCR scan |
| Reference | `HelpViewController.swift` | the in-app language reference; every code line in it runs as written |
| Scan layout | `ScanLayout.swift` | OCR regions → source lines; no UIKit/Vision dependency, so it is testable from the command line |
| Editor layout | `Indent.swift` | where a line sits by brace depth, what a typed newline or `}` becomes, how an arriving paste is laid out, and where the keyboard's full stop is about to strike ([§15](#15-known-limitations--maintainer-notes), note 14); kept clear of UIKit for the same reason as `ScanLayout` |
| Test harness | `Tests/harness/main.swift` | command-line driver; mirrors the app's order exactly, so the suite tests what the app does |
| Regression suite | `Tests/regression.sh` | ~445 cases, including every program in `Examples/` end to end |
| Scan-layout tests | `Tests/scanlayout/main.swift` | 16 cases over `ScanLayout`, folded into the suite's counts |
| Editor-layout tests | `Tests/indent/main.swift` | 51 cases over `Indent`, written as the editor sees them — a document with a caret, an edit, and the document that should come back |

The checker is the stage 3.0 added, and it behaves unlike the other two: it does not stop at the
first problem, so every diagnostic is printed and only an error prevents the run.

### 16.1 Running the tests

The language core is pure Foundation with no UIKit dependency, so it compiles and runs outside the
app — no simulator and no Xcode test target required:

```
./Tests/regression.sh
```

Every case traces to a claim in this document. **When a case and this document disagree, the document
wins** and the interpreter is what gets fixed — same rule as the preamble.

A pre-commit hook in `.githooks/pre-commit` runs the suite against the *staged* tree, so an unstaged
local fix cannot mask a commit that is broken on its own. Enable it once per clone:

```
git config core.hooksPath .githooks
```

`Interpreter.output: (String) -> Void` defaults to a plain `print(...)`, but the app replaces it with
a closure that appends to the console. `Interpreter.diagnostic` is the separate channel for the
interpreter's own failures, so the console can colour them without inspecting the text. Anything
written through either reaches the screen; a bare Swift `print(...)` elsewhere in the core does not —
and there are none.
