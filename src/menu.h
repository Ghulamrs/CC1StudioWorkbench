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
    ActionDebugStop,
    ActionConfigDebug,
    ActionConfigRelease,
    ActionShowConsole,
    ActionShowDebug,
    ActionShowAssembly,
    ActionArchWindows,
    ActionArchLinux,
    ActionArchDarwin,
    ActionToolAuto,
    ActionToolCc1,
    ActionToolMsvc,
    ActionKeys,
    ActionAbout
};

struct MenuItem {
    std::string label;
    std::string key;     // what to show on the right, or empty
    Action action;
};

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

private:
    std::vector<MenuColumn> columns_;
    bool active_;
    bool dropped_;
    size_t column_;
    size_t item_;
};

}  // namespace editor

#endif
