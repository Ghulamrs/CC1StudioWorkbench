# CC1 Studio Workbench - an editor that drives the cc1 compiler. ed1 is the
# terminal half and ed1gui the window; this builds ed1.
#
# Everything except one file is ordinary C++14 and builds anywhere. The
# exception is the terminal, and even that is smaller than it looks: Windows 10
# and later understand the same escape sequences as a Unix terminal once
# ENABLE_VIRTUAL_TERMINAL_PROCESSING is set, so src/terminal_win.cpp differs
# from src/terminal.cpp only in how the console is put into raw mode. The
# drawing, the key decoding and the status bar are the same code on both.
#
# On Windows with MSVC, use build.bat instead - it calls cl directly, since
# that machine has no make.

UNAME_S := $(shell uname -s 2>/dev/null)

ifeq ($(origin CXX),default)
  ifeq ($(UNAME_S),Darwin)
    CXX := clang++
  else
    CXX := g++
  endif
endif

# C++14, not because nothing newer works here but because cc1 is C++14 and the
# arena it is developed in holds itself to what it compiles. That is why
# src/path.cpp exists: <filesystem> is C++17.
CXXFLAGS := -std=c++14 -Wall -Wextra -Werror -pedantic -O2

# MSYS and MinGW report themselves here, and want the Windows console.
ifneq (,$(findstring MINGW,$(UNAME_S)))
  TERM_SRC := src/terminal_win.cpp
else ifneq (,$(findstring MSYS,$(UNAME_S)))
  TERM_SRC := src/terminal_win.cpp
else
  TERM_SRC := src/terminal.cpp
endif

SRC := src/main.cpp src/editor.cpp src/buffer.cpp src/compile.cpp \
       src/indent.cpp src/menu.cpp src/tree.cpp src/syntax.cpp \
       src/toolchain.cpp src/json.cpp src/project.cpp src/find.cpp \
       src/utf8.cpp src/workspace.cpp src/symbols.cpp src/demangle_win.cpp \
       src/path.cpp src/process.cpp src/debugger.cpp src/settings.cpp src/about.cpp \
       src/terminal_common.cpp \
       $(TERM_SRC)

# The objects go under src/obj rather than beside the sources they came from,
# so that a listing of src/ is the code and nothing else.
OBJDIR := src/obj
OBJ := $(patsubst src/%.cpp,$(OBJDIR)/%.o,$(SRC))

ed1: $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ)

$(OBJDIR)/%.o: src/%.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<

$(OBJDIR):
	mkdir -p $(OBJDIR)

# Which headers each object depends on is the compiler's answer, not a list
# kept by hand here. The list that used to be here had gone stale: editor.cpp
# had come to include debugger.h and the line for editor.o did not say so, so a
# member added to Debugger rebuilt debugger.o and not editor.o. One binary then
# held two ideas of where that class's members were, and it segfaulted - after
# a run of tests that had looked like a parser bug. A clean build hid it, which
# is the worst thing a bug of this kind can do.
-include $(OBJ:.o=.d)

# The two pieces with a contract: the layout rules, and the reading of cc1's
# diagnostic - which has to cope with a Windows path whose drive letter is
# followed by a colon that is not a separator.
test: tests/test
	./tests/test

tests/test: tests/test.cpp src/compile.cpp src/indent.cpp src/syntax.cpp \
            src/toolchain.cpp src/json.cpp src/project.cpp src/find.cpp \
       src/utf8.cpp src/workspace.cpp src/symbols.cpp src/demangle_win.cpp \
            src/path.cpp src/process.cpp src/debugger.cpp src/settings.cpp src/about.cpp \
            src/buffer.cpp \
            winforms/bridge.cpp winforms/bridge.h src/compile.h src/indent.h src/syntax.h \
            src/json.h src/project.h src/path.h src/buffer.h
	$(CXX) $(CXXFLAGS) -Isrc -Iwinforms -o $@ tests/test.cpp winforms/bridge.cpp \
	    src/compile.cpp src/indent.cpp \
	    src/syntax.cpp src/toolchain.cpp src/json.cpp src/project.cpp src/find.cpp \
       src/utf8.cpp src/workspace.cpp src/symbols.cpp src/demangle_win.cpp \
	    src/path.cpp src/process.cpp src/debugger.cpp src/settings.cpp src/about.cpp \
	    src/buffer.cpp

# The other half of the checking: the editor itself, driven by keystrokes.
# CC1 names a compiler for the build cases; without one they are skipped.
session: tests/session ed1
	./tests/session ./ed1 $(CC1)

tests/session: tests/session.cpp src/path.cpp src/path.h
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/session.cpp src/path.cpp

check: test session

# The Xcode project is generated from the source list above rather than kept by
# hand, so it cannot fall behind it. Run this after adding or removing a file.
xcodeproj:
	python3 tools/make-xcodeproj.py

# What gets used, as against what gets built. The binaries land beside their
# objects because that is where a build puts them; this is where the product
# lives - one directory holding what you would actually run, away from the
# project space it was compiled in.
#
# PRODUCT names it, so a different one can be asked for without editing this.
PRODUCT ?= $(HOME)/cc1-studio

product: ed1
	mkdir -p "$(PRODUCT)/bin" "$(PRODUCT)/examples"
	cp ed1 "$(PRODUCT)/bin/"
	cp README.md "$(PRODUCT)/"
	cp examples/*.c examples/*.cpp "$(PRODUCT)/examples/"
	@echo "CC1 Studio Workbench is in $(PRODUCT)"

clean:
	rm -rf $(OBJDIR)
	rm -f ed1 tests/test tests/session

.PHONY: test session check xcodeproj product clean
