#ifndef EDITOR_EDITOR_H
#define EDITOR_EDITOR_H

#include <cstddef>
#include <string>
#include <vector>

#include "buffer.h"
#include "compile.h"
#include "indent.h"
#include "menu.h"
#include "project.h"
#include "syntax.h"
#include "toolchain.h"
#include "terminal.h"
#include "tree.h"

namespace editor {

// A tab is eight columns because that is what the terminal itself does with
// one. An editor that disagreed with its own screen would put the caret cc1
// reports in the wrong place.
const size_t kTabStop = 8;

// One open file: its text, and where the caret and the view were when you last
// looked at it. Switching tabs has to put all of that back, not just the text -
// a tab that forgot where you were reading would be worse than no tabs.
struct Document {
    Buffer buf;
    size_t cx = 0, cy = 0, rowoff = 0, coloff = 0;
    Language lang = LangPlain;
};

class Editor {
public:
    Editor();

    void open(const std::string& path);
    void closeDocument();
    void switchTo(size_t index);
    void nextDocument(int by);
    void openProject(const std::string& path);
    void setCc1(const std::string& path) { tool_.cc1 = path; }
    void setCl(const std::string& path) { tool_.cl = path; }
    void setToolchain(ToolchainKind kind) { tool_.kind = kind; }
    void setStyle(const IndentStyle& style) { style_ = style; }
    // Applied one at a time, after the project has been read, so that a flag
    // overrides the project without wiping the settings it did not mention.
    void setIndentWidth(size_t width) { style_.width = width; }
    void setTabs(bool tabs) { style_.tabs = tabs; }
    void setCaseIndent(size_t levels) { style_.caseIndent = levels; }
    void run();

    // Where the console's lines come from while cc1 is running.
    void console(const std::string& line);

private:
    enum Focus { FocusText, FocusTree, FocusPanel };
    enum Tab { TabConsole, TabDebug, TabAssembly, TabCount };

    void layout();
    void scroll();
    void refresh();
    void drawMenuBar(std::string& out) const;
    void drawTabs(std::string& out) const;
    void drawBody(std::string& out) const;
    void drawPanel(std::string& out) const;
    void drawStatus(std::string& out) const;
    void drawMessage(std::string& out) const;
    void drawDropdown(std::string& out) const;
    void placeCursor(std::string& out) const;

    void processKey(int key);
    void perform(Action action);
    void moveCursor(int key);
    void moveTree(int key);
    void movePanel(int key);
    void cycleFocus();

    void insertChar(char c);
    void insertNewline();
    void backspace();
    void deleteForward();
    void realign();
    void tabKey();
    void reindentAll();

    bool save();
    void saveAs();
    void openPrompt();
    void newFile();
    void compile();
    void openSelected();
    void goToProblem();

    void refreshTree();
    void applyProject();
    std::string targetFile() const;
    void createFile();
    void renameFile();
    void deleteFile();
    void regroupFile();
    void addToProject();
    void newProject();
    void saveProject();
    void resetDebug();
    void showKeys();

    void stash();
    void restore();
    size_t findDocument(const std::string& path) const;

    std::string prompt(const std::string& text, bool& cancelled);
    void say(const std::string& text) { message_ = text; }
    size_t renderCol(const std::string& line, size_t col) const;
    void clampCursor();
    const std::vector<std::string>& panelLines() const;

    Terminal term_;

    // The active copy. It is written back into docs_[doc_] whenever the tab
    // changes, which keeps every other line in this file reading the way it did
    // when there was only ever one file open.
    Buffer buf_;
    std::vector<Document> docs_;
    size_t doc_;
    Menu menu_;
    Tree tree_;
    Project project_;
    std::string projectDir_;
    IndentStyle style_;
    Toolchain tool_;

    size_t cx_, cy_, rx_;
    size_t rowoff_, coloff_;

    size_t treeSel_, treeOff_;
    bool treeOpen_;

    std::vector<std::string> console_;   // the command, its output, its errors
    std::vector<std::string> debug_;     // variables, once there are any to show
    std::vector<std::string> assembly_;
    Diagnostic lastDiag_;
    size_t panelOff_;
    bool panelOpen_;
    Tab tab_;

    Focus focus_;
    Language lang_;
    size_t arch_;
    bool numbers_;
    bool needsDraw_;
    std::string message_;
    int quitConfirm_;
    bool running_;

    int screenRows_, screenCols_;
    int bodyRows_, panelRows_;
    int treeCols_, sourceCols_, gutterCols_;
};

}  // namespace editor

#endif
