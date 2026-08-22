#ifndef EDITOR_MENU_H
#define EDITOR_MENU_H

#include <cstddef>
#include <string>
#include <vector>

namespace editor {

// What a menu item asks for. The menu knows nothing about how any of it is
// done - it returns one of these and the editor carries it out, which is what
// keeps the same command reachable from a key and from the menu without the
// two ever disagreeing.
enum Action {
    ActionNone = 0,
    ActionNew,
    ActionOpen,
    ActionSave,
    ActionSaveAs,
    ActionQuit,
    ActionCloseFile,
    ActionNextFile,
    ActionPrevFile,
    ActionUndo,
    ActionCut,
    ActionCopy,
    ActionPaste,
    ActionSelectAll,
    ActionRedo,
    ActionLayOut,
    ActionFind,
    ActionFindNext,
    ActionFindPrevious,
    ActionReplace,
    ActionToggleTree,
    ActionTogglePanel,
    ActionToggleNumbers,
    ActionTogglePlain,
    ActionProjectNew,
    ActionProjectSave,
    ActionProjectAdd,
    ActionFileCreate,
    ActionFileRename,
    ActionFileDelete,
    ActionFileRegroup,
    ActionBuild,
    ActionRun,
    // The project's program, as against the file in front of you. Two commands
    // rather than one that guesses: which of them you meant is said by which
    // one you press, and neither depends on the other being unavailable.
    ActionBuildProject,
    ActionRunProject,
    ActionToggleBreak,
    ActionDebug,
    ActionDebugProject,
    ActionStepOver,
    ActionStepInto,
    ActionStepOut,
    ActionFrameUp,
    ActionFrameDown,
    ActionWatch,
    ActionDebugStop,
    ActionConfigDebug,
    ActionConfigRelease,
    ActionShowConsole,
    ActionShowDebug,
    ActionShowAssembly,
    ActionArchWindows,
    ActionArchLinux,
    ActionArchDarwin,
    ActionLangAuto,
    ActionLangC,
    ActionLangCpp,
    ActionLangShalimar,
    ActionLangText,
    ActionToolAuto,
    ActionToolCc1,
    ActionToolMsvc,
    ActionToolShc,
    ActionToolCxx,
    ActionHelpContents,
    ActionKeys,
    ActionAbout
};

struct MenuItem {
    std::string label;
    std::string key;     // what to show on the right, or empty
    Action action;
    // A rule across the box rather than an item: it groups what is above it
    // from what is below and cannot be chosen or landed on.
    bool rule;

    MenuItem(const std::string& text = std::string(),
             const std::string& shortcut = std::string(),
             Action what = ActionNone)
        : label(text), key(shortcut), action(what), rule(false) {}
};

// One of those, for a menu that wants a line drawn in it.
MenuItem separator();

struct MenuColumn {
    std::string title;
    std::vector<MenuItem> items;
};

// The menu bar along the top, and the list that drops out of it. Closed, it is
// a row of words; open, it takes the keyboard until it is finished with.
class Menu {
public:
    Menu();

    const std::vector<MenuColumn>& columns() const { return columns_; }

    bool active() const { return active_; }
    bool dropped() const { return active_ && dropped_; }
    size_t column() const { return column_; }
    size_t item() const { return item_; }

    void open();
    void close();

    // Where a column's title starts on the bar, in screen columns.
    size_t titleAt(size_t index) const;
    size_t barWidth() const;

    // Handles a key while the menu has the keyboard. Returns the action chosen,
    // or ActionNone if the key only moved about (or closed the menu).
    Action key(int k);

    // The items that cannot be chosen just now, named by the editor before the
    // menu opens - a Shalimar program has no call stack to walk and nothing to
    // watch, so those three are not offered while one is stopped.
    //
    // Told to the menu rather than worked out by it: the menu knows about
    // labels and keys and nothing about debuggers, which is what keeps it
    // usable from both front ends.
    void disable(const std::vector<Action>& actions);
    bool disabled(Action action) const;

    // Whether an item can be landed on at all: a rule never can, and a
    // disabled item never can, so up and down step over both.
    bool selectable(const MenuItem& item) const;

private:
    std::vector<MenuColumn> columns_;
    bool active_;
    bool dropped_;
    size_t column_;
    size_t item_;
    std::vector<Action> disabled_;
};

}  // namespace editor

#endif
