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
    ActionLayOut,
    ActionToggleTree,
    ActionTogglePanel,
    ActionToggleNumbers,
    ActionBuild,
    ActionShowConsole,
    ActionShowDebug,
    ActionShowAssembly,
    ActionArchWindows,
    ActionArchLinux,
    ActionArchDarwin,
    ActionToolAuto,
    ActionToolCc1,
    ActionToolMsvc,
    ActionKeys
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
