#include "menu.h"

#include "terminal.h"

namespace editor {

Menu::Menu() : active_(false), dropped_(false), column_(0), item_(0) {
    MenuColumn file;
    file.title = "File";
    file.items.push_back({"New", "", ActionNew});
    file.items.push_back({"Open...", "", ActionOpen});
    file.items.push_back({"Save", "Ctrl-S", ActionSave});
    file.items.push_back({"Save As...", "", ActionSaveAs});
    file.items.push_back({"Close", "", ActionCloseFile});
    file.items.push_back({"Next file", "F3", ActionNextFile});
    file.items.push_back({"Previous file", "F2", ActionPrevFile});
    file.items.push_back({"Quit", "Ctrl-Q", ActionQuit});
    columns_.push_back(file);

    MenuColumn edit;
    edit.title = "Edit";
    edit.items.push_back({"Undo", "Ctrl-Z", ActionUndo});
    edit.items.push_back({"Redo", "Ctrl-Y", ActionRedo});
    edit.items.push_back({"Cut", "Ctrl-X", ActionCut});
    edit.items.push_back({"Copy", "Ctrl-C", ActionCopy});
    edit.items.push_back({"Paste", "Ctrl-V", ActionPaste});
    edit.items.push_back({"Select all", "", ActionSelectAll});
    edit.items.push_back({"Find...", "Ctrl-F", ActionFind});
    edit.items.push_back({"Find next", "Ctrl-G", ActionFindNext});
    edit.items.push_back({"Find previous", "", ActionFindPrevious});
    edit.items.push_back({"Replace...", "Ctrl-R", ActionReplace});
    edit.items.push_back({"Lay out file", "Ctrl-A", ActionLayOut});
    edit.items.push_back({"Project pane", "Ctrl-P", ActionToggleTree});
    edit.items.push_back({"Bottom panel", "Ctrl-E", ActionTogglePanel});
    edit.items.push_back({"Line numbers", "Ctrl-L", ActionToggleNumbers});
    columns_.push_back(edit);

    // Everything that changes what the project holds, in one place. The file
    // commands act on whatever the project pane is standing on, or on the file
    // being edited when it is not.
    MenuColumn project;
    project.title = "Project";
    project.items.push_back({"New project", "", ActionProjectNew});
    project.items.push_back({"Save project", "", ActionProjectSave});
    project.items.push_back({"Add this file", "", ActionProjectAdd});
    project.items.push_back({"New file...", "", ActionFileCreate});
    project.items.push_back({"Rename...", "", ActionFileRename});
    project.items.push_back({"Move to group...", "", ActionFileRegroup});
    project.items.push_back({"Delete...", "", ActionFileDelete});
    columns_.push_back(project);

    MenuColumn build;
    build.title = "Build";
    build.items.push_back({"Compile file", "Ctrl-B", ActionBuild});
    build.items.push_back({"Run file", "F5", ActionRun});
    build.items.push_back({"Build project", "F4", ActionBuildProject});
    build.items.push_back({"Run project", "", ActionRunProject});
    build.items.push_back({"Debug", "Ctrl-D", ActionConfigDebug});
    build.items.push_back({"Release", "Ctrl-D", ActionConfigRelease});
    build.items.push_back({"Console", "", ActionShowConsole});
    build.items.push_back({"Debug", "", ActionShowDebug});
    build.items.push_back({"Assembly", "", ActionShowAssembly});
    columns_.push_back(build);

    // Stopping the program and walking through it. Its own column because it is
    // a mode rather than a command: once it has started, most of what the
    // editor does next is one of these.
    MenuColumn debug;
    debug.title = "Debug";
    debug.items.push_back({"Start / continue", "F8", ActionDebug});
    debug.items.push_back({"Toggle breakpoint", "F9", ActionToggleBreak});
    debug.items.push_back({"Step over", "F7", ActionStepOver});
    debug.items.push_back({"Step into", "F6", ActionStepInto});
    debug.items.push_back({"Step out", "", ActionStepOut});
    debug.items.push_back({"Stop debugging", "", ActionDebugStop});
    columns_.push_back(debug);

    // The three cc1 generates for. Two of them reach -S and no further on any
    // given machine, which is the whole reason the assembly tab exists.
    MenuColumn target;
    target.title = "Target";
    target.items.push_back({"x86_64-windows", "", ActionArchWindows});
    target.items.push_back({"x86_64-linux", "", ActionArchLinux});
    target.items.push_back({"arm64-darwin", "", ActionArchDarwin});
    columns_.push_back(target);

    // Which compiler is driven. cc1 is what this was written for; cl is here
    // because the machine it runs on already has it, and because a front end
    // that can only speak to one compiler is a narrower thing than it needs to
    // be.
    MenuColumn tools;
    tools.title = "Tools";
    tools.items.push_back({"By language", "Ctrl-K", ActionToolAuto});
    tools.items.push_back({"cc1", "", ActionToolCc1});
    tools.items.push_back({"MSVC (cl)", "", ActionToolMsvc});
    columns_.push_back(tools);

    MenuColumn help;
    help.title = "Help";
    help.items.push_back({"Keys", "", ActionKeys});
    help.items.push_back({"About", "", ActionAbout});
    columns_.push_back(help);
}

void Menu::open() {
    active_ = true;
    dropped_ = true;
    item_ = 0;
}

void Menu::close() {
    active_ = false;
    dropped_ = false;
    item_ = 0;
}

size_t Menu::titleAt(size_t index) const {
    size_t at = 1;
    for (size_t i = 0; i < index && i < columns_.size(); ++i)
        at += columns_[i].title.size() + 3;
    return at;
}

size_t Menu::barWidth() const {
    return columns_.empty() ? 0 : titleAt(columns_.size());
}

Action Menu::key(int k) {
    if (!active_) return ActionNone;

    const MenuColumn& col = columns_[column_];

    switch (k) {
        case '\x1b':
            close();
            return ActionNone;

        case KEY_ARROW_LEFT:
            column_ = (column_ == 0) ? columns_.size() - 1 : column_ - 1;
            item_ = 0;
            return ActionNone;

        case KEY_ARROW_RIGHT:
            column_ = (column_ + 1) % columns_.size();
            item_ = 0;
            return ActionNone;

        case KEY_ARROW_UP:
            item_ = (item_ == 0) ? col.items.size() - 1 : item_ - 1;
            return ActionNone;

        case KEY_ARROW_DOWN:
            item_ = (item_ + 1) % col.items.size();
            return ActionNone;

        case KEY_HOME:
            item_ = 0;
            return ActionNone;

        case KEY_END:
            item_ = col.items.size() - 1;
            return ActionNone;

        case '\r':
        case '\n': {
            Action chosen = col.items[item_].action;
            close();
            return chosen;
        }

        default:
            break;
    }

    // A letter jumps to the column whose title begins with it, which is how a
    // menu bar has always been driven.
    if (k >= 32 && k < 127) {
        char want = static_cast<char>(k);
        if (want >= 'A' && want <= 'Z') want = static_cast<char>(want - 'A' + 'a');
        for (size_t i = 0; i < columns_.size(); ++i) {
            char first = columns_[i].title.empty() ? 0 : columns_[i].title[0];
            if (first >= 'A' && first <= 'Z') first = static_cast<char>(first - 'A' + 'a');
            if (first == want) {
                column_ = i;
                item_ = 0;
                return ActionNone;
            }
        }
    }

    return ActionNone;
}

}  // namespace editor
