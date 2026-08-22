#include "about.h"

namespace editor {
namespace about {

const char* name() { return "RStudio"; }
const char* version() { return "1.0"; }

std::vector<std::string> lines() {
    std::vector<std::string> said;
    said.push_back(std::string(name()) + " " + version());
    said.push_back("");
    said.push_back("C and C++ through cc1 and cl, Shalimar through shc.");
    said.push_back("ed1, WinConsole and ed1gui: three windows on one core.");
    said.push_back("");
    said.push_back("Copyright (c) 2026 G. R. Akhtar");
    said.push_back("Islamabad, Pakistan");
    return said;
}

}  // namespace about
}  // namespace editor
