#include "help.h"

#include "about.h"

namespace editor {
namespace help {

namespace {

const Page kPages[] = {
    {"1",  "What it is",       "three languages, three variants, one core",     "01-what-it-is.md"},
    {"2",  "Getting started",  "first run, the demo, where things land",        "02-getting-started.md"},
    {"3",  "The screen",       "panes, tabs, the status bar, what is where",    "03-the-screen.md"},
    {"4",  "Editing",          "indenting, UTF-8, undo, selection, clipboard",  "04-editing.md"},
    {"5",  "Finding",          "find, find again, replace, what counts",        "05-finding.md"},
    {"6",  "The project",      "ed1.json, groups, a compiler per group",        "06-the-project.md"},
    {"7",  "Building",         "file or project, targets, debug and release",   "07-building.md"},
    {"8",  "Debugging",        "breakpoints, stepping, variables, the stack",   "08-debugging.md"},
    {"9",  "The panel",        "Console, Debug, Assembly, enter on a line",     "09-the-panel.md"},
    {"10", "Keys",            "every key, every menu, every flag",             "10-keys.md"},
    {"",   "C",                "cc1, three targets, DWARF on two of them",      "c.md"},
    {"",   "C++",              "cl on Windows, clang++ or g++ elsewhere",       "cpp.md"},
    {"",   "Shalimar",         "shc, the indent dialect, stopping itself",      "shalimar.md"},
    {"",   "Appendix A",       "the Shalimar language, in full",                "appendix-a-shalimar-language.md"},
};

// Padded to a fixed width so the third column lines up. Written out rather
// than computed from the longest title: the panel is eighty columns and the
// widths were chosen to fit it, so a title that would break the layout should
// be shortened rather than silently pushing the line off the edge.
std::string column(const std::string& text, size_t width) {
    std::string out = text;
    while (out.size() < width) out += ' ';
    return out;
}

}  // namespace

const std::vector<Page>& pages() {
    static const std::vector<Page> all(kPages, kPages + sizeof kPages / sizeof kPages[0]);
    return all;
}

std::vector<std::string> contents() {
    std::vector<std::string> said;
    said.push_back(std::string(about::name()) + " " + about::version() + " - the manual");
    said.push_back("");

    bool languagesStarted = false;
    const std::vector<Page>& all = pages();
    for (size_t i = 0; i < all.size(); ++i) {
        // The numbered pages are about the editor and the rest are about one
        // language each. A blank line between them says so without a heading,
        // which the panel has no room for.
        if (!languagesStarted && all[i].number[0] == '\0') {
            languagesStarted = true;
            said.push_back("");
        }
        std::string number = all[i].number;
        said.push_back("  " + column(number.empty() ? std::string() : number + ".", 4) +
                       column(all[i].title, 18) + all[i].about);
    }

    said.push_back("");
    said.push_back("The pages are Markdown in help/, beside the source. F1 shows the keys.");
    return said;
}

}  // namespace help
}  // namespace editor
