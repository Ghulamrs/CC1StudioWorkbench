#include "about.h"

namespace editor {
namespace about {

const char* name() { return "CC1 Studio Workbench"; }
const char* version() { return "1.1"; }

std::vector<std::string> lines() {
    std::vector<std::string> said;
    said.push_back(std::string(name()) + " " + version());
    said.push_back("");
    said.push_back("An editor for the cc1 compiler, and for cl beside it.");
    said.push_back("ed1 is the terminal half and ed1gui the window, over one core.");
    said.push_back("");
    said.push_back("Copyright (c) 2026 G. R. Akhtar");
    said.push_back("Islamabad, Pakistan");
    return said;
}

}  // namespace about
}  // namespace editor
