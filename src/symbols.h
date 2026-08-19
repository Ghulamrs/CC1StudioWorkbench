#ifndef EDITOR_SYMBOLS_H
#define EDITOR_SYMBOLS_H

#include <cstddef>
#include <string>
#include <vector>

namespace editor {

// What a build produced, read out of the assembly it produced it as.
//
// This is not debug information and does not pretend to be. cc1 emits none at
// all - no -g, no DWARF, no CodeView - so there is nothing for a debugger to
// read and nothing to step through. What there is, always, is the assembly:
// which functions came out, how much stack each one takes, what it calls that
// it did not define, and what strings ended up in the binary. That is what you
// look at when there is no debugger, so that is what this finds.
//
// It reads both spellings, because cc1 writes GNU on two targets and MASM on
// the third, and cl writes MASM as well.
const unsigned char SymbolFunction = 0;
const unsigned char SymbolExported = 1;
const unsigned char SymbolExternal = 2;
const unsigned char SymbolText     = 3;

struct Symbol {
    unsigned char kind = SymbolFunction;
    std::string name;
    std::string detail;   // the stack a function takes, or what a string says
    size_t line = 0;      // where it is in the assembly, counting from 1
};

std::vector<Symbol> symbolsIn(const std::vector<std::string>& assembly);

// A C++ name in a listing is decorated - cl writes ?reset@?$Owned@VCounter@... -
// and only the platform's own library knows how to read it back. Whoever does
// know installs it here; left alone, names are shown exactly as they appear.
typedef std::string (*Demangler)(const std::string& decorated);
void setDemangler(Demangler how);

// Installs the platform's, where there is one. Does nothing off Windows, and
// is called by both front ends rather than happening by itself - a static that
// runs on its own is the thing that broke the mixed-mode build.
void installPlatformDemangler();

// The same, laid out for a panel: a heading per kind and one line each.
std::vector<std::string> describe(const std::vector<Symbol>& symbols);

}  // namespace editor

#endif
