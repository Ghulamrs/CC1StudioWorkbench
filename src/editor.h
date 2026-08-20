#ifndef EDITOR_EDITOR_H
#define EDITOR_EDITOR_H

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "buffer.h"
#include "compile.h"
#include "debugger.h"
#include "find.h"
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

    // The first file the project lists, opened when nothing was named on the
    // command line - so the editor comes up with something in it rather than
    // with an empty sheet.
    void openFirstFile();
    void setCc1(const std::string& path) { tool_.cc1 = path; }
    void setCl(const std::string& path) { tool_.cl = path; }
    void setToolchain(ToolchainKind kind) { tool_.kind = kind; }
    void setConfig(Configuration config) { config_ = config; }
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
    void drawFrameTop(std::string& out) const;
    void drawBody(std::string& out) const;
    void drawPanel(std::string& out) const;
    void drawFrameFoot(std::string& out) const;
    void drawStatus(std::string& out) const;
    void drawMessage(std::string& out) const;
    void drawDropdown(std::string& out, std::vector<size_t>& covered) const;
    void drawDialog(std::string& out, std::vector<size_t>& covered) const;
    void placeCursor(std::string& out) const;

    // A line across the screen with the ends and the junctions named, and
    // room in it for the labels that belong on that line.
    std::string rule(const char* left, const char* right, const char* junction,
                     const std::string& labels, int labelColumns,
                     const std::string& tail, int tailColumns) const;

    // The screen as rows, and the rows put on it. Only the rows that differ
    // from the last time are written, which is what stops it flickering.
    void present(const std::vector<std::string>& rows);

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
    void undoEdit();
    bool selection(Range& range) const;
    bool selectionOn(size_t row, size_t& from, size_t& to) const;
    void extendTo(int key);
    void dropSelection() { marked_ = false; }
    bool eraseSelection();
    void copySelection(bool cut);
    void pasteClipboard();
    void selectAll();
    void redoEdit();
    void tabKey();
    void reindentAll();
    void findPrompt();
    void findAgain(bool forwards);
    void replacePrompt();

    bool save();
    void saveAs();
    void openPrompt();
    void newFile();
    void compile();
    void buildAndRun();

    // The project's own build: the program it says it is, out of the sources
    // it says make it. Separate from everything above on purpose - compiling
    // the file in front of you never needed a project open, and a project
    // being open does not take that away.
    void buildProject(bool andRun);
    bool saveEveryDirty();

    // Stopping the program and walking through it. The debugger is a child
    // process that outlives each of these calls, which is what makes this a
    // session rather than a command.
    void toggleBreak();
    // Starts it, or carries on from where it stopped. `project` chooses what
    // is put under the debugger: the file in front of you, or the program the
    // project says it builds - the same two things Ctrl-B and F4 choose
    // between, asked the same way and never guessed.
    void debug(bool project);
    void debugStep(Action how);
    void debugStop();
    void showStop(const Stop& where);
    bool breakpointOn(size_t line) const;
    void openSelected();
    void goToProblem();

    void refreshTree();
    void applyProject();
    std::string targetFile() const;
    std::string groupUnderCursor() const;
    void createFile();
    void renameFile();
    void deleteFile();
    void regroupFile();
    void addToProject();
    void newProject();
    void saveProject();
    void resetDebug();
    void showKeys();
    void showAbout();

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
    Configuration config_;
    size_t arch_;
    bool numbers_;
    bool needsDraw_;
    // Where the program is to stop, by file and by line counting from one, and
    // where it actually is once it has. Kept by file rather than by buffer so
    // that a breakpoint survives the file being closed and opened again.
    Debugger debugger_;
    std::map<std::string, std::set<size_t> > breaks_;
    Built debugBuilt_;
    // Whether that program is the editor's own temporary one, and so the
    // editor's to remove when the debugger stops. The project's program is the
    // project's, and stays where it was built.
    bool debugTemporary_;
    std::string stopFile_;
    size_t stopLine_;            // 0 when the program is not standing still
    std::vector<Variable> locals_;

    bool marked_;             // whether one end of a selection has been put down
    size_t markRow_, markCol_;
    std::string clipboard_;   // the editor's own, not the machine's

    std::string needle_;      // what was last searched for
    std::string message_;
    int quitConfirm_;
    bool running_;

    int screenRows_, screenCols_;
    int bodyRows_, panelRows_;
    int treeCols_, sourceCols_, gutterCols_;

    // The last screen written, row by row, and the width it was written at.
    // A row that has not changed is not written again.
    std::vector<std::string> painted_;
    int paintedCols_;

    // A question being asked in a box of its own, rather than on the message
    // line where it used to be. Empty title means nothing is being asked.
    std::string askTitle_;
    std::string askAnswer_;
};

}  // namespace editor

#endif
