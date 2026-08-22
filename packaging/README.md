# Packaging

RStudio and the two compilers it drives, as one installable thing on each
platform. They are packaged together because they are used together: the editor
drives cc1 and shc and is not much good without them.

| | | built and installed |
| --- | --- | --- |
| Linux | [`rstudio-editor.spec`](rstudio-editor.spec) | RPM, into `/opt/rstudio` |
| macOS | [`rstudio-editor.rb`](rstudio-editor.rb) | Homebrew formula |

**Named `rstudio-editor`, not `rstudio`.** "RStudio" is also a widely packaged
IDE for R, and a package by that name installing something else would be a trap
for whoever typed it. The programs keep their own names — `RStudio.exe`,
`cc1.exe`, `shc.exe`.

## Building them

```
rpmbuild -ba packaging/rstudio-editor.spec --define "_sourcedir $PWD/.."
brew install --build-from-source --HEAD <tap>/rstudio-editor
```

The RPM builds from three checkouts sitting side by side, which is what this
project actually is. The formula cannot: Homebrew builds in a temporary
directory, so the two compilers are **resources** fetched from their own
repositories — which means the formula installs what is *pushed*, not what is
in your tree.

## What packaging had to fix first

Neither program could be installed anywhere before this, and both reasons are
about how they find their own files.

**shc took the directory out of `argv[0]`.** argv[0] is not a path: started
through PATH — which is how an installed compiler is always started — a program
is handed a bare name, whose directory is `.`, so the runtime was looked for in
`./lib/shmrt-*.a`. It asks the machine now, and resolves the answer, because
Homebrew exposes binaries as symbolic links into a cellar.

**shc looked in one place.** Beside itself in `lib/`, which is the source tree.
It also tries `../lib` now — `<prefix>/bin` holding the compiler and
`<prefix>/lib` the runtime — because a package that had to put its runtime in
`<prefix>/bin/lib` to be found would be a package nobody could read.

**cc1 needed no code change**: its include directory is compiled in through
`CC1_INCLUDE_DIR`, and `INCDIR` is a plain `=` in its Makefile, so both
packages set it at build time to where the headers will actually live.

## Both check the compilers, not the files

A package that installs a compiler which cannot compile is the failure worth
catching, and it is not caught by checking that a file exists. `%check` and the
formula's `test do` each compile and run a C program and a Shalimar one, and
compare what they printed.

Two things that cost a build each, written down so they do not cost another:

- **rpmbuild runs a scriptlet under `set -e`**, so a probe that deliberately
  exits 42 kills the script before anything can read the status. Compare
  output, not exit codes.
- **Shalimar's `?` leaves a trailing space**, so its probe prints `"42 "`. The
  checks trim; the exact spacing is the language's business and
  `Compiler-S/tests/cases` is where it is pinned down.
