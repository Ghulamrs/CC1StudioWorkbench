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

#include "shalimar/session.h"

namespace editor {

// A tab is eight columns because that is what the terminal itself does with
// one. An editor that disagreed with its own screen would put the caret cc1
// reports in the wrong place.
const size_t kTabStop = 8;

// One open file: its text, and where the caret and the view were when you last
// looked at it. Switching tabs has to put all of that back, not just the text -
// a tab that forgot where you were reading would be worse than no tabs.
// The characters the screen is framed with. Two sets: the box-drawing ones,
// and plain ASCII for a console whose font draws the junctions from a
// different face than the lines - where the frame appears to break at every
// tee, and no amount of care here can mend it.
struct Frame {
    const char* across;
    const char* down;
    const char* topLeft;
    const char* topRight;
    const char* footLeft;
    const char* footRight;
    const char* teeDown;
    const char* teeUp;
    const char* teeRight;
    const char* teeLeft;
};

extern const Frame kBoxFrame;
extern const Frame kPlainFrame;

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
    void setShc(const std::string& path) { tool_.shc = path; }
    void setToolchain(ToolchainKind kind) { tool_.kind = kind; }
    void setConfig(Configuration config) { config_ = config; }
    // The dialect belongs to the file being edited and the rest of the style
    // to the project, so a project's settings arriving must not take the
    // language's layout rules with them.
    // Settles what the file is read as - the menu's answer if it gave one,
    // and the name's otherwise - and takes the layout rules with it.
    void applyLanguage();

    void setStyle(const IndentStyle& style) {
        IndentDialect dialect = style_.dialect;
        style_ = style;
        style_.dialect = dialect;
    }
    // Applied one at a time, after the project has been read, so that a flag
    // overrides the project without wiping the settings it did not mention.
    void setIndentWidth(size_t width) { style_.width = width; }
    void setTabs(bool tabs) { style_.tabs = tabs; }
    // Plain ASCII for the frame, for a console that draws the box characters
    // from more than one font.
    void setPlainFrame(bool plain) { frame_ = plain ? &kPlainFrame : &kBoxFrame; }
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

    // How many open files have unsaved changes, and the name of the first of
    // them; and whether leaving may go ahead, which is the question both ways
    // out have to ask.
    size_t unsaved(std::string& named) const;
    bool mayLeave();
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
    // Whether a program is standing still under either of the two, and which
    // one it is. Every place that used to ask debugger_.running() has to ask
    // this instead, because "is something being debugged" stopped being a
    // question with one place to look the moment Shalimar could be stopped.
    bool debugging() const;
    bool debuggingShalimar() const;
    // Where a file a debugger named can actually be opened from, and how to
    // put text that has newlines in it into a panel that holds lines.
    std::string whereThatFileIs(const std::string& named) const;
    static void sayLines(std::vector<std::string>& into, const std::string& text);

    void showStop(const Stop& where);
    bool breakpointOn(size_t line) const;
    void openSelected();
    void goToProblem();
    void goToFrame();
    void lookAlongStack(int by);
    void lookAt(size_t which);
    void editVariable(size_t which);
    void watchExpression();
    void editWatch(size_t which);
    void writeDebugTab();

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
    // Said after "ready", so it is the last thing on the line and not buried
    // under it: the settings are back to their defaults and the old file is
    // still there.
    void sayIfSettingsWereBad();
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
    // What the Language menu was told, or LangCount for 'by extension'. It
    // outlives a file switch on purpose: someone who says a .txt is Shalimar
    // is usually about to open another one.
    Language langChoice_ = LangCount;
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
    //
    // Filed under path::oneName, and holding the name as it was written beside
    // the lines: one file has one entry however its path was spelled, and the
    // debugger is still told the name a person would recognise rather than the
    // flattened one used to find it.
    struct FileBreaks {
        std::string path;
        std::set<size_t> lines;
    };
    Debugger debugger_;
    // The other half of stopping a program, and it is a different half rather
    // than a second copy: a Shalimar program stops itself, so there is no gdb,
    // lldb or cdb here and nothing of debugger_ that could have been extended
    // to do it - src/shalimar/README.md says why. Which of the two is live is
    // asked of them rather than written down, so there is no third thing that
    // can fall out of step with what is actually running.
    shalimar::Session shm_;
    std::map<std::string, FileBreaks> breaks_;
    Built debugBuilt_;
    // Whether that program is the editor's own temporary one, and so the
    // editor's to remove when the debugger stops. The project's program is the
    // project's, and stays where it was built.
    bool debugTemporary_;
    std::string stopFile_;
    size_t stopLine_;            // 0 when the program is not standing still
    std::vector<Variable> locals_;
    std::vector<StackFrame> stack_;   // and how it got to where it is standing
    std::string stopFunction_;        // what it stopped in, for writing the tab again
    size_t looking_;                  // which frame the variables belong to; 0 is the stop

    bool marked_;             // whether one end of a selection has been put down
    size_t markRow_, markCol_;
    std::string clipboard_;   // the editor's own, not the machine's

    std::string needle_;      // what was last searched for
    std::string message_;
    int quitConfirm_;
    bool running_;

    const Frame* frame_;

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
