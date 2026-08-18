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
       src/toolchain.cpp \
       src/terminal_common.cpp \
       $(TERM_SRC)
OBJ := $(SRC:.cpp=.o)

ed1: $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

src/main.o:     src/editor.h src/buffer.h src/compile.h src/terminal.h src/indent.h \
                src/menu.h src/tree.h
src/editor.o:   src/editor.h src/buffer.h src/compile.h src/terminal.h src/indent.h \
                src/menu.h src/tree.h
src/indent.o:   src/indent.h
src/menu.o:     src/menu.h src/terminal.h
src/tree.o:     src/tree.h
src/syntax.o:   src/syntax.h
src/toolchain.o: src/toolchain.h
src/compile.o:  src/compile.h src/toolchain.h
src/buffer.o:   src/buffer.h
src/compile.o:  src/compile.h
src/terminal.o: src/terminal.h
src/terminal_common.o: src/terminal.h
src/terminal_win.o: src/terminal.h

# The two pieces with a contract: the layout rules, and the reading of cc1's
# diagnostic - which has to cope with a Windows path whose drive letter is
# followed by a colon that is not a separator.
test: tests/test
	./tests/test

tests/test: tests/test.cpp src/compile.cpp src/indent.cpp src/syntax.cpp \
            src/toolchain.cpp src/compile.h src/indent.h src/syntax.h
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/test.cpp src/compile.cpp src/indent.cpp \
	    src/syntax.cpp src/toolchain.cpp

clean:
	rm -f $(OBJ) src/terminal.o src/terminal_win.o src/terminal_common.o ed1 tests/test

.PHONY: test clean
