#!/usr/bin/env bash
#
# Builds this editor on the Linux box and runs both suites there.
#
# Three reasons it is worth doing, and only the first is the obvious one.
#
# It is the third machine, and the editor claims to be one editor on all three.
# It is built with real g++ rather than Apple's clang, which is the only thing
# that can say whether the sources are ISO C++14 - Apple's libc++ hands you
# C++17 names under -std=c++14, so a C++17-ism compiles clean on a Mac and
# passes the host suite. And it is where a C++ group goes to g++ rather than to
# clang++ or cl, which is a routing this editor now makes and nowhere else can
# check.
#
# The tree goes over as a tarball and is built from clean, for the reason
# Compiler-S's own relay gives: an object left behind from a previous relay is
# compiled against the previous headers and the link still succeeds, because
# the mangled names match.
#
#   ./tools/to-linux.sh              build and run both suites
#   ./tools/to-linux.sh build        build only
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

KEY="${ED1_LINUX_KEY:-$HOME/Documents/Claude/myMorningWalk.pem}"
BOX="${ED1_LINUX_BOX:-ec2-user@52.202.164.123}"
DIR="${ED1_LINUX_DIR:-rstudio}"
WHAT="${1:-check}"

# cc1 lives on that box too, and its own checkout is what has it. Named here
# rather than found, so that a suite reporting "no cc1" is reporting a fact
# about that machine and not about this script.
CC1_THERE="${ED1_LINUX_CC1:-\$HOME/ansicc/cc1}"

# And shc, which Compiler-S's own relay leaves in ~/shalimar. Both are named
# rather than searched for, so that a suite saying "no cc1 named" is saying
# something about that machine and not about this script having looked in the
# wrong place - which is exactly what it said the first time.
SHC_THERE="${ED1_LINUX_SHC:-\$HOME/shalimar/shc}"

# src/obj is excluded and that is not tidiness. The first run of this script
# carried the Mac's own Mach-O objects over, make found them newer than the
# sources it had just unpacked, and the link failed with "file format not
# recognized" - which reads as a broken toolchain on that box and is nothing of
# the kind. Everything is built from clean there for the same family of reason
# Compiler-S's relay gives.
# Built things are excluded by name as well as by suffix: tests/test and
# tests/session have no extension, so a Mach-O one travelled over and make
# found it newer than tests/test.cpp - "cannot execute binary file", which
# reads as a broken box and is the Mac's own binary being run on Linux.
tar --no-mac-metadata --exclude 'src/obj' --exclude '*.o' --exclude '*.d' \
    --exclude 'tests/test' --exclude 'tests/session' --exclude 'ed1' \
    -czf "${TMPDIR:-/tmp}/ed1-src.tgz" \
    src tests winforms examples Makefile README.md 2>/dev/null || exit 2

ssh -n -i "$KEY" "$BOX" "rm -rf ~/$DIR && mkdir -p ~/$DIR" || exit 2
scp -q -i "$KEY" "${TMPDIR:-/tmp}/ed1-src.tgz" "$BOX:~/$DIR/" || exit 2

# g++ needs telling where its own headers' worth of parallelism is; -j is the
# box's business rather than this script's, and that box is small.
ssh -n -i "$KEY" "$BOX" "cd ~/$DIR && tar xzf ed1-src.tgz 2>/dev/null; find . -name '._*' -delete && \
    make -j2 2>&1 | grep -E 'error|Error' ; \
    [ -x ./ed1 ] || { echo 'no ed1 was built'; exit 2; } ; \
    if [ \"$WHAT\" = build ]; then echo 'built ed1'; exit 0; fi ; \
    make check CC1=$CC1_THERE SHC=$SHC_THERE"
