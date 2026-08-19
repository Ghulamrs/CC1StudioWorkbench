# ed1 - an editor that drives the cc1 compiler.
#
# Everything except one file is ordinary C++17 and builds anywhere. The
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

CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -pedantic -O2

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
       src/terminal_common.cpp \
       $(TERM_SRC)

# The objects go under src/obj rather than beside the sources they came from,
# so that a listing of src/ is the code and nothing else.
OBJDIR := src/obj
OBJ := $(patsubst src/%.cpp,$(OBJDIR)/%.o,$(SRC))

ed1: $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ)

$(OBJDIR)/%.o: src/%.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/main.o:     src/editor.h src/buffer.h src/compile.h src/terminal.h src/indent.h \
                src/menu.h src/tree.h
$(OBJDIR)/editor.o:   src/editor.h src/buffer.h src/compile.h src/terminal.h src/indent.h \
                src/menu.h src/tree.h
$(OBJDIR)/indent.o:   src/indent.h
$(OBJDIR)/menu.o:     src/menu.h src/terminal.h
$(OBJDIR)/tree.o:     src/tree.h
$(OBJDIR)/syntax.o:   src/syntax.h
$(OBJDIR)/toolchain.o: src/toolchain.h src/syntax.h
$(OBJDIR)/json.o:     src/json.h
$(OBJDIR)/find.o:     src/find.h
$(OBJDIR)/utf8.o:     src/utf8.h
$(OBJDIR)/workspace.o: src/workspace.h src/project.h
$(OBJDIR)/symbols.o:  src/symbols.h
$(OBJDIR)/project.o:  src/project.h src/json.h src/indent.h src/toolchain.h
$(OBJDIR)/compile.o:  src/compile.h src/toolchain.h
$(OBJDIR)/buffer.o:   src/buffer.h
$(OBJDIR)/compile.o:  src/compile.h
$(OBJDIR)/terminal.o: src/terminal.h
$(OBJDIR)/terminal_common.o: src/terminal.h
$(OBJDIR)/terminal_win.o: src/terminal.h

# The two pieces with a contract: the layout rules, and the reading of cc1's
# diagnostic - which has to cope with a Windows path whose drive letter is
# followed by a colon that is not a separator.
test: tests/test
	./tests/test

tests/test: tests/test.cpp src/compile.cpp src/indent.cpp src/syntax.cpp \
            src/toolchain.cpp src/json.cpp src/project.cpp src/find.cpp \
       src/utf8.cpp src/workspace.cpp src/symbols.cpp src/demangle_win.cpp \
            src/buffer.cpp src/compile.h src/indent.h src/syntax.h src/json.h \
            src/project.h src/buffer.h
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test.cpp src/compile.cpp src/indent.cpp \
	    src/syntax.cpp src/toolchain.cpp src/json.cpp src/project.cpp src/find.cpp \
       src/utf8.cpp src/workspace.cpp src/symbols.cpp src/demangle_win.cpp \
	    src/buffer.cpp

# The other half of the checking: the editor itself, driven by keystrokes.
# CC1 names a compiler for the build cases; without one they are skipped.
session: tests/session ed1
	./tests/session ./ed1 $(CC1)

tests/session: tests/session.cpp
	$(CXX) $(CXXFLAGS) -o $@ tests/session.cpp

check: test session

# The Xcode project is generated from the source list above rather than kept by
# hand, so it cannot fall behind it. Run this after adding or removing a file.
xcodeproj:
	python3 tools/make-xcodeproj.py

clean:
	rm -rf $(OBJDIR)
	rm -f ed1 tests/test tests/session

.PHONY: test session check xcodeproj clean
