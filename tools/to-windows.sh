#!/usr/bin/env bash
#
# Copies this tree to the Windows box and builds it there.
#
# That box has no git: it is reached over ssh and the sources are put there by
# hand, which is how a stale build.bat and a stale test.cpp each came to report
# an old, smaller suite that looked green. So this script copies the build
# scripts and the tests as well as the sources, every time, and copies into the
# same directories the Mac has rather than into the root - build.bat names
# src\*.cpp and tests\*.cpp explicitly, so a file that lands in the root
# updates nothing and is not compiled.
#
# The loose .cpp and .h files in that root, and its stale-headers directory,
# are leftovers from an older flat layout. Nothing builds them. They are left
# alone rather than tidied, because tidying somebody's machine from a script is
# not this script's business.
#
#   ./tools/to-windows.sh              build and run both suites
#   ./tools/to-windows.sh build        build only
#   ./tools/to-windows.sh gui          also msbuild the window, which
#                                      build.bat never compiles
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

BOX="${ED1_WINDOWS_BOX:-windows}"
DIR="${ED1_WINDOWS_DIR:-CC1StudioWorkbench}"
WHAT="${1:-check}"

say() { printf '%s\n' "$*"; }

say "copying to $BOX:$DIR"
ssh -n "$BOX" "powershell -NoProfile -Command \"New-Item -ItemType Directory -Force -Path '\$HOME\\$DIR\\src\\shalimar','\$HOME\\$DIR\\tests','\$HOME\\$DIR\\winforms','\$HOME\\$DIR\\examples','\$HOME\\$DIR\\help' | Out-Null\"" || exit 2

scp -q src/*.cpp src/*.h "$BOX:$DIR/src/" || exit 2
scp -q src/shalimar/*.cpp src/shalimar/*.h "$BOX:$DIR/src/shalimar/" || exit 2
scp -q tests/*.cpp "$BOX:$DIR/tests/" || exit 2
scp -q winforms/* "$BOX:$DIR/winforms/" 2>/dev/null
scp -q build.bat README.md "$BOX:$DIR/" || exit 2
# The solution and the project it names. RStudio.sln reaches ../Compiler-C and
# ../Compiler-S, so the two compilers have to be relayed too for it to build -
# tools/to-windows.sh --solution does that and msbuilds it.
scp -q RStudio.sln winconsole.vcxproj "$BOX:$DIR/" 2>/dev/null
# help/ travels because tests/test.cpp checks Help > Contents against it: a
# page named by the editor and absent from disk is a check that fails, and
# skipping it on two of the three machines would be checking it in one place.
scp -q help/*.md "$BOX:$DIR/help/" || exit 2
scp -q examples/* "$BOX:$DIR/examples/" 2>/dev/null

# The shell on the other end is PowerShell, not cmd - so && is not a statement
# separator there, and build.bat is a batch file and has to be run through cmd
# /c. Both of those are one-line lessons that cost an hour each to learn twice.
#
# CC1 and SHC name the compilers for the build cases, the same way make does
# here. They are paths on that machine, so they are not spelled from this one.
#
# shc.exe is new there: Compiler-S grew an MSVC build on 2026-08-22
# (Compiler-S/build.bat, put there by its tests/build-windows.sh), and until
# then every Shalimar case on this box skipped itself for want of a compiler.
# Its driver calls ml64 and link by their bare names, so it needs the Visual
# Studio environment at run time as well - which build.bat has already set up
# by the time the suites run, and which the editor arranges for itself through
# prepareFor().
CC1_THERE='$env:USERPROFILE\Compiler-C\msvc\x64\Release\cc1.exe'
SHC_THERE='$env:USERPROFILE\Compiler-S\shc.exe'

if [ "$WHAT" = "gui" ]; then
    say "msbuild winforms\\ed1gui.vcxproj"
    ssh -n "$BOX" "cd $DIR; msbuild winforms\\ed1gui.vcxproj /p:Configuration=Release /p:Platform=x64 /v:minimal"
    exit $?
fi

say "build.bat $WHAT"
ssh -n "$BOX" "cd $DIR; \$env:CC1=\"$CC1_THERE\"; \$env:SHC=\"$SHC_THERE\"; cmd /c build.bat $WHAT"
