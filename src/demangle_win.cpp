// Reading a decorated C++ name back into something a person can read.
//
// One file, compiled into both front ends, because two copies of it would be
// two things to keep in step - and this is the only place either of them needs
// a platform library for anything but the terminal itself.

#include "symbols.h"

#ifdef _WIN32

#include <windows.h>
// After windows.h, which it needs.
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

namespace editor {

namespace {

std::string readable(const std::string& decorated) {
    char plain[1024];
    // Without the leading underscore rules and the calling convention, which
    // are noise in a list of what a build produced.
    DWORD flags = UNDNAME_NO_MS_KEYWORDS | UNDNAME_NO_FUNCTION_RETURNS |
                  UNDNAME_NO_ALLOCATION_MODEL | UNDNAME_NO_ALLOCATION_LANGUAGE |
                  UNDNAME_NO_ACCESS_SPECIFIERS | UNDNAME_NO_MEMBER_TYPE;

    DWORD got = UnDecorateSymbolName(decorated.c_str(), plain,
                                     static_cast<DWORD>(sizeof plain), flags);
    if (got == 0) return decorated;   // not a name it knows; leave it alone
    return std::string(plain, got);
}

}  // namespace

void installPlatformDemangler() { setDemangler(readable); }

}  // namespace editor

#else

namespace editor {
// Nothing here knows how to read a Microsoft name, so nothing pretends to.
void installPlatformDemangler() {}
}  // namespace editor

#endif
