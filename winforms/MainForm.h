#pragma once

// The Windows Forms front end.
//
// It includes bridge.h and nothing else of ours. No <string>, no <vector>, no
// editor headers - because a /clr translation unit that instantiates the same
// templates the native files instantiate corrupts the heap before main runs.
// Everything below talks to the editor through plain C.

#include "bridge.h"

namespace ed1gui {

using namespace System;
using namespace System::Windows::Forms;

// One open file. The box keeps its own text, caret, scroll position and undo
// history, so switching tabs puts all of that back without the form having to
// remember any of it.
ref class Sheet {
public:
    String^ path;
    RichTextBox^ box;
    Panel^ gutter;
    TabPage^ page;
};

// The numbers down the left are repainted on every keystroke, and a plain
// Panel clears itself to its background before the Paint handler is called -
// which is the blink. This one draws the strip off screen and puts it down in
// one go.
ref class Gutter : public Panel {
public:
    Gutter() { DoubleBuffered = true; }
};

// Where a text box is scrolled to. Windows Forms does not say, and colouring
// has to put it back: selecting a run scrolls that run into view, and
// colouring a file selects every run in it.
value struct Spot {
    int x;
    int y;
};

public ref class MainForm : public Form {
public:
    MainForm() { Start(nullptr, nullptr); }
    MainForm(String^ projectDirectory, array<String^>^ files) {
        Start(projectDirectory, files);
    }

protected:
    // The text is what a person came to type in, so that is what has the
    // keyboard - but only once the window exists. Asked for in the constructor
    // it does nothing at all, because there is nothing yet to give it to.
    // Every sheet with unsaved work is asked about before the window goes,
    // and any Cancel stops it. File > Exit and the close button both arrive
    // here. Protected because what it overrides is: a managed override may not
    // be less accessible than the method it replaces.
    // Ctrl+D, Ctrl+K and Ctrl+T do not belong to one menu item: they move
    // between several, the way the terminal's do. A ToolStripMenuItem can own
    // only one meaning, and a key bound to two items goes to whichever menu was
    // built first while the other advertises a key that does nothing - which is
    // exactly what happened to F7 and Step over. So these are caught here and
    // shown on every item they move between with ShortcutKeyDisplayString.
    // Protected because what it overrides is.
    // The view moves when the window does, so the bar over the stopped line is
    // put back afterwards. Protected because what it overrides is.
    virtual void OnResize(EventArgs^ e) override {
        Form::OnResize(e);
        if (stopBar_ != nullptr) PlaceStopBar();
    }

    virtual bool ProcessCmdKey(System::Windows::Forms::Message% message, Keys keys) override {
        if (keys == static_cast<Keys>(Keys::Control | Keys::D)) { NextConfig(); return true; }
        if (keys == static_cast<Keys>(Keys::Control | Keys::K)) { NextTool(); return true; }
        if (keys == static_cast<Keys>(Keys::Control | Keys::T)) { NextTarget(); return true; }
        return Form::ProcessCmdKey(message, keys);
    }

    virtual void OnFormClosing(System::Windows::Forms::FormClosingEventArgs^ e) override {
        for (int i = 0; i < sheets_->Count; ++i)
            if (!MayDiscard(sheets_[i])) {
                e->Cancel = true;
                return;
            }
        Form::OnFormClosing(e);
    }

    virtual void OnShown(EventArgs^ e) override {
        Form::OnShown(e);
        Arrange();
        text_->Select(0, 0);
        text_->Focus();
        Recolour();

        // The window has a handle now. Anything opened before it had one - the
        // project's first file, or a file named on the command line - settled
        // its modified flag only at this moment, and nothing has been typed
        // yet, so none of it has changes.
        for (int i = 0; i < sheets_->Count; ++i) {
            sheets_[i]->box->Modified = false;
            MarkTab(sheets_[i]);
        }
    }

    // What the pair of programs is called. The binaries stay ed1 and ed1gui;
    // this is the name of the thing they are two halves of, and it is not
    // CC1 Studio, which is the VS Code extension for the same compiler.
    //
    // A function rather than a static string, because a global or static of a
    // managed type is refused outright - C3145 - since there would be nothing
    // rooting it for the collector. That is a fourth mixed-mode hazard to put
    // beside the three in the README.
    static String^ ProductName() { return "CC1 Studio Workbench"; }

    ~MainForm() { this->!MainForm(); }
    !MainForm() {
        if (project_ != nullptr) {
            ed1_project_free(project_);
            project_ = nullptr;
        }
        // A debugger left running would outlive the window that started it,
        // and the program it is attached to would be left in the temporary
        // directory. Freeing the handle removes both.
        if (built_ != nullptr) {
            ed1_program_free(built_);
            built_ = nullptr;
        }
        if (debugger_ != nullptr) {
            ed1_debugger_free(debugger_);
            debugger_ = nullptr;
        }
    }

private:
    Ed1Project* project_;

    // Everything the core needs to be told is kept as managed text and handed
    // over as UTF-8 at the moment of the call.
    String^ arch_;
    String^ cc1_;
    String^ cl_;
    int toolKind_;
    int config_;
    int indentWidth_;
    int indentTabs_;
    int indentCase_;

    // Stopping the program and walking through it. The breakpoints are the
    // editor's own note and are kept by file, so they survive a file being
    // closed and opened again; the debugger and the program it is attached to
    // are native and are freed in OnFormClosed.
    Ed1Debugger* debugger_;
    Ed1Program* built_;
    bool busy_;             // something slow is on another thread
    int pending_;           // which slow thing
    int workResult_;        // what it came back with
    int workKind_;          // and the compiler and language it was told to use
    int workLanguage_;
    // Breakpoints, filed under OneName so that one file has one set of them
    // however its path was spelled - the same rule that keeps one file to one
    // tab. breakNames_ holds the spelling to hand a debugger, since OneName is
    // flattened and lower-cased and is for finding things, never for showing
    // or for passing on. Editor::renameFile keeps the same pair in the core.
    System::Collections::Generic::Dictionary<String^,
        System::Collections::Generic::List<int>^>^ breaks_;
    System::Collections::Generic::Dictionary<String^, String^>^ breakNames_;
    // The last error a build reported, kept so it can be gone back to. The
    // window already jumps there when the build fails; this is for afterwards,
    // once you have moved away - which is what Enter on the console does in the
    // terminal. errorFile_ is null when the error is in the file that was
    // built, and named when a project build found it somewhere else.
    int errorLine_;
    int errorColumn_;
    String^ errorMessage_;
    String^ errorFile_;
    String^ stopFile_;
    int stopLine_;
    // The row wearing the stopped-here bar, so it can be taken off again. -1
    // when no line has one.
    int highlightRow_;
    // The bar itself: a strip laid over the line, the width of the view. A
    // RichTextBox will not colour past the end of a line's text - it paints its
    // own background and the only per-range colour it draws is behind
    // characters - so the bar is not asked of it. It is a window of our own,
    // made translucent and click-through, sitting on top.
    Panel^ stopBar_;
    // The font code is drawn in. One font for every tab: a file does not have
    // a typeface, the person reading it does. The gutter draws its numbers with
    // the box's own font, so it follows this without being told.
    System::Drawing::Font^ codeFont_;          // 0 when the program is not standing still

    String^ path_;
    String^ projectDirectory_;
    String^ needle_;      // what was last searched for
    bool colouring_;

    // The lexer's state at the start of one line, kept so that typing on that
    // line does not lex the whole file above it again for every character.
    // Typing on a line cannot change the state it began with; moving to
    // another line can, and OnCaretMoved throws it away.
    bool stateGood_;
    int stateRow_;
    int stateAt_;

    // Fires when typing stops. Colouring the line you are on keeps up with the
    // keyboard; what a typed quote or /* does to the lines below it can wait a
    // quarter of a second for this.
    Timer^ settle_;

    // Kept as fields because their proportions are set once the window has a
    // size, not while it is being built - see Arrange.
    SplitContainer^ outer_;
    SplitContainer^ upper_;

    TreeView^ tree_;
    TabControl^ files_;
    System::Collections::Generic::List<Sheet^>^ sheets_;
    // The box of whichever tab is in front. Kept as a field so that everything
    // written when there was only ever one file still reads the same.
    RichTextBox^ text_;
    TabControl^ panel_;
    // The three things View can hide, and the ticks that say which are showing.
    // numbers_ is the gutter as a whole - the line numbers, the breakpoint dots
    // and the arrow together - which is what Ctrl-L takes away in the terminal
    // too, rather than the digits alone.
    bool numbers_;
    // The radio choices, kept so their ticks can say which one is in force.
    // The terminal puts all three on its status bar and never has to be asked;
    // here the answer lives where the choice is made.
    System::Collections::Generic::List<ToolStripMenuItem^>^ targetItems_;
    ToolStripMenuItem^ toolAutoItem_;
    ToolStripMenuItem^ toolCc1Item_;
    ToolStripMenuItem^ toolClItem_;
    ToolStripMenuItem^ debugConfigItem_;
    ToolStripMenuItem^ releaseConfigItem_;
    ToolStripMenuItem^ numbersItem_;
    ToolStripMenuItem^ paneItem_;
    ToolStripMenuItem^ panelItem_;
    TextBox^ console_;
    TextBox^ debug_;
    RichTextBox^ assembly_;
    StatusStrip^ status_;
    ToolStripStatusLabel^ build_;
    ToolStripStatusLabel^ where_;
    ToolStripStatusLabel^ what_;
    ToolStripStatusLabel^ root_;   // which directory the project is in

    // ---- the seam ----------------------------------------------------------

    // UTF-8 and null-terminated, so it can be pinned and passed straight in.
    static array<Byte>^ Utf8Of(String^ text) {
        array<Byte>^ raw = System::Text::Encoding::UTF8->GetBytes(text == nullptr ? "" : text);
        array<Byte>^ out = gcnew array<Byte>(raw->Length + 1);
        Array::Copy(raw, out, raw->Length);
        return out;
    }

    static String^ FromUtf8(const char* text) {
        if (text == nullptr) return String::Empty;
        int length = 0;
        while (text[length] != '\0') ++length;
        if (length == 0) return String::Empty;

        array<Byte>^ bytes = gcnew array<Byte>(length);
        Runtime::InteropServices::Marshal::Copy(IntPtr(const_cast<char*>(text)), bytes, 0,
                                                length);
        return System::Text::Encoding::UTF8->GetString(bytes);
    }

    // A char* the core allocated, taken as a string and handed back to it.
    static String^ TakeUtf8(char* text) {
        String^ out = FromUtf8(text);
        ed1_free(text);
        return out;
    }

    // ---- building the window ----------------------------------------------

    void Start(String^ projectDirectory, array<String^>^ files) {
        project_ = ed1_project_new();
        arch_ = "x86_64-windows";

        // Beside the editor first, which is where the product puts cc1.exe,
        // and then PATH. $CC1 and $CL name one outright, as they do for the
        // terminal half: it reads them and this did not, so the same machine
        // could build from one and not from the other.
        cc1_ = Named("CC1", "cc1");
        cl_ = Named("CL", "cl");
        toolKind_ = ED1_TOOL_AUTO;
        config_ = ED1_CONFIG_DEBUG;
        debugger_ = ed1_debugger_new();
        built_ = nullptr;
        busy_ = false;
        pending_ = 0;
        workResult_ = 0;
        workKind_ = 0;
        workLanguage_ = 0;
        breaks_ = gcnew System::Collections::Generic::Dictionary<String^,
            System::Collections::Generic::List<int>^>();
        breakNames_ = gcnew System::Collections::Generic::Dictionary<String^, String^>();
        stopFile_ = nullptr;
        stopLine_ = 0;
        highlightRow_ = -1;
        stopBar_ = nullptr;
        codeFont_ = RememberedFont();
        numbers_ = true;
        ForgetError();
        indentWidth_ = 4;
        indentTabs_ = 0;
        indentCase_ = 0;

        Lay();

        // Told nothing, this used to open on an empty pane. The terminal half
        // opens on the project it was last in, and failing that makes a small
        // one in your own files - both of which the core does; only the asking
        // was missing here.
        if (projectDirectory == nullptr) {
            String^ last = FromUtf8(ed1_last_project());
            if (last->Length == 0) last = FromUtf8(ed1_demo_directory());
            if (last->Length > 0 && System::IO::Directory::Exists(last))
                projectDirectory = last;
        }

        if (projectDirectory != nullptr) LoadProject(projectDirectory);

        bool anyNamed = false;
        if (files != nullptr)
            for (int i = 0; i < files->Length; ++i)
                if (files[i] != nullptr) {
                    OpenPath(files[i]);
                    anyNamed = true;
                }

        // Nothing named on the command line: the project's own first file,
        // which is what the terminal half opens. A window on an empty untitled
        // sheet, with the project listed beside it, is an odd place to start.
        if (!anyNamed) OpenFirstOfProject();
    }

    void Lay() {
        Text = ProductName();
        Width = 1100;
        Height = 760;
        MinimumSize = System::Drawing::Size(840, 560);
        StartPosition = FormStartPosition::CenterScreen;
        DoubleBuffered = true;
        colouring_ = false;
        stateGood_ = false;
        stateRow_ = 0;
        stateAt_ = 0;

        settle_ = gcnew Timer();
        settle_->Interval = 250;
        settle_->Tick += gcnew EventHandler(this, &MainForm::OnSettled);

        MenuStrip^ bar = gcnew MenuStrip();

        ToolStripMenuItem^ file = gcnew ToolStripMenuItem("&File");
        file->DropDownItems->Add("New", nullptr,
                                 gcnew EventHandler(this, &MainForm::OnNewBuffer));
        file->DropDownItems->Add(
            Item("New file...", Keys::Control | Keys::N,
                 gcnew EventHandler(this, &MainForm::OnNewFile)));
        file->DropDownItems->Add("New project...", nullptr,
                                 gcnew EventHandler(this, &MainForm::OnNewProject));
        file->DropDownItems->Add(gcnew ToolStripSeparator());
        file->DropDownItems->Add("Open project...", nullptr,
                                 gcnew EventHandler(this, &MainForm::OnOpenProject));
        file->DropDownItems->Add("Open file...", nullptr,
                                 gcnew EventHandler(this, &MainForm::OnOpenFile));
        ToolStripMenuItem^ save = gcnew ToolStripMenuItem(
            "Save", nullptr, gcnew EventHandler(this, &MainForm::OnSave));
        save->ShortcutKeys = static_cast<Keys>(Keys::Control | Keys::S);
        file->DropDownItems->Add(save);
        file->DropDownItems->Add("Save as...", nullptr,
                                 gcnew EventHandler(this, &MainForm::OnSaveAs));
        file->DropDownItems->Add(
            Item("Close", Keys::Control | Keys::W, gcnew EventHandler(this, &MainForm::OnCloseFile)));
        file->DropDownItems->Add(gcnew ToolStripSeparator());
        // Ctrl+PageDown and Ctrl+PageUp rather than the terminal's F3 and F2,
        // which are Find next and Rename here. The user's call: the Windows
        // convention, and nothing already bound has to move for it.
        file->DropDownItems->Add(
            Item("Next file", Keys::Control | Keys::PageDown,
                 gcnew EventHandler(this, &MainForm::OnNextFile)));
        file->DropDownItems->Add(
            Item("Previous file", Keys::Control | Keys::PageUp,
                 gcnew EventHandler(this, &MainForm::OnPreviousFile)));
        file->DropDownItems->Add(
            Item("Exit", Keys::Control | Keys::Q, gcnew EventHandler(this, &MainForm::OnExit)));
        bar->Items->Add(file);

        ToolStripMenuItem^ edit = gcnew ToolStripMenuItem("&Edit");
        edit->DropDownItems->Add(Item("Undo", Keys::Control | Keys::Z, gcnew EventHandler(this, &MainForm::OnUndo)));
        edit->DropDownItems->Add(Item("Redo", Keys::Control | Keys::Y, gcnew EventHandler(this, &MainForm::OnRedo)));
        edit->DropDownItems->Add(gcnew ToolStripSeparator());
        edit->DropDownItems->Add(Item("Cut", Keys::Control | Keys::X, gcnew EventHandler(this, &MainForm::OnCut)));
        edit->DropDownItems->Add(Item("Copy", Keys::Control | Keys::C, gcnew EventHandler(this, &MainForm::OnCopy)));
        edit->DropDownItems->Add(Item("Paste", Keys::Control | Keys::V, gcnew EventHandler(this, &MainForm::OnPaste)));
        edit->DropDownItems->Add(
            Item("Select all", Keys::Control | Keys::A, gcnew EventHandler(this, &MainForm::OnSelectAll)));
        edit->DropDownItems->Add(gcnew ToolStripSeparator());
        edit->DropDownItems->Add(Item("Find...", Keys::Control | Keys::F, gcnew EventHandler(this, &MainForm::OnFind)));
        edit->DropDownItems->Add(Item("Find next", Keys::F3, gcnew EventHandler(this, &MainForm::OnFindNext)));
        edit->DropDownItems->Add(
            Item("Find previous", Keys::Shift | Keys::F3, gcnew EventHandler(this, &MainForm::OnFindPrevious)));
        edit->DropDownItems->Add(
            Item("Replace...", Keys::Control | Keys::H, gcnew EventHandler(this, &MainForm::OnReplace)));
        edit->DropDownItems->Add(gcnew ToolStripSeparator());
        edit->DropDownItems->Add(
            Item("Re-indent", Keys::Control | Keys::L, gcnew EventHandler(this, &MainForm::OnLayOut)));
        bar->Items->Add(edit);

        // Everything that changes what the project holds. Each of these asks a
        // question and then hands the answer to the core, which does the disk
        // work, keeps the list in step and writes the project back - the same
        // code the terminal front end calls.
        ToolStripMenuItem^ project = gcnew ToolStripMenuItem("&Project");
        project->DropDownItems->Add("New project...", nullptr,
                                    gcnew EventHandler(this, &MainForm::OnNewProject));
        project->DropDownItems->Add("Save project", nullptr,
                                    gcnew EventHandler(this, &MainForm::OnSaveProject));
        project->DropDownItems->Add("Add this file...", nullptr,
                                    gcnew EventHandler(this, &MainForm::OnAddThisFile));
        project->DropDownItems->Add(gcnew ToolStripSeparator());
        ToolStripMenuItem^ another = gcnew ToolStripMenuItem(
            "New file...", nullptr, gcnew EventHandler(this, &MainForm::OnNewFile));
        another->ShortcutKeyDisplayString = "Ctrl+N";
        project->DropDownItems->Add(another);
        project->DropDownItems->Add(Item("Rename...", Keys::F2,
                                         gcnew EventHandler(this, &MainForm::OnRenameFile)));
        project->DropDownItems->Add("Move to group...", nullptr,
                                    gcnew EventHandler(this, &MainForm::OnMoveToGroup));
        project->DropDownItems->Add("Delete...", nullptr,
                                    gcnew EventHandler(this, &MainForm::OnDeleteFile));
        bar->Items->Add(project);

        ToolStripMenuItem^ build = gcnew ToolStripMenuItem("&Build");
        ToolStripMenuItem^ compile = gcnew ToolStripMenuItem(
            "Compile", nullptr, gcnew EventHandler(this, &MainForm::OnCompile));
        // Ctrl-B, not F7: F7 is Step over on the Debug menu, and a shortcut
        // bound twice goes to whichever menu was built first - so Compile took
        // it and Step over never saw a key it advertised. Ctrl-B is what the
        // terminal half compiles with anyway.
        compile->ShortcutKeys = static_cast<Keys>(Keys::Control | Keys::B);
        build->DropDownItems->Add(compile);
        ToolStripMenuItem^ runIt = gcnew ToolStripMenuItem(
            "Run", nullptr, gcnew EventHandler(this, &MainForm::OnRun));
        runIt->ShortcutKeys = Keys::F5;
        build->DropDownItems->Add(runIt);

        // The project's program, as against the file in front of you. Two
        // commands and no guessing: which one you meant is the one you pick,
        // and compiling a single file never needs the project shut.
        build->DropDownItems->Add(gcnew ToolStripSeparator());
        build->DropDownItems->Add(
            Item("Build project", Keys::F4,
                 gcnew EventHandler(this, &MainForm::OnBuildProject)));
        build->DropDownItems->Add("Run project", nullptr,
                                  gcnew EventHandler(this, &MainForm::OnRunProject));
        debugConfigItem_ = gcnew ToolStripMenuItem(
            "Debug build", nullptr, gcnew EventHandler(this, &MainForm::OnDebugConfig));
        // Shown, not bound: Ctrl+D moves between these two and belongs to
        // neither, so it is caught in ProcessCmdKey. Both say so, as both of
        // the terminal's own say Ctrl-D.
        debugConfigItem_->ShortcutKeyDisplayString = "Ctrl+D";
        build->DropDownItems->Add(debugConfigItem_);
        releaseConfigItem_ = gcnew ToolStripMenuItem(
            "Release build", nullptr, gcnew EventHandler(this, &MainForm::OnReleaseConfig));
        releaseConfigItem_->ShortcutKeyDisplayString = "Ctrl+D";
        build->DropDownItems->Add(releaseConfigItem_);
        bar->Items->Add(build);

        // Its own column, as in the other front end: once the program has
        // stopped, most of what you do next is one of these.
        ToolStripMenuItem^ debug = gcnew ToolStripMenuItem("&Debug");
        debug->DropDownItems->Add(Item("Start / continue", Keys::F8,
                                       gcnew EventHandler(this, &MainForm::OnDebug)));
        debug->DropDownItems->Add(Item("Toggle breakpoint", Keys::F9,
                                       gcnew EventHandler(this, &MainForm::OnToggleBreak)));
        debug->DropDownItems->Add(Item("Step over", Keys::F7,
                                       gcnew EventHandler(this, &MainForm::OnStepOver)));
        debug->DropDownItems->Add(Item("Step into", Keys::F6,
                                       gcnew EventHandler(this, &MainForm::OnStepInto)));
        debug->DropDownItems->Add("Step out", nullptr,
                                  gcnew EventHandler(this, &MainForm::OnStepOut));
        debug->DropDownItems->Add("Stop debugging", nullptr,
                                  gcnew EventHandler(this, &MainForm::OnDebugStop));
        bar->Items->Add(debug);

        // The panel's three tabs, reachable without the mouse.
        ToolStripMenuItem^ view = gcnew ToolStripMenuItem("&View");
        // The pane was reachable by mouse and by nothing else: Tab belongs to
        // the text box, which lays a line out with it. Zero, before the three
        // that pick the panels, because it is the pane before them.
        view->DropDownItems->Add(Item("Project pane", Keys::Control | Keys::D0,
                                      gcnew EventHandler(this, &MainForm::OnFocusTree)));
        view->DropDownItems->Add(Item("The file", Keys::Control | Keys::D4,
                                      gcnew EventHandler(this, &MainForm::OnFocusText)));
        view->DropDownItems->Add(gcnew ToolStripSeparator());
        view->DropDownItems->Add(Item("Console", Keys::Control | Keys::D1,
                                      gcnew EventHandler(this, &MainForm::OnShowConsole)));
        view->DropDownItems->Add(Item("Debug", Keys::Control | Keys::D2,
                                      gcnew EventHandler(this, &MainForm::OnShowDebug)));
        view->DropDownItems->Add(Item("Assembly", Keys::Control | Keys::D3,
                                      gcnew EventHandler(this, &MainForm::OnShowAssembly)));
        view->DropDownItems->Add(gcnew ToolStripSeparator());

        // What is showing, rather than what to look at. Ctrl+P and Ctrl+E are
        // free here and are the terminal's own keys for these two. Ctrl+L is
        // not free - it is Re-indent in this window - so line numbers are on
        // the menu and nowhere else. The ticks are how anybody finds out these
        // can be turned off at all.
        numbersItem_ = Item("Show line numbers", Keys::None,
                            gcnew EventHandler(this, &MainForm::OnToggleNumbers));
        numbersItem_->Checked = true;
        view->DropDownItems->Add(numbersItem_);

        paneItem_ = Item("Show project pane", Keys::Control | Keys::P,
                         gcnew EventHandler(this, &MainForm::OnTogglePane));
        paneItem_->Checked = true;
        view->DropDownItems->Add(paneItem_);

        panelItem_ = Item("Show bottom panel", Keys::Control | Keys::E,
                          gcnew EventHandler(this, &MainForm::OnTogglePanel));
        panelItem_->Checked = true;
        view->DropDownItems->Add(panelItem_);

        bar->Items->Add(view);

        ToolStripMenuItem^ target = gcnew ToolStripMenuItem("&Target");
        targetItems_ = gcnew System::Collections::Generic::List<ToolStripMenuItem^>();
        for (int i = 0; i < 3; ++i) {
            ToolStripMenuItem^ one = gcnew ToolStripMenuItem(
                FromUtf8(ed1_arch(i)), nullptr, gcnew EventHandler(this, &MainForm::OnTarget));
            one->ShortcutKeyDisplayString = "Ctrl+T";   // shown, not bound - see ProcessCmdKey
            targetItems_->Add(one);
            target->DropDownItems->Add(one);
        }
        bar->Items->Add(target);

        ToolStripMenuItem^ tools = gcnew ToolStripMenuItem("Too&ls");
        toolAutoItem_ = gcnew ToolStripMenuItem(
            "By language", nullptr, gcnew EventHandler(this, &MainForm::OnToolAuto));
        toolAutoItem_->ShortcutKeyDisplayString = "Ctrl+K";   // shown, not bound
        tools->DropDownItems->Add(toolAutoItem_);
        toolCc1Item_ = gcnew ToolStripMenuItem(
            "cc1", nullptr, gcnew EventHandler(this, &MainForm::OnToolCc1));
        toolCc1Item_->ShortcutKeyDisplayString = "Ctrl+K";
        tools->DropDownItems->Add(toolCc1Item_);
        toolClItem_ = gcnew ToolStripMenuItem(
            "MSVC (cl)", nullptr, gcnew EventHandler(this, &MainForm::OnToolCl));
        toolClItem_->ShortcutKeyDisplayString = "Ctrl+K";
        tools->DropDownItems->Add(toolClItem_);
        tools->DropDownItems->Add(gcnew ToolStripSeparator());
        // Here rather than on View: View is what is shown at this moment, and
        // this is a choice made once and kept, like the compiler above it.
        tools->DropDownItems->Add("Font...", nullptr,
                                  gcnew EventHandler(this, &MainForm::OnFont));
        bar->Items->Add(tools);

        ToolStripMenuItem^ help = gcnew ToolStripMenuItem("&Help");
        help->DropDownItems->Add(Item("Keys", Keys::F1,
                                      gcnew EventHandler(this, &MainForm::OnKeys)));
        help->DropDownItems->Add("About " + ProductName(), nullptr,
                                 gcnew EventHandler(this, &MainForm::OnAbout));
        bar->Items->Add(help);

        MainMenuStrip = bar;
        Controls->Add(bar);
        ShowChoices();

        // The same four regions as the terminal one: project, text, panel and
        // status - by splitters here instead of by counting rows.
        // The splitter is the only line drawn between the panes: each control
        // fills its side completely, so the container's own colour shows in
        // that gap and nowhere else. Sunken borders around each pane put a
        // second line beside that one and a sliver of grey outside the tree,
        // which is what made the left edge look like an accident.
        outer_ = gcnew SplitContainer();
        outer_->Dock = DockStyle::Fill;
        outer_->Orientation = Orientation::Horizontal;
        outer_->SplitterWidth = 5;
        outer_->BackColor = System::Drawing::Color::FromArgb(222, 222, 222);
        SplitContainer^ outer = outer_;

        upper_ = gcnew SplitContainer();
        upper_->Dock = DockStyle::Fill;
        upper_->SplitterWidth = 5;
        upper_->BackColor = System::Drawing::Color::FromArgb(222, 222, 222);
        SplitContainer^ upper = upper_;

        tree_ = gcnew TreeView();
        tree_->Dock = DockStyle::Fill;
        tree_->BorderStyle = System::Windows::Forms::BorderStyle::None;
        tree_->BackColor = System::Drawing::Color::FromArgb(250, 250, 250);
        tree_->ItemHeight = 20;
        tree_->FullRowSelect = true;
        tree_->HideSelection = false;
        tree_->ShowLines = false;
        tree_->ShowRootLines = false;
        tree_->Indent = 16;
        tree_->NodeMouseDoubleClick +=
            gcnew TreeNodeMouseClickEventHandler(this, &MainForm::OnTreeOpen);
        // A double-click was the only way in, so the pane could not be used
        // from the keyboard at all - where the terminal half opens on Enter.
        tree_->KeyDown += gcnew KeyEventHandler(this, &MainForm::OnTreeKey);
        upper->Panel1->Controls->Add(tree_);

        sheets_ = gcnew System::Collections::Generic::List<Sheet^>();
        files_ = gcnew TabControl();
        files_->Dock = DockStyle::Fill;
        files_->SelectedIndexChanged +=
            gcnew EventHandler(this, &MainForm::OnSheetChanged);
        upper->Panel2->Controls->Add(files_);
        outer->Panel1->Controls->Add(upper);

        panel_ = gcnew TabControl();
        panel_->Dock = DockStyle::Fill;

        console_ = ReadOnlyBox();
        // Enter on the compiler's words goes to what they are about, as Enter
        // on the terminal's console does. Double-click for the same, since a
        // console is a thing people click at.
        console_->KeyDown += gcnew KeyEventHandler(this, &MainForm::OnConsoleKey);
        console_->DoubleClick += gcnew EventHandler(this, &MainForm::OnConsoleDoubleClick);
        debug_ = ReadOnlyBox();
        // A frame in the Debug tab is gone to the way an error in the Console
        // is: double-click it, or put the caret on it and press enter.
        debug_->KeyDown += gcnew KeyEventHandler(this, &MainForm::OnDebugKey);
        debug_->DoubleClick += gcnew EventHandler(this, &MainForm::OnDebugDoubleClick);
        assembly_ = gcnew RichTextBox();
        assembly_->Dock = DockStyle::Fill;
        assembly_->BorderStyle = System::Windows::Forms::BorderStyle::None;
        assembly_->Font = gcnew System::Drawing::Font("Consolas", 10.0f);
        assembly_->ReadOnly = true;
        assembly_->WordWrap = false;

        TabPage^ one = gcnew TabPage("Console");
        one->Controls->Add(console_);
        TabPage^ two = gcnew TabPage("Debug");
        two->Controls->Add(debug_);
        TabPage^ three = gcnew TabPage("Assembly");
        three->Controls->Add(assembly_);
        panel_->TabPages->Add(one);
        panel_->TabPages->Add(two);
        panel_->TabPages->Add(three);
        outer->Panel2->Controls->Add(panel_);

        Controls->Add(outer);
        outer->BringToFront();

        // The line along the bottom: what happened on the left, then the
        // directory the project is in - asked for once too often to be left
        // out - and the caret's line and column at the right.
        status_ = gcnew StatusStrip();
        what_ = gcnew ToolStripStatusLabel("no file");
        what_->Spring = true;
        what_->TextAlign = System::Drawing::ContentAlignment::MiddleLeft;
        // What the next build will use, the way the terminal's status bar has
        // always carried it. The ticks in the menus answer the same question,
        // but only when a menu is open; this answers it at a glance.
        build_ = gcnew ToolStripStatusLabel("");
        build_->BorderSides = ToolStripStatusLabelBorderSides::Left;
        build_->ToolTipText =
            "language, debug or release, the compiler that will run "
            "(* when the file chose it) and the target when it matters";
        root_ = gcnew ToolStripStatusLabel("no project");
        root_->BorderSides = ToolStripStatusLabelBorderSides::Left;
        root_->ForeColor = System::Drawing::Color::FromArgb(90, 90, 90);
        where_ = gcnew ToolStripStatusLabel("1:1");
        where_->BorderSides = ToolStripStatusLabelBorderSides::Left;
        status_->Items->Add(what_);
        status_->Items->Add(build_);
        status_->Items->Add(root_);
        status_->Items->Add(where_);
        Controls->Add(status_);
        SayBuild();   // ShowChoices ran before this strip existed

        // If the settings would not parse they have been kept and replaced, and
        // that is worth one line: the font and the last project have gone back
        // to their defaults, and somebody should know why.
        String^ kept = FromUtf8(ed1_settings_set_aside());
        if (kept != nullptr && kept->Length > 0)
            what_->Text = "bad configuration file - kept as " +
                          System::IO::Path::GetFileName(kept) + ", a new one made";

        // FixedPanel is the rule the terminal front end already follows: the
        // project pane takes 22 columns and the bottom panel takes 7 rows -
        // "the command, and a few lines of what it said" - and the code gets
        // everything that is left, including everything the window gains when
        // it is made larger.
        upper->FixedPanel = FixedPanel::Panel1;
        outer->FixedPanel = FixedPanel::Panel2;

        // How much each of them takes is settled in Arrange, once the window
        // has a size. SplitterDistance is refused while a control has none,
        // and quietly leaves both panes at half - which is a third of the
        // width for short filenames and half the height for a dozen lines of
        // output.

        // There is always a sheet, even before a file is opened, so nothing
        // below has to ask whether there is somewhere to type.
        Sheet^ first = MakeSheet(nullptr, "");
        text_ = first->box;

        console_->Text = "cc1 or cl output appears here.  Ctrl-B builds, F5 runs.";
        SayDebugTab(nullptr);
        SayWhere();
    }

    // The proportions and the smallest each pane may be dragged to, in one
    // place and applied once the window has a size to divide.
    //
    // Both belong here rather than where the containers are made, and for the
    // same reason: a minimum is checked against the size the container has at
    // that moment, and a SplitContainer that is not in a window yet is 150 by
    // 100. Asking for a 240-pixel minimum there is not refused quietly - it
    // throws where it stands, and the window never appears at all.
    void Arrange() {
        const int forTree = 120;    // the narrowest the project pane may be
        const int forCode = 240;    // and the narrowest the text beside it
        const int forUpper = 160;
        const int forPanel = 80;

        if (upper_ != nullptr && upper_->Width > forTree + forCode) {
            upper_->Panel1MinSize = forTree;
            upper_->Panel2MinSize = forCode;
            upper_->SplitterDistance =
                Math::Max(forTree, Math::Min(240, upper_->Width - forCode));
        }

        if (outer_ != nullptr &&
            outer_->Height > forUpper + forPanel + outer_->SplitterWidth) {
            outer_->Panel1MinSize = forUpper;
            outer_->Panel2MinSize = forPanel;

            // A quarter of the window for the output, between about eight
            // lines and about twenty.
            int deep = Math::Max(120, Math::Min(220, outer_->Height / 4));
            int distance = outer_->Height - deep - outer_->SplitterWidth;
            int most = outer_->Height - forPanel - outer_->SplitterWidth;
            outer_->SplitterDistance = Math::Max(forUpper, Math::Min(distance, most));
        }
    }

    // The first file the project lists, if it lists one.
    void OpenFirstOfProject() {
        int groups = ed1_project_groups(project_);
        for (int group = 0; group < groups; ++group) {
            if (ed1_project_files(project_, group) < 1) continue;

            String^ relative = FromUtf8(ed1_project_file(project_, group, 0));
            array<Byte>^ bytes = Utf8Of(relative);
            pin_ptr<Byte> pinned = &bytes[0];
            String^ full = FromUtf8(
                ed1_project_absolute(project_, reinterpret_cast<const char*>(pinned)));
            if (full->Length > 0 && System::IO::File::Exists(full)) OpenPath(full);
            return;
        }
    }

    // Which directory the project is in, where it can be read. "Where would a
    // new file go" is not a question the window should leave anyone asking.
    String^ RootNow() {
        String^ root = FromUtf8(ed1_project_root(project_));
        if (root == nullptr || root->Length == 0) root = projectDirectory_;
        return root;
    }

    void SayWhere() {
        String^ root = RootNow();
        root_->Text = root == nullptr || root->Length == 0 ? "no project" : root;
    }

    // The console holds what a build said and then what the program said, and
    // the second is the part somebody is waiting for - so the box is left
    // showing its end rather than its beginning. The terminal front end has
    // always done this by moving panelOff_; a text box has to be told.
    void ShowConsoleEnd() {
        console_->SelectionStart = console_->TextLength;
        console_->SelectionLength = 0;
        console_->ScrollToCaret();
    }

    // A text box wants CRLF; a debugger writes whatever it writes. Normalised
    // rather than assumed, since cdb and lldb do not agree about it.
    static String^ Lines(String^ text) {
        if (String::IsNullOrEmpty(text)) return text;
        return text->Replace("\r\n", "\n")->Replace("\r", "\n")->Replace("\n", "\r\n");
    }

    // What the environment says a compiler is; failing that the one sitting
    // beside the editor; failing that the bare name, for PATH to answer.
    //
    // The middle one is why this is more than a getenv. The product directory
    // holds ed1gui.exe and cc1.exe side by side, and this used to reach that
    // cc1.exe only when the editor happened to have been started in that
    // directory - so the installed copy, started from a shortcut or the Start
    // menu, reported a compiler that was standing right next to it. Beside the
    // editor is looked at before PATH on purpose: a compiler shipped with this
    // copy of the editor is the one that copy is meant to drive.
    static String^ Named(String^ variable, String^ orElse) {
        String^ said = Environment::GetEnvironmentVariable(variable);
        if (said != nullptr && said->Length != 0) return said;
        String^ here = Application::StartupPath;
        if (here != nullptr && here->Length != 0) {
            String^ beside = System::IO::Path::Combine(here, orElse + ".exe");
            if (System::IO::File::Exists(beside)) return beside;
        }
        return orElse;
    }

    // A menu item with its key, since there are a dozen of them now. The
    // handler arrives already made: a managed member function has no address
    // to take, so the delegate is built at the call.
    [System::Runtime::InteropServices::DllImport("user32.dll", SetLastError = true)]
    static int GetWindowLong(System::IntPtr window, int index);
    [System::Runtime::InteropServices::DllImport("user32.dll", SetLastError = true)]
    static int SetWindowLong(System::IntPtr window, int index, int value);
    [System::Runtime::InteropServices::DllImport("user32.dll", SetLastError = true)]
    static bool SetLayeredWindowAttributes(System::IntPtr window, int key, unsigned char alpha,
                                           int flags);

    // "Courier New 11.5" - the name, then the size, which is what the last
    // space separates. Anything this machine cannot make is quietly the
    // default: a settings file carried from another machine may name a font
    // that is not here, and that is not worth a complaint on startup.
    System::Drawing::Font^ RememberedFont() {
        String^ said = FromUtf8(ed1_code_font());
        if (said != nullptr && said->Length > 0) {
            int cut = said->LastIndexOf(' ');
            if (cut > 0) {
                String^ name = said->Substring(0, cut);
                double points = 0;
                if (Double::TryParse(said->Substring(cut + 1),
                                     System::Globalization::NumberStyles::Float,
                                     System::Globalization::CultureInfo::InvariantCulture,
                                     points) && points >= 6 && points <= 48) {
                    try {
                        System::Drawing::Font^ made =
                            gcnew System::Drawing::Font(name, (float)points);
                        // A name it does not have gives back something else
                        // entirely, so ask what it made rather than trust it.
                        if (made->FontFamily->Name == name) return made;
                    } catch (Exception^) { }
                }
            }
        }
        return gcnew System::Drawing::Font("Consolas", 11.0f);
    }

    ToolStripMenuItem^ Item(String^ label, Keys key, EventHandler^ handler) {
        ToolStripMenuItem^ item = gcnew ToolStripMenuItem(label, nullptr, handler);
        item->ShortcutKeys = key;
        return item;
    }

    // There is no input box in Windows Forms, so here is one.
    String^ Ask(String^ title, String^ initial) { return Ask(title, nullptr, initial); }

    // The same one, with a line above the entry for what the title has no room
    // to say - which directory a name will be taken as relative to, most of
    // the time, since that is the thing nobody can otherwise see.
    String^ Ask(String^ title, String^ note, String^ initial) {
        Form^ box = gcnew Form();
        box->Text = title;
        box->FormBorderStyle = System::Windows::Forms::FormBorderStyle::FixedDialog;
        box->StartPosition = System::Windows::Forms::FormStartPosition::CenterParent;
        box->MinimizeBox = false;
        box->MaximizeBox = false;

        int lift = note == nullptr || note->Length == 0 ? 0 : 24;
        box->ClientSize = System::Drawing::Size(480, 96 + lift);

        if (lift > 0) {
            Label^ says = gcnew Label();
            says->Text = note;
            says->AutoEllipsis = true;
            says->ForeColor = System::Drawing::Color::FromArgb(90, 90, 90);
            says->SetBounds(12, 12, 456, 20);
            box->Controls->Add(says);
        }

        TextBox^ entry = gcnew TextBox();
        entry->Text = initial == nullptr ? "" : initial;
        entry->SetBounds(12, 16 + lift, 456, 26);
        entry->Font = gcnew System::Drawing::Font("Consolas", 10.0f);
        entry->SelectAll();

        Button^ yes = gcnew Button();
        yes->Text = "OK";
        yes->DialogResult = System::Windows::Forms::DialogResult::OK;
        yes->SetBounds(306, 56 + lift, 78, 28);

        Button^ no = gcnew Button();
        no->Text = "Cancel";
        no->DialogResult = System::Windows::Forms::DialogResult::Cancel;
        no->SetBounds(390, 56 + lift, 78, 28);

        box->Controls->Add(entry);
        box->Controls->Add(yes);
        box->Controls->Add(no);
        box->AcceptButton = yes;
        box->CancelButton = no;

        if (box->ShowDialog(this) != System::Windows::Forms::DialogResult::OK) return nullptr;
        return entry->Text;
    }

    int LeadingOf(int row) {
        if (row < 0 || row >= text_->Lines->Length) return 0;
        String^ line = text_->Lines[row];
        int lead = 0;
        while (lead < line->Length && (line[lead] == ' ' || line[lead] == '\t')) ++lead;
        return lead;
    }

    int CaretRow() { return text_->GetLineFromCharIndex(text_->SelectionStart); }
    int CaretColumn() {
        return text_->SelectionStart - text_->GetFirstCharIndexFromLine(CaretRow());
    }

    // The core counts columns in UTF-8 bytes and the box counts them in
    // characters. For ASCII they agree and for anything else they do not, so
    // the two are converted rather than assumed equal.
    int CharacterColumn(int row, int byteColumn) {
        if (row < 0 || row >= text_->Lines->Length) return 0;
        array<Byte>^ bytes = Utf8Of(text_->Lines[row]);
        int usable = bytes->Length - 1;   // without the terminator
        if (byteColumn > usable) byteColumn = usable;
        if (byteColumn <= 0) return 0;
        return System::Text::Encoding::UTF8->GetString(bytes, 0, byteColumn)->Length;
    }

    int ByteColumn(int row, int characterColumn) {
        if (row < 0 || row >= text_->Lines->Length) return 0;
        String^ line = text_->Lines[row];
        if (characterColumn > line->Length) characterColumn = line->Length;
        if (characterColumn <= 0) return 0;
        return System::Text::Encoding::UTF8->GetByteCount(line->Substring(0, characterColumn));
    }

    // The whole document as the core wants it: one string, lines separated by
    // a single newline.
    array<Byte>^ WholeText() { return Utf8Of(text_->Text->Replace("\r\n", "\n")); }

    Sheet^ Current() {
        int at = files_->SelectedIndex;
        if (at < 0 || at >= sheets_->Count) return nullptr;
        return sheets_[at];
    }

    // One file has more than one spelling. The project pane hands out paths
    // with forward slashes, because that is how the project writes them; the
    // command line and the open dialog give backslashes. Comparing the text
    // meant a file opened from the pane while already open opened a *second*
    // time, showing what was on disk - so the changes in the first tab looked
    // as though they had been thrown away.
    static String^ OneName(String^ path) {
        if (path == nullptr) return nullptr;
        String^ full = path;
        try {
            full = System::IO::Path::GetFullPath(path);
        } catch (Exception^) {
            // Not a path this machine can resolve; the text will have to do.
        }
        return full->Replace('/', '\\')->TrimEnd('\\')->ToLowerInvariant();
    }

    static bool SamePath(String^ one, String^ other) {
        if (one == nullptr || other == nullptr) return false;
        return String::Equals(OneName(one), OneName(other), StringComparison::Ordinal);
    }

    Sheet^ SheetFor(String^ path) {
        for (int i = 0; i < sheets_->Count; ++i)
            if (SamePath(sheets_[i]->path, path)) return sheets_[i];
        return nullptr;
    }

    // A tab, a box, and the numbers down its left.
    Sheet^ MakeSheet(String^ path, String^ contents) {
        Sheet^ sheet = gcnew Sheet();
        sheet->path = path;

        sheet->box = gcnew RichTextBox();
        sheet->box->Dock = DockStyle::Fill;
        sheet->box->Font = codeFont_;
        sheet->box->WordWrap = false;
        sheet->box->AcceptsTab = true;
        sheet->box->HideSelection = false;
        sheet->box->BorderStyle = System::Windows::Forms::BorderStyle::None;
        sheet->box->Text = contents == nullptr ? "" : contents;
        sheet->box->KeyDown += gcnew KeyEventHandler(this, &MainForm::OnKeyDown);
        sheet->box->KeyUp += gcnew KeyEventHandler(this, &MainForm::OnKeyUp);
        sheet->box->SelectionChanged += gcnew EventHandler(this, &MainForm::OnCaretMoved);
        sheet->box->TextChanged += gcnew EventHandler(this, &MainForm::OnTextChanged);
        sheet->box->VScroll += gcnew EventHandler(this, &MainForm::OnScrolled);

        sheet->gutter = gcnew Gutter();
        sheet->gutter->Dock = DockStyle::Left;
        sheet->gutter->Width = 52;
        sheet->gutter->BackColor = System::Drawing::Color::FromArgb(245, 245, 245);
        sheet->gutter->Tag = sheet->box;
        sheet->gutter->Paint += gcnew PaintEventHandler(this, &MainForm::OnGutterPaint);
        sheet->gutter->Visible = numbers_;   // a tab opened while they are off has none either
        sheet->box->Tag = sheet->gutter;

        sheet->page = gcnew TabPage(path == nullptr
                                        ? "untitled"
                                        : System::IO::Path::GetFileName(path));
        sheet->page->Controls->Add(sheet->box);
        sheet->page->Controls->Add(sheet->gutter);
        sheet->box->BringToFront();

        sheets_->Add(sheet);
        files_->TabPages->Add(sheet->page);
        files_->SelectedTab = sheet->page;
        return sheet;
    }

    void OnSheetChanged(Object^, EventArgs^) {
        Sheet^ sheet = Current();
        if (sheet == nullptr) return;

        text_ = sheet->box;
        path_ = sheet->path;
        Text = path_ == nullptr ? "ed1"
                                : String::Format("{0} - {1}", ProductName(), System::IO::Path::GetFileName(path_));
        what_->Text = path_ == nullptr
                          ? "untitled"
                          : System::IO::Path::GetFileName(path_) + "  " +
                                text_->Lines->Length + " lines";
        text_->Focus();
        SayBuild();   // the language is the file's, so it arrives with the tab
        PlaceStopBar();
        sheet->gutter->Invalidate();

        // Each tab keeps its own text, so the one coming forward is coloured
        // for where it is scrolled to.
        Recolour();
    }

    // ---- what Windows Forms does not say ----------------------------------

    // Three messages, because there is no other way to say any of them. A box
    // that is drawing repaints on every one of the hundreds of selections
    // colouring makes, and it scrolls itself to each of them; frozen, it does
    // neither, and its scroll position is read before and put back after.
    literal int kDrawing = 0x000B;        // WM_SETREDRAW
    literal int kWhereScrolled = 0x04DD;  // EM_GETSCROLLPOS
    literal int kScrollTo = 0x04DE;       // EM_SETSCROLLPOS

    [System::Runtime::InteropServices::DllImport("user32.dll", EntryPoint = "SendMessageW")]
    static IntPtr Tell(IntPtr window, int message, IntPtr one, IntPtr two);

    [System::Runtime::InteropServices::DllImport("user32.dll", EntryPoint = "SendMessageW")]
    static IntPtr Tell(IntPtr window, int message, IntPtr one, Spot% where);

    // Stopping and starting drawing are not symmetrical: nothing that happened
    // while it was stopped is on the screen, so the box has to be told to paint
    // itself once it is allowed to again.
    static void Drawing(Control^ box, bool allowed) {
        Tell(box->Handle, kDrawing, IntPtr(allowed ? 1 : 0), IntPtr::Zero);
        if (allowed) {
            box->Invalidate();
            box->Update();
        }
    }

    // ---- the numbers down the left ----------------------------------------

    void OnTextChanged(Object^ sender, EventArgs^) {
        // Colouring raises this. A RichTextBox reports a formatting change as
        // a text change, so every pass that paints a keyword blue came through
        // here and marked the file modified - which is why a file just opened
        // and coloured wore a star that nobody had earned. Nothing about the
        // text has changed while this flag is up.
        if (colouring_) return;

        Sheet^ sheet = Current();
        if (sheet == nullptr) return;

        // Wide enough for the last line, and it does not shrink back as you
        // scroll - a gutter that changed width would take the text with it.
        int digits = sheet->box->Lines->Length < 1 ? 1
                                                   : sheet->box->Lines->Length.ToString()->Length;
        int wanted = 22 + 9 * digits;
        if (wanted > sheet->gutter->Width) sheet->gutter->Width = wanted;
        sheet->gutter->Invalidate();

        // What has just been typed is coloured as it is typed, which costs
        // what a screenful costs. The box a file is being read into is not the
        // box in front yet, and colouring it would use the language of
        // whatever is - so only the one in front is coloured here.
        if (sender == text_) {
            // The line being typed on, and nothing else. Recolour freezes the
            // box and repaints all of it, which is right when a file arrives
            // or the view moves and is far too much for one keystroke - it was
            // the whole text area redrawn per character, which is what made
            // the numbers down the side judder.
            RecolourLine(CaretRow());
            settle_->Stop();
            settle_->Start();
            MarkTab(sheet);
        }
    }

    void OnScrolled(Object^, EventArgs^) {
        PlaceStopBar();
        Sheet^ sheet = Current();
        if (sheet == nullptr) return;
        // What has just come into view is coloured now that it can be seen.
        Recolour();
        sheet->gutter->Invalidate();
    }

    void OnGutterPaint(Object^ sender, PaintEventArgs^ e) {
        Panel^ panel = safe_cast<Panel^>(sender);
        RichTextBox^ box = safe_cast<RichTextBox^>(panel->Tag);
        if (box == nullptr) return;

        // A hairline where the numbers stop, so the gutter reads as a margin
        // rather than as a stripe of a different colour.
        System::Drawing::Pen^ edge =
            gcnew System::Drawing::Pen(System::Drawing::Color::FromArgb(228, 228, 228));
        e->Graphics->DrawLine(edge, panel->Width - 1, 0, panel->Width - 1, panel->Height);

        int lines = box->Lines->Length;
        if (lines < 1) lines = 1;

        int first = box->GetLineFromCharIndex(box->GetCharIndexFromPosition(
            System::Drawing::Point(1, 1)));
        int caretLine = box->GetLineFromCharIndex(box->SelectionStart);

        System::Drawing::Brush^ quiet =
            gcnew System::Drawing::SolidBrush(System::Drawing::Color::FromArgb(150, 150, 150));
        System::Drawing::Brush^ here =
            gcnew System::Drawing::SolidBrush(System::Drawing::Color::FromArgb(60, 60, 60));

        // Which file this gutter belongs to, so that its breakpoints can be
        // found. The sheet knows; the panel only knows its box.
        String^ file = nullptr;
        for each (Sheet^ sheet in sheets_)
            if (sheet->gutter == panel) file = sheet->path;

        System::Collections::Generic::List<int>^ marks = nullptr;
        if (file != nullptr) breaks_->TryGetValue(OneName(file), marks);

        bool sameFile = file != nullptr && stopFile_ != nullptr &&
                        System::IO::Path::GetFileName(file) ==
                            System::IO::Path::GetFileName(stopFile_);

        System::Drawing::Brush^ breakMark =
            gcnew System::Drawing::SolidBrush(System::Drawing::Color::FromArgb(200, 60, 60));
        System::Drawing::Brush^ stopMark =
            gcnew System::Drawing::SolidBrush(System::Drawing::Color::FromArgb(40, 150, 60));

        for (int row = first; row < lines; ++row) {
            int at = box->GetFirstCharIndexFromLine(row);
            if (at < 0) break;
            System::Drawing::Point where = box->GetPositionFromCharIndex(at);
            if (where.Y > panel->Height) break;

            // The left of the gutter is the debugger's: a breakpoint waiting,
            // and an arrow on the line the program is standing on. The numbers
            // are right-aligned away from it, so nothing moves when one appears.
            float top = static_cast<float>(where.Y) + 3.0f;
            if (marks != nullptr && marks->Contains(row + 1))
                e->Graphics->FillEllipse(breakMark, 3.0f, top, 9.0f, 9.0f);
            if (sameFile && stopLine_ == row + 1) {
                array<System::Drawing::PointF>^ arrow = gcnew array<System::Drawing::PointF>(3);
                arrow[0] = System::Drawing::PointF(3.0f, top);
                arrow[1] = System::Drawing::PointF(12.0f, top + 4.5f);
                arrow[2] = System::Drawing::PointF(3.0f, top + 9.0f);
                e->Graphics->FillPolygon(stopMark, arrow);
            }

            String^ number = (row + 1).ToString();
            System::Drawing::SizeF size = e->Graphics->MeasureString(number, box->Font);
            e->Graphics->DrawString(number, box->Font,
                                    row == caretLine ? here : quiet,
                                    panel->Width - size.Width - 6,
                                    static_cast<float>(where.Y));
        }
    }

    TextBox^ ReadOnlyBox() {
        TextBox^ box = gcnew TextBox();
        box->Dock = DockStyle::Fill;
        box->Multiline = true;
        box->ReadOnly = true;
        box->ScrollBars = ScrollBars::Both;
        box->WordWrap = false;
        box->Font = gcnew System::Drawing::Font("Consolas", 10.0f);
        return box;
    }

    // What the build produced, read out of its own assembly - the same reader
    // the terminal front end uses, and now the same words too. They used to be
    // written out here as well, and that copy went stale the day cc1 started
    // writing DWARF; it says whatever the core says.
    void SayDebugTab(String^ assembly) {
        array<Byte>^ bytes = Utf8Of(assembly == nullptr ? "" : assembly);
        pin_ptr<Byte> pinned = &bytes[0];
        String^ found = TakeUtf8(ed1_describe_build(reinterpret_cast<const char*>(pinned)));

        array<Byte>^ archBytes = Utf8Of(arch_ == nullptr ? "" : arch_);
        pin_ptr<Byte> archPin = &archBytes[0];
        String^ note = TakeUtf8(ed1_debug_note(
            ed1_resolve(toolKind_, LanguageNow()),
            reinterpret_cast<const char*>(archPin)));

        debug_->Text = String::Join(
            "\r\n",
            gcnew array<String^>{note->Replace("\n", "\r\n"), "",
                                 found->Replace("\n", "\r\n")});
    }

    // Said again about the same assembly, because what is said about it depends
    // on the target and the compiler, and both can be changed after a build.
    void RefreshDebugTab() {
        SayDebugTab(assembly_->Text->Replace("\r\n", "\n"));
    }

    // ---- laying out and colouring -----------------------------------------

    int LanguageNow() {
        array<Byte>^ bytes = Utf8Of(path_ == nullptr ? "" : path_);
        pin_ptr<Byte> pinned = &bytes[0];
        return ed1_language_for(reinterpret_cast<const char*>(pinned));
    }

    // Lay out what is selected, or the whole file when nothing is - the same
    // rule the terminal half follows, and for the same reason: the whole file
    // is laid out either way, because indentation is a property of everything
    // above a line and not of the line. What a selection decides is which
    // lines are written back.
    void OnLayOut(Object^, EventArgs^) {
        array<Byte>^ bytes = Utf8Of(text_->Text->Replace("\r\n", "\n"));
        pin_ptr<Byte> pinned = &bytes[0];

        String^ laid = TakeUtf8(ed1_reindent(reinterpret_cast<const char*>(pinned),
                                             indentWidth_, indentTabs_, indentCase_));

        int caret = text_->SelectionStart;
        int length = text_->SelectionLength;

        array<String^>^ was = text_->Lines;
        array<String^>^ now = laid->Split('\n');
        // Split leaves an empty piece after a trailing newline; Lines does not.
        int howManyNow = now->Length;
        if (howManyNow > 0 && now[howManyNow - 1]->Length == 0) --howManyNow;

        if (length > 0 && howManyNow == was->Length) {
            int first = text_->GetLineFromCharIndex(caret);
            int last = text_->GetLineFromCharIndex(caret + length - 1);
            if (last >= was->Length) last = was->Length - 1;

            for (int row = first; row <= last; ++row) was[row] = now[row];

            text_->Text = String::Join("\r\n", was);
            text_->SelectionStart = Math::Min(caret, text_->TextLength);
            text_->SelectionLength = 0;
            Recolour();
            int howMany = last - first + 1;
            what_->Text = String::Format("laid out {0} line{1} of the selection",
                                         howMany, howMany == 1 ? "" : "s");
            return;
        }

        text_->Text = laid->Replace("\n", "\r\n");
        text_->SelectionStart = Math::Min(caret, text_->TextLength);
        Recolour();
        what_->Text = String::Format("laid out - {0} lines", text_->Lines->Length);
    }

    void OnKeyDown(Object^, KeyEventArgs^ e) {
        if (e->KeyCode == Keys::Tab && !e->Control && !e->Shift) {
            e->SuppressKeyPress = true;

            int row = CaretRow();
            int column = CaretColumn();
            String^ line = text_->Lines->Length > row ? text_->Lines[row] : "";
            int lead = 0;
            while (lead < line->Length && (line[lead] == ' ' || line[lead] == '\t')) ++lead;

            // In the leading space, tab means 'put this line where it belongs'
            // rather than 'add a step'. Anywhere else it is an ordinary indent.
            if (column <= lead) {
                Realign(row);
                text_->SelectionStart = text_->GetFirstCharIndexFromLine(row) +
                                        LeadingOf(row);
            } else if (indentTabs_ != 0) {
                text_->SelectedText = "\t";
            } else {
                text_->SelectedText = gcnew String(' ', indentWidth_);
            }
            return;
        }

        if (e->KeyCode != Keys::Enter || e->Control || e->Shift) return;

        int caret = text_->SelectionStart;
        int row = text_->GetLineFromCharIndex(caret);
        int column = caret - text_->GetFirstCharIndexFromLine(row);

        array<Byte>^ bytes = Utf8Of(text_->Text->Replace("\r\n", "\n"));
        pin_ptr<Byte> pinned = &bytes[0];

        // The indentation is decided by the same function the terminal editor
        // calls, on the same text.
        String^ lead = TakeUtf8(ed1_indent_after_newline(
            reinterpret_cast<const char*>(pinned), row, column, indentWidth_, indentTabs_,
            indentCase_));

        e->SuppressKeyPress = true;
        text_->SelectedText = "\r\n" + lead;
    }

    // Colouring is done to what is on the screen and a screenful either side of
    // it, not to the whole file. The lexer still runs from the top - it has to,
    // since a comment opened on line 3 colours line 900 - but that part is
    // native and costs nothing worth counting. What costs is the box: every
    // coloured run is a selection, and a file the size of a parser has tens of
    // thousands of them.
    //
    // The box is frozen while it happens and its scroll position is put back
    // afterwards, because a selection scrolls itself into view. Without that,
    // opening a long file walks visibly down to its last line - which is what
    // it used to do.
    // Colouring is done *to* the box, and Rich Edit records what is done to the
    // box twice over: as a text change, and in its undo buffer. colouring_ has
    // always kept the first out of OnTextChanged. The second went unnoticed
    // until Ctrl+Z on a file nobody had touched undid a colour, jumped the
    // caret to it and put a star on the tab. Suspending the recording keeps
    // colour out of the undo stack, so Ctrl+Z undoes your typing - and, when
    // there is none, says there is none.
    void BeginColouring() {
        colouring_ = true;
        if (text_ != nullptr && text_->IsHandleCreated)
            ed1_undo_suspend(text_->Handle.ToPointer());
    }

    void EndColouring() {
        if (text_ != nullptr && text_->IsHandleCreated)
            ed1_undo_resume(text_->Handle.ToPointer());
        colouring_ = false;
    }

    // The line the program is standing on, washed in light blue across its
    // whole width. The gutter has an arrow for it, but the eye finds a bar
    // through the line sooner than a mark beside it.
    //
    // Only the background: the syntax colours on that line are left as they
    // are. And done the way the colouring passes are done - the caret put back,
    // the Modified flag put back, and undo recording suspended - because to a
    // RichTextBox this is formatting like any other, which means a text change
    // and an undo entry unless both are held off.
    // The wash stops where the line's text stops, and cannot be made to run to
    // the edge of the view. A RichTextBox paints its own background and renders
    // exactly one kind of per-range colour, which is behind characters. Both
    // other roads were tried and are closed: including the line break in the
    // selection extends nothing once the selection moves away, and Rich Edit
    // stores paragraph shading for RTF's sake without ever drawing it. An
    // edge-to-edge bar wants an owner-drawn edit control, which is a different
    // project from this one.
    void PaintRow(int row, bool on) {
        if (text_ == nullptr || row < 0 || row >= text_->Lines->Length) return;
        int start = text_->GetFirstCharIndexFromLine(row);
        int length = text_->Lines[row]->Length;
        if (row < text_->Lines->Length - 1) length += 1;   // take the line break too
        int caret = text_->SelectionStart;
        int chosen = text_->SelectionLength;
        bool touched = text_->Modified;

        BeginColouring();
        text_->Select(start, length);
        text_->SelectionBackColor =
            on ? System::Drawing::Color::FromArgb(214, 234, 255) : text_->BackColor;
        text_->Select(caret, chosen);
        text_->Modified = touched;
        EndColouring();
    }

    void ShowStoppedLine(int row) {
        if (highlightRow_ != row) {
            if (highlightRow_ >= 0) PaintRow(highlightRow_, false);
            highlightRow_ = row;
            if (row >= 0) PaintRow(row, true);
        }
        PlaceStopBar();
    }

    // Made once, on the form rather than inside the text box, so it can reach
    // the edge of the view.
    //
    // It covers no text, and so needs no transparency: the line's own
    // characters already carry the blue behind them, and this fills only the
    // empty part of the line from where the code stops to the right-hand edge.
    // The two meet and read as one band. A translucent strip over the whole
    // line was tried first and the text went dim behind it - the form is
    // double-buffered, so it composites its children itself and a layered
    // child's alpha never applies.
    //
    // No WS_EX_TRANSPARENT, though click-through would have been nice: on a
    // child window that flag means "do not paint your own background", and the
    // strip simply never appeared. It is a plain panel now. What it costs is
    // that a click in the empty space to the right of the stopped line lands on
    // the strip instead of putting the caret at the end of that line - dead
    // space, and only while the program is stopped there.
    void MakeStopBar() {
        if (stopBar_ != nullptr) return;
        stopBar_ = gcnew Panel();
        stopBar_->BackColor = System::Drawing::Color::FromArgb(214, 234, 255);
        stopBar_->Visible = false;
        stopBar_->TabStop = false;
        Controls->Add(stopBar_);
        stopBar_->BringToFront();
    }

    // Where the bar goes, in the form's own coordinates: the text box tells us
    // where the line is, and the answer is turned into the form's frame because
    // that is where the bar lives. Hidden when the line has scrolled out of the
    // view, or there is no stop to show.
    void PlaceStopBar() {
        MakeStopBar();
        if (text_ == nullptr || highlightRow_ < 0 || !text_->IsHandleCreated) {
            stopBar_->Visible = false;
            return;
        }
        // Only over the file the program is actually stopped in - another tab
        // has its own line 12 and the program is not on it.
        if (path_ == nullptr || stopFile_ == nullptr ||
            System::IO::Path::GetFileName(stopFile_) != System::IO::Path::GetFileName(path_)) {
            stopBar_->Visible = false;
            return;
        }
        if (highlightRow_ >= text_->Lines->Length) { stopBar_->Visible = false; return; }

        int index = text_->GetFirstCharIndexFromLine(highlightRow_);
        System::Drawing::Point where = text_->GetPositionFromCharIndex(index);
        int height = System::Windows::Forms::TextRenderer::MeasureText("Ay", text_->Font).Height;

        if (where.Y < -height || where.Y > text_->ClientSize.Height) {
            stopBar_->Visible = false;   // scrolled out of sight
            return;
        }

        // Where the code on that line stops - the caret's place at the end of
        // it - is where this begins.
        int after = index + text_->Lines[highlightRow_]->Length;
        System::Drawing::Point ends = text_->GetPositionFromCharIndex(after);
        int from = ends.Y == where.Y ? ends.X : where.X;   // a wrapped line: give up and fill

        System::Drawing::Point corner =
            PointToClient(text_->PointToScreen(System::Drawing::Point(from, where.Y)));
        int width = text_->ClientSize.Width - from;
        if (width <= 0) { stopBar_->Visible = false; return; }

        stopBar_->SetBounds(corner.X, corner.Y, width, height);
        stopBar_->Visible = true;
        stopBar_->BringToFront();
    }

    void Recolour() {
        if (colouring_) return;
        if (text_ == nullptr || !text_->IsHandleCreated) return;
        BeginColouring();

        array<String^>^ all = text_->Lines;
        int language = LanguageNow();

        // What can be seen now, and as much again above and below it, so that
        // the wheel has somewhere coloured to travel before this runs again.
        int top = text_->GetLineFromCharIndex(
            text_->GetCharIndexFromPosition(System::Drawing::Point(1, 1)));
        int bottom = text_->GetLineFromCharIndex(text_->GetCharIndexFromPosition(
            System::Drawing::Point(1, Math::Max(1, text_->ClientSize.Height - 2))));
        int deep = Math::Max(1, bottom - top + 1);
        int from = Math::Max(0, top - deep);
        int to = Math::Min(all->Length - 1, bottom + deep);

        // Formatting sets the box's own modified flag, and a file that has only
        // been looked at has not been modified.
        bool touched = text_->Modified;
        int caret = text_->SelectionStart;
        int length = text_->SelectionLength;

        Spot scrolled;
        stateGood_ = false;
        Tell(text_->Handle, kWhereScrolled, IntPtr::Zero, scrolled);
        Drawing(text_, false);

        // Above the window: the lexer only, for the state it carries down.
        int state = 0;
        for (int row = 0; row < from; ++row) {
            array<Byte>^ above = Utf8Of(all[row]);
            pin_ptr<Byte> abovePin = &above[0];
            array<Byte>^ ignored = gcnew array<Byte>(above->Length);
            pin_ptr<Byte> ignoredPin = &ignored[0];
            ed1_highlight(reinterpret_cast<const char*>(abovePin), language, &state,
                          ignoredPin, ignored->Length);
        }

        // A run that stops being a keyword has to stop being blue, so the
        // window goes back to black before it is coloured again.
        if (from <= to) {
            int start = text_->GetFirstCharIndexFromLine(from);
            int end = to + 1 < all->Length ? text_->GetFirstCharIndexFromLine(to + 1)
                                           : text_->TextLength;
            if (start >= 0 && end > start) {
                text_->Select(start, end - start);
                text_->SelectionColor = System::Drawing::Color::Black;
            }
        }

        for (int row = from; row <= to; ++row) {
            array<Byte>^ bytes = Utf8Of(all[row]);
            pin_ptr<Byte> linePin = &bytes[0];

            array<Byte>^ kinds = gcnew array<Byte>(bytes->Length);
            pin_ptr<Byte> kindPin = &kinds[0];

            int howMany = ed1_highlight(reinterpret_cast<const char*>(linePin), language,
                                        &state, kindPin, kinds->Length);

            int at = text_->GetFirstCharIndexFromLine(row);
            if (at < 0) break;

            // The kinds are one per byte and the box counts characters, so each
            // run of one kind is measured by decoding just that run.
            int column = 0;
            int byte = 0;
            while (byte < howMany) {
                Byte kind = kinds[byte];
                int end = byte;
                while (end < howMany && kinds[end] == kind) ++end;

                int width =
                    System::Text::Encoding::UTF8->GetString(bytes, byte, end - byte)->Length;
                if (width > 0 && kind != ED1_KIND_NORMAL) {
                    text_->Select(at + column, width);
                    text_->SelectionColor = ColourOf(kind);
                }
                column += width;
                byte = end;
            }
        }

        text_->Select(caret, length);
        text_->SelectionColor = System::Drawing::Color::Black;
        Tell(text_->Handle, kScrollTo, IntPtr::Zero, scrolled);
        text_->Modified = touched;
        Drawing(text_, true);
        EndColouring();
    }

    // A quarter of a second after the last keystroke, colour what is on the
    // screen properly: a quote or a /* just typed changes the lines below it,
    // and that is the pass which notices.
    void OnSettled(Object^, EventArgs^) {
        settle_->Stop();
        Recolour();
    }

    // One line, coloured where it sits. No freezing and no full repaint: the
    // line is on the screen already, so the box redraws it and nothing else.
    void RecolourLine(int row) {
        if (colouring_ || text_ == nullptr || !text_->IsHandleCreated) return;

        array<String^>^ all = text_->Lines;
        if (row < 0 || row >= all->Length) return;

        BeginColouring();
        int language = LanguageNow();

        int state = 0;
        if (stateGood_ && stateRow_ == row) {
            state = stateAt_;
        } else {
            for (int above = 0; above < row; ++above) {
                array<Byte>^ bytes = Utf8Of(all[above]);
                pin_ptr<Byte> linePin = &bytes[0];
                array<Byte>^ kinds = gcnew array<Byte>(bytes->Length);
                pin_ptr<Byte> kindPin = &kinds[0];
                ed1_highlight(reinterpret_cast<const char*>(linePin), language, &state,
                              kindPin, kinds->Length);
            }
            stateGood_ = true;
            stateRow_ = row;
            stateAt_ = state;
        }

        bool touched = text_->Modified;
        int caret = text_->SelectionStart;
        int length = text_->SelectionLength;

        int at = text_->GetFirstCharIndexFromLine(row);
        if (at >= 0) {
            text_->Select(at, all[row]->Length);
            text_->SelectionColor = System::Drawing::Color::Black;

            array<Byte>^ bytes = Utf8Of(all[row]);
            pin_ptr<Byte> linePin = &bytes[0];
            array<Byte>^ kinds = gcnew array<Byte>(bytes->Length);
            pin_ptr<Byte> kindPin = &kinds[0];
            int howMany = ed1_highlight(reinterpret_cast<const char*>(linePin), language,
                                        &state, kindPin, kinds->Length);

            int column = 0;
            int byte = 0;
            while (byte < howMany) {
                Byte kind = kinds[byte];
                int end = byte;
                while (end < howMany && kinds[end] == kind) ++end;

                int width =
                    System::Text::Encoding::UTF8->GetString(bytes, byte, end - byte)->Length;
                if (width > 0 && kind != ED1_KIND_NORMAL) {
                    text_->Select(at + column, width);
                    text_->SelectionColor = ColourOf(kind);
                }
                column += width;
                byte = end;
            }
        }

        text_->Select(caret, length);
        text_->SelectionColor = System::Drawing::Color::Black;
        text_->Modified = touched;
        EndColouring();
    }

    System::Drawing::Color ColourOf(Byte kind) {
        switch (kind) {
            case ED1_KIND_KEYWORD: return System::Drawing::Color::Blue;
            case ED1_KIND_TYPE:    return System::Drawing::Color::Teal;
            case ED1_KIND_STRING:  return System::Drawing::Color::FromArgb(0, 128, 0);
            case ED1_KIND_CHAR:    return System::Drawing::Color::FromArgb(0, 128, 0);
            case ED1_KIND_COMMENT: return System::Drawing::Color::Gray;
            case ED1_KIND_PREPROC: return System::Drawing::Color::Purple;
            case ED1_KIND_NUMBER:  return System::Drawing::Color::FromArgb(180, 100, 0);
            case ED1_KIND_LABEL:   return System::Drawing::Color::FromArgb(150, 120, 0);
            default:               return System::Drawing::Color::Black;
        }
    }

    // These three said nothing when there was nothing to do, where the terminal
    // has a word for each. A command that appears to have been ignored is worse
    // than one that says why it did nothing - and the box gives no sign of its
    // own either way, since the text simply does not change.
    void OnUndo(Object^, EventArgs^) {
        // The box keeps its own history, and it is the one the typing went
        // into - there is no sense in keeping a second one beside it.
        if (!text_->CanUndo) { what_->Text = "nothing to undo"; return; }
        text_->Undo();
    }
    void OnRedo(Object^, EventArgs^) {
        if (!text_->CanRedo) { what_->Text = "nothing to redo"; return; }
        text_->Redo();
    }
    void OnCut(Object^, EventArgs^) { text_->Cut(); }
    void OnCopy(Object^, EventArgs^) { text_->Copy(); }
    void OnPaste(Object^, EventArgs^) {
        // Asked about text rather than about the clipboard in general: this is
        // a source file, and an image or a page of RTF on the clipboard is
        // nothing to paste into one whatever the box would make of it.
        if (!text_->CanPaste(DataFormats::GetFormat(DataFormats::Text))) {
            what_->Text = "there is nothing to paste";
            return;
        }
        text_->Paste();
        Recolour();
    }
    void OnSelectAll(Object^, EventArgs^) { text_->SelectAll(); }

    // ---- finding ----------------------------------------------------------

    void OnFind(Object^, EventArgs^) {
        String^ want = Ask("Find", needle_);
        // Nothing asked for, whether the box was cleared or the question was
        // cancelled - and said out loud, where this used to close and leave no
        // trace of having been opened. Editor::findPrompt answers the same.
        if (want == nullptr || want->Length == 0) {
            what_->Text = "nothing looked for";
            return;
        }
        needle_ = want;
        Seek(CaretRow(), ByteColumn(CaretRow(), CaretColumn()), true);
    }

    void OnFindNext(Object^, EventArgs^) {
        if (needle_ == nullptr) { OnFind(nullptr, nullptr); return; }
        Seek(CaretRow(), ByteColumn(CaretRow(), CaretColumn()) + 1, true);
    }

    void OnFindPrevious(Object^, EventArgs^) {
        if (needle_ == nullptr) { OnFind(nullptr, nullptr); return; }
        Seek(CaretRow(), ByteColumn(CaretRow(), CaretColumn()), false);
    }

    void Seek(int row, int column, bool forwards) {
        array<Byte>^ text = WholeText();
        pin_ptr<Byte> textPin = &text[0];
        array<Byte>^ needle = Utf8Of(needle_);
        pin_ptr<Byte> needlePin = &needle[0];

        int foundRow = 0, foundColumn = 0;
        int found = forwards ? ed1_find_next(reinterpret_cast<const char*>(textPin),
                                             reinterpret_cast<const char*>(needlePin), row,
                                             column, &foundRow, &foundColumn)
                             : ed1_find_previous(reinterpret_cast<const char*>(textPin),
                                                 reinterpret_cast<const char*>(needlePin), row,
                                                 column, &foundRow, &foundColumn);
        if (found == 0) {
            what_->Text = needle_ + " is not in this file";
            return;
        }

        int at = text_->GetFirstCharIndexFromLine(foundRow) +
                 CharacterColumn(foundRow, foundColumn);
        text_->Select(at, needle_->Length);
        text_->ScrollToCaret();
        // A jump of hundreds of lines lands past whatever was coloured last,
        // and scrolling done in code raises no scroll event to notice it.
        Recolour();
        text_->Focus();
        what_->Text = String::Format("{0} - line {1}", needle_, foundRow + 1);
    }

    void OnReplace(Object^, EventArgs^) {
        // Both ways out are said out loud, as Editor::replacePrompt says them:
        // the box closing with nothing on the message line reads as a command
        // that did not work, rather than one that was called off. The second
        // question takes an empty answer, which is how a word is deleted
        // everywhere it appears - only cancelling it means nothing.
        String^ want = Ask("Replace what", needle_);
        if (want == nullptr || want->Length == 0) {
            what_->Text = "nothing replaced";
            return;
        }
        String^ with = Ask("Replace \"" + want + "\" with", "");
        if (with == nullptr) {
            what_->Text = "nothing replaced";
            return;
        }

        array<Byte>^ text = WholeText();
        pin_ptr<Byte> textPin = &text[0];
        array<Byte>^ needle = Utf8Of(want);
        pin_ptr<Byte> needlePin = &needle[0];
        array<Byte>^ replacement = Utf8Of(with);
        pin_ptr<Byte> replacementPin = &replacement[0];

        int howMany = 0;
        String^ changed = TakeUtf8(ed1_replace_all(
            reinterpret_cast<const char*>(textPin), reinterpret_cast<const char*>(needlePin),
            reinterpret_cast<const char*>(replacementPin), &howMany));

        if (howMany == 0) {
            what_->Text = want + " is not in this file";
            return;
        }

        int caret = text_->SelectionStart;
        text_->Text = changed->Replace("\n", "\r\n");
        text_->SelectionStart = Math::Min(caret, text_->TextLength);
        needle_ = with;
        Recolour();
        what_->Text = String::Format("{0} change{1} - Ctrl-Z puts them back", howMany,
                                     howMany == 1 ? "" : "s");
    }

    // ---- laying one line out ----------------------------------------------

    // The leading space a line should have, put there without disturbing the
    // rest of it.
    void Realign(int row) {
        if (row < 0 || row >= text_->Lines->Length) return;

        String^ line = text_->Lines[row];
        int lead = 0;
        while (lead < line->Length && (line[lead] == ' ' || line[lead] == '\t')) ++lead;

        array<Byte>^ text = WholeText();
        pin_ptr<Byte> textPin = &text[0];
        String^ want = TakeUtf8(ed1_indent_for(reinterpret_cast<const char*>(textPin), row,
                                               indentWidth_, indentTabs_, indentCase_));
        if (want == line->Substring(0, lead)) return;

        int start = text_->GetFirstCharIndexFromLine(row);
        int caret = text_->SelectionStart;

        text_->Select(start, lead);
        text_->SelectedText = want;
        text_->SelectionStart = Math::Max(0, Math::Min(caret + want->Length - lead,
                                                       text_->TextLength));
    }

    void OnKeyUp(Object^, KeyEventArgs^) {
        // Three characters decide where their own line sits, and only these
        // three, so nothing moves under the caret unless it had to.
        int caret = text_->SelectionStart;
        if (caret <= 0 || caret > text_->TextLength) return;

        wchar_t just = text_->Text[caret - 1];
        if (just != '}' && just != '#' && just != ':') return;

        int row = text_->GetLineFromCharIndex(caret - 1);
        String^ line = text_->Lines[row];
        int column = (caret - 1) - text_->GetFirstCharIndexFromLine(row);

        if (just != ':') {
            // A brace or a hash only moves its line when nothing precedes it.
            for (int i = 0; i < column && i < line->Length; ++i)
                if (line[i] != ' ' && line[i] != '\t') return;
        }
        Realign(row);
    }

    void OnCaretMoved(Object^, EventArgs^) {
        // Colouring moves the caret to every run it paints. None of those are
        // the person's caret, and the line and column below are theirs.
        if (colouring_) return;
        stateGood_ = false;   // another line begins in another state

        int caret = text_->SelectionStart;
        int row = text_->GetLineFromCharIndex(caret);
        where_->Text =
            String::Format("{0}:{1}", row + 1, caret - text_->GetFirstCharIndexFromLine(row) + 1);

        Sheet^ sheet = Current();
        if (sheet != nullptr) sheet->gutter->Invalidate();
    }

    // ---- files and the project --------------------------------------------

    void OnOpenProject(Object^, EventArgs^) {
        FolderBrowserDialog^ pick = gcnew FolderBrowserDialog();
        if (pick->ShowDialog() != System::Windows::Forms::DialogResult::OK) {
            what_->Text = "no project opened";
            return;
        }
        LoadProject(pick->SelectedPath);
    }

    void LoadProject(String^ directory) {
        projectDirectory_ = directory;
        tree_->Nodes->Clear();

        array<Byte>^ bytes = Utf8Of(directory);
        pin_ptr<Byte> pinned = &bytes[0];

        array<Byte>^ error = gcnew array<Byte>(512);
        pin_ptr<Byte> errorPin = &error[0];

        int loaded = ed1_project_load(project_, reinterpret_cast<const char*>(pinned),
                                      reinterpret_cast<char*>(errorPin), error->Length);
        if (loaded == 0) {
            String^ why = FromUtf8(reinterpret_cast<const char*>(errorPin));

            // Nothing to read is not the same as something that will not read.
            // A directory with no ed1.json gets one written from what is in it,
            // as the terminal half has always done - the window used to show an
            // empty pane and say so, which is a worse answer to "open this
            // folder" than the one the core was already able to give.
            //
            // A project file that will not *parse* is somebody's work and is
            // never written over; then the pane shows the directory and the
            // message says what is wrong with it.
            if (why->Length == 0 &&
                ed1_begin_from_what_is_there(project_,
                                             reinterpret_cast<const char*>(pinned)) != 0) {
                FillTree();
                indentWidth_ = ed1_project_indent_width(project_);
                indentTabs_ = ed1_project_indent_tabs(project_);
                indentCase_ = ed1_project_case_indent(project_);
                toolKind_ = ed1_project_toolchain(project_);
                config_ = ed1_project_config(project_);
                arch_ = FromUtf8(ed1_project_arch(project_));
                ShowChoices();
                ed1_remember_project(reinterpret_cast<const char*>(pinned));
                what_->Text = FromUtf8(ed1_outcome_message(project_));
                SayWhere();
                return;
            }

            what_->Text = why->Length > 0 ? why : "no ed1.json in that directory";

            // No project file still leaves a directory that paths are counted
            // from, so the file commands work either way.
            ed1_project_set_root(project_, reinterpret_cast<const char*>(pinned));
            SayWhere();
            return;
        }

        FillTree();

        indentWidth_ = ed1_project_indent_width(project_);
        indentTabs_ = ed1_project_indent_tabs(project_);
        indentCase_ = ed1_project_case_indent(project_);
        toolKind_ = ed1_project_toolchain(project_);
        config_ = ed1_project_config(project_);
        arch_ = FromUtf8(ed1_project_arch(project_));
        ShowChoices();

        // Remembered, so that starting the window with nothing opens here -
        // which the terminal half has always done and this never did.
        array<Byte>^ opened = Utf8Of(directory);
        pin_ptr<Byte> openedPin = &opened[0];
        ed1_remember_project(reinterpret_cast<const char*>(openedPin));

        what_->Text = String::Format("ready - {0}, {1} groups",
                                     FromUtf8(ed1_project_name(project_)),
                                     ed1_project_groups(project_));
        SayWhere();
    }

    // Rebuilt from the project as it stands, so a change shows without the
    // file being read again.
    void FillTree() {
        tree_->Nodes->Clear();

        int groups = ed1_project_groups(project_);
        for (int group = 0; group < groups; ++group) {
            TreeNode^ node = gcnew TreeNode(FromUtf8(ed1_project_group_name(project_, group)));
            int files = ed1_project_files(project_, group);
            for (int file = 0; file < files; ++file) {
                String^ relative = FromUtf8(ed1_project_file(project_, group, file));
                TreeNode^ leaf = gcnew TreeNode(relative);

                array<Byte>^ rel = Utf8Of(relative);
                pin_ptr<Byte> relPin = &rel[0];
                leaf->Tag = FromUtf8(
                    ed1_project_absolute(project_, reinterpret_cast<const char*>(relPin)));
                node->Nodes->Add(leaf);
            }
            tree_->Nodes->Add(node);
        }
        tree_->ExpandAll();

        indentWidth_ = ed1_project_indent_width(project_);
        indentTabs_ = ed1_project_indent_tabs(project_);
        indentCase_ = ed1_project_case_indent(project_);
        toolKind_ = ed1_project_toolchain(project_);
        config_ = ed1_project_config(project_);
        arch_ = FromUtf8(ed1_project_arch(project_));
        ShowChoices();

        what_->Text = String::Format("ready - {0}, {1} groups",
                                     FromUtf8(ed1_project_name(project_)), groups);
    }

    // What the file commands act on: whatever the project pane is standing on
    // when that is a file, and the tab in front otherwise.
    String^ TargetFile() {
        if (tree_->SelectedNode != nullptr && tree_->SelectedNode->Tag != nullptr)
            return safe_cast<String^>(tree_->SelectedNode->Tag);
        return path_;
    }

    // The group the pane is standing in, so a file made while looking at a
    // group lands in it.
    String^ GroupUnderCursor() {
        TreeNode^ node = tree_->SelectedNode;
        while (node != nullptr && node->Tag != nullptr) node = node->Parent;
        return node == nullptr ? "Sources" : node->Text;
    }

    // A native call whose answer is a message either way.
    bool Did(int outcome) {
        what_->Text = FromUtf8(ed1_outcome_message(project_));
        return outcome != 0;
    }

    String^ OutcomePath() { return FromUtf8(ed1_outcome_path(project_)); }

    void OnNewFile(Object^, EventArgs^) {
        String^ root = RootNow();
        String^ name = Ask("New file (name, or one directory and a name)",
                           root == nullptr || root->Length == 0
                               ? "There is no project, so this goes where the editor was started."
                               : "It will be made in " + root,
                           "");
        if (name == nullptr || name->Length == 0) { what_->Text = "nothing made"; return; }

        array<Byte>^ relative = Utf8Of(name);
        pin_ptr<Byte> relativePin = &relative[0];
        array<Byte>^ group = Utf8Of(GroupUnderCursor());
        pin_ptr<Byte> groupPin = &group[0];

        if (!Did(ed1_create_file(project_, reinterpret_cast<const char*>(relativePin),
                                 reinterpret_cast<const char*>(groupPin))))
            return;

        FillTree();
        OpenPath(OutcomePath());
    }

    // A blank buffer with no name, as Editor::newFile makes one. MakeSheet
    // adds the tab and brings it forward, and bringing it forward is what puts
    // "untitled" on the message line - so what this has to say is said after.
    // The gutter goes as a whole, as Ctrl-L takes the whole column away in the
    // terminal: the numbers, the breakpoint dots and the arrow are one thing to
    // turn off, not three.
    // Fixed-pitch only: code in a proportional face is not worth offering, and
    // the gutter's numbers are laid out on the assumption that every character
    // is the same width.
    void OnFont(Object^, EventArgs^) {
        FontDialog^ pick = gcnew FontDialog();
        pick->Font = codeFont_;
        pick->FixedPitchOnly = true;
        pick->ShowEffects = false;
        pick->ShowColor = false;
        pick->MinSize = 6;
        pick->MaxSize = 48;
        if (pick->ShowDialog(this) != System::Windows::Forms::DialogResult::OK) {
            what_->Text = "font unchanged";
            return;
        }

        UseFont(pick->Font);

        String^ said = String::Format(
            System::Globalization::CultureInfo::InvariantCulture, "{0} {1}",
            codeFont_->FontFamily->Name, codeFont_->SizeInPoints);
        array<Byte>^ bytes = Utf8Of(said);
        pin_ptr<Byte> pinned = &bytes[0];
        ed1_remember_code_font(reinterpret_cast<const char*>(pinned));
        what_->Text = said;
    }

    // Every tab, not only the one in front, and everything measured from the
    // font afterwards: the gutter draws its numbers in it, and the bar over the
    // stopped line is one line high.
    void UseFont(System::Drawing::Font^ chosen) {
        codeFont_ = chosen;
        for each (Sheet^ sheet in sheets_) {
            bool touched = sheet->box->Modified;
            sheet->box->Font = codeFont_;
            sheet->box->Modified = touched;   // a font is not an edit
            if (sheet->gutter != nullptr) sheet->gutter->Invalidate();
        }
        Recolour();
        PlaceStopBar();
    }

    void OnToggleNumbers(Object^, EventArgs^) {
        numbers_ = !numbers_;
        numbersItem_->Checked = numbers_;
        for each (Sheet^ sheet in sheets_)
            if (sheet->gutter != nullptr) sheet->gutter->Visible = numbers_;
        what_->Text = numbers_ ? "line numbers on" : "line numbers off";
    }

    void OnTogglePane(Object^, EventArgs^) {
        bool hidden = !upper_->Panel1Collapsed;
        upper_->Panel1Collapsed = hidden;
        paneItem_->Checked = !hidden;
        what_->Text = hidden ? "project pane hidden" : "project pane showing";
    }

    void OnTogglePanel(Object^, EventArgs^) {
        bool hidden = !outer_->Panel2Collapsed;
        outer_->Panel2Collapsed = hidden;
        panelItem_->Checked = !hidden;
        what_->Text = hidden ? "bottom panel hidden" : "bottom panel showing";
    }

    void OnNewBuffer(Object^, EventArgs^) {
        MakeSheet(nullptr, "");
        what_->Text = "new file - Ctrl+S names it";
    }

    void OnNextFile(Object^, EventArgs^) { StepFile(1); }
    void OnPreviousFile(Object^, EventArgs^) { StepFile(-1); }

    // Round the ends, as Editor::nextDocument does. Which file arrived is left
    // to OnSheetChanged, which says it for every other way of changing tab too.
    void StepFile(int by) {
        int count = files_->TabPages->Count;
        if (count < 2) { what_->Text = "only one file is open"; return; }
        int at = (files_->SelectedIndex + count + by) % count;
        files_->SelectedTab = files_->TabPages[at];
    }

    void OnRenameFile(Object^, EventArgs^) {
        String^ target = TargetFile();
        if (target == nullptr) { what_->Text = "no file to rename"; return; }

        array<Byte>^ was = Utf8Of(target);
        pin_ptr<Byte> wasPin = &was[0];
        String^ shown = FromUtf8(ed1_project_relative(project_,
                                                      reinterpret_cast<const char*>(wasPin)));

        String^ name = Ask("Rename " + shown + " to", shown);
        if (name == nullptr || name->Length == 0) { what_->Text = "not renamed"; return; }

        array<Byte>^ from = Utf8Of(target);
        pin_ptr<Byte> fromPin = &from[0];
        array<Byte>^ to = Utf8Of(name);
        pin_ptr<Byte> toPin = &to[0];

        if (!Did(ed1_rename_file(project_, reinterpret_cast<const char*>(fromPin),
                                 reinterpret_cast<const char*>(toPin))))
            return;

        // A tab showing that file has to follow its own name, or saving would
        // write the old one back.
        String^ now = OutcomePath();
        for (int i = 0; i < sheets_->Count; ++i) {
            if (sheets_[i]->path == nullptr) continue;
            if (!SamePath(sheets_[i]->path, target)) continue;
            sheets_[i]->path = now;
            MarkTab(sheets_[i]);   // the new name, and the star if it still has one
        }
        if (SamePath(path_, target)) {
            path_ = now;
            Text = String::Format("{0} - {1}", ProductName(), System::IO::Path::GetFileName(now));
            SayBuild();   // a rename can change the suffix, and so the language
        }

        // So do its breakpoints, which are filed under the file's name. Without
        // this they are left under a name nothing asks for again: the marks go
        // from the gutter, and a debugger started afterwards is told to stop in
        // a file that is no longer there. Editor::renameFile does the same.
        String^ wasKey = OneName(target);
        System::Collections::Generic::List<int>^ hadBreaks = nullptr;
        if (breaks_->TryGetValue(wasKey, hadBreaks)) {
            breaks_->Remove(wasKey);
            breakNames_->Remove(wasKey);
            String^ nowKey = OneName(now);
            breaks_[nowKey] = hadBreaks;
            breakNames_[nowKey] = now;
            Sheet^ showing = Current();
            if (showing != nullptr && showing->gutter != nullptr)
                showing->gutter->Invalidate();   // the marks are drawn, not stored
        }

        FillTree();
    }

    void OnDeleteFile(Object^, EventArgs^) {
        String^ target = TargetFile();
        if (target == nullptr) { what_->Text = "no file to delete"; return; }

        // The one command here that cannot be undone, so it is asked plainly
        // and the safe answer is the one already chosen.
        System::Windows::Forms::DialogResult answer = MessageBox::Show(
            this, "Delete " + System::IO::Path::GetFileName(target) + " from disk?",
            "Delete", MessageBoxButtons::YesNo, MessageBoxIcon::Warning,
            MessageBoxDefaultButton::Button2);
        if (answer != System::Windows::Forms::DialogResult::Yes) {
            what_->Text = "not deleted";
            return;
        }

        array<Byte>^ path = Utf8Of(target);
        pin_ptr<Byte> pathPin = &path[0];
        if (!Did(ed1_delete_file(project_, reinterpret_cast<const char*>(pathPin)))) return;

        for (int i = sheets_->Count - 1; i >= 0; --i) {
            if (sheets_[i]->path == nullptr) continue;
            if (!SamePath(sheets_[i]->path, target)) continue;
            Sheet^ sheet = sheets_[i];
            sheets_->Remove(sheet);
            files_->TabPages->Remove(sheet->page);
        }

        // Its breakpoints go with it, as they do in Editor::deleteFile. A file
        // that is not there cannot be stopped in - and a name can come back, so
        // leaving them would hand lines set in this file to whatever is written
        // under the name next.
        String^ key = OneName(target);
        breaks_->Remove(key);
        breakNames_->Remove(key);

        if (sheets_->Count == 0) MakeSheet(nullptr, "");
        OnSheetChanged(nullptr, nullptr);
        FillTree();
    }

    void OnMoveToGroup(Object^, EventArgs^) {
        String^ target = TargetFile();
        if (target == nullptr) { what_->Text = "no file to move"; return; }

        String^ group = Ask("Move to group", GroupUnderCursor());
        if (group == nullptr || group->Length == 0) { what_->Text = "not moved"; return; }

        array<Byte>^ path = Utf8Of(target);
        pin_ptr<Byte> pathPin = &path[0];
        array<Byte>^ into = Utf8Of(group);
        pin_ptr<Byte> intoPin = &into[0];

        if (Did(ed1_move_to_group(project_, reinterpret_cast<const char*>(pathPin),
                                  reinterpret_cast<const char*>(intoPin))))
            FillTree();
    }

    void OnAddThisFile(Object^, EventArgs^) {
        if (path_ == nullptr) {
            what_->Text = "save the file first, so it has a name";
            return;
        }

        String^ group = Ask("Add to group", "Sources");
        if (group == nullptr || group->Length == 0) { what_->Text = "not added"; return; }

        array<Byte>^ path = Utf8Of(path_);
        pin_ptr<Byte> pathPin = &path[0];
        array<Byte>^ into = Utf8Of(group);
        pin_ptr<Byte> intoPin = &into[0];

        if (Did(ed1_add_existing(project_, reinterpret_cast<const char*>(pathPin),
                                 reinterpret_cast<const char*>(intoPin))))
            FillTree();
    }

    // Where a project is made used to be the directory the editor happened to
    // be started in, which is not a thing anybody can see - so it is asked for,
    // and the answer is on the status line from then on.
    void OnNewProject(Object^, EventArgs^) {
        FolderBrowserDialog^ pick = gcnew FolderBrowserDialog();
        pick->Description = "Where to put the project";
        pick->ShowNewFolderButton = true;
        String^ start = RootNow();
        if (start != nullptr && start->Length > 0) pick->SelectedPath = start;
        if (pick->ShowDialog(this) != System::Windows::Forms::DialogResult::OK) {
            what_->Text = "no project made";
            return;
        }

        String^ name = Ask("Project name", "It will be made in " + pick->SelectedPath,
                           "Project");
        if (name == nullptr || name->Length == 0) {
            what_->Text = "no project made";
            return;
        }

        array<Byte>^ where = Utf8Of(pick->SelectedPath);
        pin_ptr<Byte> wherePin = &where[0];
        array<Byte>^ called = Utf8Of(name);
        pin_ptr<Byte> calledPin = &called[0];
        array<Byte>^ first = Utf8Of(path_ == nullptr ? "" : path_);
        pin_ptr<Byte> firstPin = &first[0];

        if (Did(ed1_begin_project(project_, reinterpret_cast<const char*>(wherePin),
                                  reinterpret_cast<const char*>(calledPin),
                                  reinterpret_cast<const char*>(firstPin)))) {
            projectDirectory_ = pick->SelectedPath;
            FillTree();
            SayWhere();
        }
    }

    void OnSaveProject(Object^, EventArgs^) { Did(ed1_save_project(project_)); }

    void OnTreeOpen(Object^, TreeNodeMouseClickEventArgs^ e) {
        if (e->Node == nullptr || e->Node->Tag == nullptr) return;
        OpenPath(safe_cast<String^>(e->Node->Tag));
    }

    // Somewhere to put the keyboard. Without these the pane can be reached
    // only with a mouse, and once in it there is no way back to the text.
    void OnFocusTree(Object^, EventArgs^) {
        if (tree_->Nodes->Count == 0) { what_->Text = "no project is open"; return; }
        if (tree_->SelectedNode == nullptr) tree_->SelectedNode = tree_->Nodes[0];
        tree_->Focus();
        what_->Text = "the project pane - enter opens, Ctrl-4 goes back to the file";
    }

    void OnFocusText(Object^, EventArgs^) {
        if (text_ != nullptr) text_->Focus();
    }

    // Enter opens what is picked, as it does in the pane of the terminal half.
    // A group has no file behind it and opens nothing; it folds instead, which
    // is what Enter on a heading should do.
    void OnTreeKey(Object^, KeyEventArgs^ e) {
        if (e->KeyCode != Keys::Return) return;
        TreeNode^ node = tree_->SelectedNode;
        if (node == nullptr) return;

        e->Handled = true;
        e->SuppressKeyPress = true;   // or the box beeps at a key it did not use

        if (node->Tag == nullptr) {
            if (node->IsExpanded) node->Collapse();
            else node->Expand();
            return;
        }
        OpenPath(safe_cast<String^>(node->Tag));
    }

    void OnOpenFile(Object^, EventArgs^) {
        OpenFileDialog^ pick = gcnew OpenFileDialog();
        pick->Filter = "C and C++|*.c;*.h;*.cpp;*.hpp|All files|*.*";
        if (pick->ShowDialog() != System::Windows::Forms::DialogResult::OK) {
            what_->Text = "not opened";
            return;
        }
        OpenPath(pick->FileName);
    }

    void OpenPath(String^ path) {
        // Already open is already open: the tab comes forward with its caret
        // and its history where they were left.
        Sheet^ already = SheetFor(path);
        if (already != nullptr) {
            files_->SelectedTab = already->page;
            return;
        }

        String^ contents;
        try {
            contents = System::IO::File::ReadAllText(path);
        } catch (Exception^ problem) {
            what_->Text = problem->Message;
            return;
        }

        // An untouched, unnamed sheet is a spare tab rather than a file anyone
        // is working on, so opening into it replaces it instead of leaving one
        // behind. The same rule the terminal front end keeps.
        Sheet^ spare = Current();
        Sheet^ sheet;
        if (spare != nullptr && spare->path == nullptr && spare->box->TextLength == 0 &&
            !spare->box->Modified) {
            spare->path = path;
            spare->box->Text = contents;
            // TextChanged fired while the flag was still set, which put a
            // star on the tab; it comes off with the flag.
            spare->box->Modified = false;
            sheet = spare;
        } else {
            sheet = MakeSheet(path, contents);
        }
        text_ = sheet->box;
        path_ = path;
        Text = String::Format("{0} - {1}", ProductName(), System::IO::Path::GetFileName(path));
        SayBuild();   // the file names the language, and no tab changed here
        Recolour();
        OnTextChanged(nullptr, nullptr);

        // A file just read off the disk has no changes in it, whatever the box
        // made of being filled and coloured.
        sheet->box->Modified = false;
        MarkTab(sheet);
        text_->Select(0, 0);
        text_->Focus();
        what_->Text = System::IO::Path::GetFileName(path) + "  " + text_->Lines->Length + " lines";
    }

    void OnCloseFile(Object^, EventArgs^) {
        Sheet^ sheet = Current();
        if (sheet == nullptr) return;
        if (!MayDiscard(sheet)) return;

        sheets_->Remove(sheet);
        files_->TabPages->Remove(sheet->page);
        if (sheets_->Count == 0) {
            MakeSheet(nullptr, "");
            OnSheetChanged(nullptr, nullptr);
        }
        what_->Text = "closed";
    }

    void OnSave(Object^, EventArgs^) {
        if (path_ == nullptr) {
            OnSaveAs(nullptr, nullptr);
            return;
        }
        try {
            // Newlines, not carriage returns: the box keeps CRLF and every
            // other part of this project - the core, the terminal half, the
            // save that happens before a project build - writes LF.
            System::IO::File::WriteAllText(path_, text_->Text->Replace("\r\n", "\n"));
        } catch (Exception^ problem) {
            what_->Text = problem->Message;
            return;
        }
        // Cleared, or nothing can tell afterwards that it was saved - which is
        // what a question about unsaved changes has to ask.
        text_->Modified = false;
        MarkTab(Current());
        what_->Text = System::IO::Path::GetFileName(path_) + " written";
    }

    // A file that has never been saved has to be given a name before it can
    // be. Reached from Save when there is no name yet, and from the File menu.
    void OnSaveAs(Object^, EventArgs^) {
        Sheet^ sheet = Current();
        if (sheet == nullptr) return;

        SaveFileDialog^ pick = gcnew SaveFileDialog();
        pick->Filter = "C and C++|*.c;*.h;*.cpp;*.hpp|All files|*.*";
        if (sheet->path != nullptr) {
            pick->InitialDirectory = System::IO::Path::GetDirectoryName(sheet->path);
            pick->FileName = System::IO::Path::GetFileName(sheet->path);
        } else if (projectDirectory_ != nullptr) {
            pick->InitialDirectory = projectDirectory_;
        }
        if (pick->ShowDialog(this) != System::Windows::Forms::DialogResult::OK) {
            what_->Text = "not saved";
            return;
        }

        sheet->path = pick->FileName;
        path_ = pick->FileName;
        Text = String::Format("{0} - {1}", ProductName(),
                              System::IO::Path::GetFileName(path_));
        SayBuild();
        OnSave(nullptr, nullptr);
        FillTree();
    }

    // A tab wears a star while its file has changes in it, which is how the
    // terminal half shows it and how anybody knows which tab the question is
    // about.
    void MarkTab(Sheet^ sheet) {
        if (sheet == nullptr || sheet->page == nullptr) return;
        String^ name = sheet->path == nullptr
                           ? "[no name]"
                           : System::IO::Path::GetFileName(sheet->path);
        sheet->page->Text = sheet->box->Modified ? name + "*" : name;
    }

    void OnExit(Object^, EventArgs^) { Close(); }

    // Whether a sheet with unsaved work in it may go. Save writes it, Don't
    // save throws it away, Cancel leaves everything where it is - which is
    // what the window had none of: closing a tab discarded the changes without
    // a word, and closing the window discarded every tab's.
    //
    // The terminal half refuses instead of asking, because its answer has to
    // fit on the message line. Here there is room to ask properly.
    bool MayDiscard(Sheet^ sheet) {
        if (sheet == nullptr || !sheet->box->Modified) return true;

        String^ named = sheet->path == nullptr
                            ? "This file has never been saved."
                            : System::IO::Path::GetFileName(sheet->path) + " has changes.";
        System::Windows::Forms::DialogResult answer =
            MessageBox::Show(this, named + "\r\n\r\nSave it before closing?",
                             ProductName(), MessageBoxButtons::YesNoCancel,
                             MessageBoxIcon::Warning);

        if (answer == System::Windows::Forms::DialogResult::Cancel) return false;
        if (answer == System::Windows::Forms::DialogResult::No) return true;

        // Yes: the one being closed is not necessarily the one in front, so it
        // is brought forward and saved through the ordinary path.
        files_->SelectedTab = sheet->page;
        OnSheetChanged(nullptr, nullptr);
        OnSave(nullptr, nullptr);
        return !sheet->box->Modified;
    }


    // Read off the menus, not kept as a second list. A key table written by
    // hand is a promise about the menus that nothing checks, and this project
    // has been bitten more than once by a document that outlived the thing it
    // described - the Makefile's hand-kept dependency list being the worst of
    // them. Rebind anything and this says so the same afternoon.
    //
    // It cannot be the terminal's table either: the window's keys really do
    // differ - Ctrl+PageUp/PageDown for files where the terminal has F2/F3,
    // Ctrl+L for Re-indent where the terminal has Ctrl-A, Ctrl+A for Select
    // all. F1 is the same in both, which is how anybody finds this.
    void OnKeys(Object^, EventArgs^) {
        System::Text::StringBuilder^ table = gcnew System::Text::StringBuilder();
        System::Windows::Forms::KeysConverter^ spelling =
            gcnew System::Windows::Forms::KeysConverter();

        for each (ToolStripItem^ top in MainMenuStrip->Items) {
            ToolStripMenuItem^ menu = dynamic_cast<ToolStripMenuItem^>(top);
            if (menu == nullptr) continue;

            System::Text::StringBuilder^ under = gcnew System::Text::StringBuilder();
            for each (ToolStripItem^ each in menu->DropDownItems) {
                ToolStripMenuItem^ item = dynamic_cast<ToolStripMenuItem^>(each);
                if (item == nullptr) continue;

                // What the item advertises, which is not always what it is
                // bound to: a key that moves between several items is caught in
                // ProcessCmdKey and shown here by name, so it is in this table
                // like any other rather than missing from it.
                String^ key = item->ShortcutKeys == Keys::None
                                  ? item->ShortcutKeyDisplayString
                                  : spelling->ConvertToString(item->ShortcutKeys);
                if (key == nullptr || key->Length == 0) continue;
                under->AppendFormat("  {0,-18}{1}\r\n", key, item->Text->Replace("&", ""));
            }

            // A menu whose items all go without keys says nothing here.
            if (under->Length == 0) continue;
            table->Append(menu->Text->Replace("&", ""))->Append("\r\n");
            table->Append(under->ToString())->Append("\r\n");
        }

        // The one key that is not a menu item, and so the one line here that
        // is written by hand. It is handled in OnKeyDown because it belongs to
        // the text box - a menu shortcut on Tab would take it away from typing.
        table->Append("Editing\r\n");
        table->Append("  Tab               lay this line out, in the leading space\r\n");
        table->Append("  Enter             on the Console, go to the error it is about\r\n");

        Form^ box = gcnew Form();
        box->Text = "Keys";
        box->FormBorderStyle = System::Windows::Forms::FormBorderStyle::SizableToolWindow;
        box->StartPosition = System::Windows::Forms::FormStartPosition::CenterParent;
        box->ClientSize = System::Drawing::Size(460, 560);
        box->ShowInTaskbar = false;

        TextBox^ shown = gcnew TextBox();
        shown->Multiline = true;
        shown->ReadOnly = true;
        shown->WordWrap = false;
        shown->ScrollBars = System::Windows::Forms::ScrollBars::Vertical;
        shown->Dock = DockStyle::Fill;
        shown->BorderStyle = System::Windows::Forms::BorderStyle::None;
        shown->BackColor = System::Drawing::Color::White;
        shown->Font = gcnew System::Drawing::Font("Consolas", 10.0f);
        shown->Text = table->ToString();
        box->Controls->Add(shown);

        // Shown without a selection, and with the caret at the top: a read-only
        // box that opens with everything highlighted looks like a mistake.
        box->Shown += gcnew EventHandler(this, &MainForm::OnKeysShown);
        box->ShowDialog(this);
    }

    void OnKeysShown(Object^ sender, EventArgs^) {
        Form^ box = dynamic_cast<Form^>(sender);
        if (box == nullptr || box->Controls->Count == 0) return;
        TextBox^ shown = dynamic_cast<TextBox^>(box->Controls[0]);
        if (shown == nullptr) return;
        shown->Select(0, 0);
    }

    void OnAbout(Object^, EventArgs^) {
        MessageBox::Show(this, TakeUtf8(ed1_about())->Replace("\n", "\r\n"),
                         "About " + ProductName(),
                         MessageBoxButtons::OK, MessageBoxIcon::Information);
    }

    // ---- building ----------------------------------------------------------

    void OnCompile(Object^, EventArgs^) {
        if (busy_) { what_->Text = "still working - give it a moment"; return; }
        ForgetError();
        if (path_ == nullptr) {
            what_->Text = "open a file first";
            return;
        }
        OnSave(nullptr, nullptr);

        int language = LanguageNow();
        int kind = ed1_resolve(toolKind_, language);
        if (ed1_can_compile(kind, language) == 0) {
            what_->Text = FromUtf8(ed1_refusal(kind, language));
            return;
        }

        array<Byte>^ sourceBytes = Utf8Of(path_);
        pin_ptr<Byte> source = &sourceBytes[0];
        array<Byte>^ cc1Bytes = Utf8Of(cc1_);
        pin_ptr<Byte> cc1 = &cc1Bytes[0];
        array<Byte>^ clBytes = Utf8Of(cl_);
        pin_ptr<Byte> cl = &clBytes[0];
        array<Byte>^ archBytes = Utf8Of(arch_);
        pin_ptr<Byte> arch = &archBytes[0];

        console_->Text =
            "$ " +
            FromUtf8(ed1_shown_command(reinterpret_cast<const char*>(cc1),
                                       reinterpret_cast<const char*>(cl), kind,
                                       reinterpret_cast<const char*>(source), language,
                                       reinterpret_cast<const char*>(arch), config_)) +
            "\r\n";
        panel_->SelectedIndex = 0;
        Application::DoEvents();

        Ed1Build* built = ed1_build(reinterpret_cast<const char*>(cc1),
                                    reinterpret_cast<const char*>(cl), kind,
                                    reinterpret_cast<const char*>(source), language,
                                    reinterpret_cast<const char*>(arch), config_);

        console_->Text += FromUtf8(ed1_build_output(built))->Replace("\n", "\r\n");
        ShowConsoleEnd();

        if (ed1_build_has_error(built) != 0) {
            int line = ed1_build_error_line(built);
            int column = ed1_build_error_column(built);
            String^ message = FromUtf8(ed1_build_error_message(built));
            ed1_build_free(built);

            RememberError(line, column, message, nullptr);
            GoTo(line, column);
            panel_->SelectedIndex = 0;   // the compiler's words are on the Console
            what_->Text = String::Format("{0}:{1}: error: {2}", line, column, message);
            return;
        }

        if (ed1_build_ok(built) == 0) {
            what_->Text = FromUtf8(ed1_toolchain_name(kind)) + " failed - see the console";
            ed1_build_free(built);
            return;
        }

        String^ produced = FromUtf8(ed1_build_assembly(built));
        assembly_->Text = produced->Replace("\n", "\r\n");
        SayDebugTab(produced);
        int lines = ed1_build_assembly_lines(built);
        ed1_build_free(built);

        panel_->SelectedIndex = 2;
        what_->Text = String::Format("{0} lines of assembly", lines);
    }

    // Compiling, linking and running. The console has to keep the three apart:
    // a compiler that refused is not a program that returned something other
    // than zero, and only the program knows what its number meant. Same words
    // as the terminal front end, from the same core.
    void OnRun(Object^, EventArgs^) {
        if (busy_) { what_->Text = "still working - give it a moment"; return; }
        ForgetError();
        if (path_ == nullptr) {
            what_->Text = "open a file first";
            return;
        }
        OnSave(nullptr, nullptr);

        int language = LanguageNow();
        int kind = ed1_resolve(toolKind_, language);
        if (ed1_can_compile(kind, language) == 0) {
            what_->Text = FromUtf8(ed1_refusal(kind, language));
            return;
        }

        array<Byte>^ sourceBytes = Utf8Of(path_);
        pin_ptr<Byte> source = &sourceBytes[0];
        array<Byte>^ cc1Bytes = Utf8Of(cc1_);
        pin_ptr<Byte> cc1 = &cc1Bytes[0];
        array<Byte>^ clBytes = Utf8Of(cl_);
        pin_ptr<Byte> cl = &clBytes[0];
        array<Byte>^ archBytes = Utf8Of(arch_);
        pin_ptr<Byte> arch = &archBytes[0];

        if (ed1_runs_here(kind, reinterpret_cast<const char*>(arch)) == 0) {
            what_->Text = FromUtf8(ed1_why_not_run(kind, reinterpret_cast<const char*>(arch)));
            return;
        }

        console_->Text =
            "$ " +
            FromUtf8(ed1_shown_run_command(reinterpret_cast<const char*>(cc1),
                                           reinterpret_cast<const char*>(cl), kind,
                                           reinterpret_cast<const char*>(source), language,
                                           reinterpret_cast<const char*>(arch), config_)) +
            "\r\n";
        panel_->SelectedIndex = 0;
        Application::DoEvents();

        Ed1Ran* ran = ed1_run(reinterpret_cast<const char*>(cc1),
                              reinterpret_cast<const char*>(cl), kind,
                              reinterpret_cast<const char*>(source), language,
                              reinterpret_cast<const char*>(arch), config_);

        console_->Text += FromUtf8(ed1_ran_output(ran))->Replace("\n", "\r\n");
        ShowConsoleEnd();

        if (ed1_ran_has_error(ran) != 0) {
            int line = ed1_ran_error_line(ran);
            int column = ed1_ran_error_column(ran);
            String^ message = FromUtf8(ed1_ran_error_message(ran));
            ed1_run_free(ran);

            RememberError(line, column, message, nullptr);
            GoTo(line, column);
            panel_->SelectedIndex = 0;   // the compiler's words are on the Console
            what_->Text = String::Format("{0}:{1}: error: {2}", line, column, message);
            return;
        }

        if (ed1_ran_built(ran) == 0) {
            what_->Text = FromUtf8(ed1_toolchain_name(kind)) + " built no program - see the console";
            ed1_run_free(ran);
            return;
        }

        int status = ed1_ran_status(ran);
        ed1_run_free(ran);

        console_->Text += String::Format("\r\n[program returned {0}]\r\n", status);
        ShowConsoleEnd();
        what_->Text = String::Format("{0} ran - it returned {1}",
                                     System::IO::Path::GetFileName(path_), status);
    }

    void OnBuildProject(Object^, EventArgs^) { BuildProject(false); }
    void OnRunProject(Object^, EventArgs^) { BuildProject(true); }

    // The project's program: the sources its build entry names, compiled and
    // linked into one thing beside the project file. It reads nothing from the
    // window - not the file in front of you, not what is selected - so it says
    // the same thing here as F4 says in the terminal.
    void BuildProject(bool andRun) {
        if (busy_) { what_->Text = "still working - give it a moment"; return; }
        ForgetError();

        if (ed1_project_target_ready(project_) == 0) {
            String^ why = FromUtf8(ed1_project_target_why(project_));
            String^ detail = FromUtf8(ed1_project_target_detail(project_));
            what_->Text = why;
            console_->Text = detail->Length > 0 ? why + "\r\n\r\n" + detail : why;
            panel_->SelectedIndex = 0;
            return;
        }

        SaveEveryDirty();

        int language = ed1_project_target_language(project_);
        int kind = ed1_resolve(toolKind_, language);
        if (ed1_can_compile(kind, language) == 0) {
            what_->Text = FromUtf8(ed1_refusal(kind, language));
            return;
        }

        array<Byte>^ archBytes = Utf8Of(arch_);
        pin_ptr<Byte> arch = &archBytes[0];
        if (andRun && ed1_runs_here(kind, reinterpret_cast<const char*>(arch)) == 0) {
            what_->Text = FromUtf8(ed1_why_not_run(kind, reinterpret_cast<const char*>(arch)));
            return;
        }

        String^ program = FromUtf8(ed1_project_target_program(project_));
        int howMany = ed1_project_target_sources(project_);

        System::Text::StringBuilder^ said = gcnew System::Text::StringBuilder();
        said->Append("$ " + FromUtf8(ed1_toolchain_name(kind)) + " " + howMany +
                     (howMany == 1 ? " source -o " : " sources -o ") + program + "\r\n");
        for (int i = 0; i < howMany; ++i)
            said->Append("    " + FromUtf8(ed1_project_target_source(project_, i)) + "\r\n");
        console_->Text = said->ToString();
        panel_->SelectedIndex = 0;
        what_->Text = "building " + System::IO::Path::GetFileName(program) + " ...";
        Application::DoEvents();

        array<Byte>^ cc1Bytes = Utf8Of(cc1_);
        pin_ptr<Byte> cc1 = &cc1Bytes[0];
        array<Byte>^ clBytes = Utf8Of(cl_);
        pin_ptr<Byte> cl = &clBytes[0];

        Ed1Build* made = ed1_build_target(project_, reinterpret_cast<const char*>(cc1),
                                          reinterpret_cast<const char*>(cl), kind,
                                          reinterpret_cast<const char*>(arch), config_);
        if (made == nullptr) {
            what_->Text = FromUtf8(ed1_project_target_why(project_));
            return;
        }

        console_->Text += FromUtf8(ed1_build_output(made))->Replace("\n", "\r\n");
        ShowConsoleEnd();

        if (ed1_build_has_error(made) != 0) {
            int line = ed1_build_error_line(made);
            int column = ed1_build_error_column(made);
            String^ message = FromUtf8(ed1_build_error_message(made));
            String^ where = FromUtf8(ed1_build_error_file(made));
            ed1_build_free(made);

            // The error is as likely as not in a file nothing has opened, so
            // it is opened before the caret is put in it.
            if (where->Length > 0) {
                if (!System::IO::Path::IsPathRooted(where)) {
                    array<Byte>^ relative = Utf8Of(where);
                    pin_ptr<Byte> relativePin = &relative[0];
                    where = FromUtf8(ed1_project_absolute(
                        project_, reinterpret_cast<const char*>(relativePin)));
                }
                if (System::IO::File::Exists(where)) OpenPath(where);
            }

            RememberError(line, column, message, where);
            GoTo(line, column);
            panel_->SelectedIndex = 0;
            what_->Text = String::Format("{0}:{1}:{2}: error: {3}",
                                         System::IO::Path::GetFileName(where), line, column,
                                         message);
            return;
        }

        bool ok = ed1_build_ok(made) != 0;
        ed1_build_free(made);

        if (!ok) {
            what_->Text = FromUtf8(ed1_toolchain_name(kind)) + " did not build it - see the console";
            return;
        }

        if (!andRun) {
            console_->Text += "\r\n[built " + program + "]\r\n";
            ShowConsoleEnd();
            what_->Text = "built " + System::IO::Path::GetFileName(program) + " from " +
                          howMany + (howMany == 1 ? " source" : " sources");
            return;
        }

        array<Byte>^ programBytes = Utf8Of(program);
        pin_ptr<Byte> programPin = &programBytes[0];
        Ed1Ran* ran = ed1_run_built(reinterpret_cast<const char*>(programPin));
        console_->Text += FromUtf8(ed1_ran_output(ran))->Replace("\n", "\r\n");
        int status = ed1_ran_status(ran);
        ed1_run_free(ran);

        console_->Text += String::Format("\r\n[program returned {0}]\r\n", status);
        ShowConsoleEnd();
        what_->Text = String::Format("ran {0} - it returned {1}",
                                     System::IO::Path::GetFileName(program), status);
    }

    // Everything with a name and an unsaved change. A project build reads
    // several files off the disk, so saving the one in front of you - which is
    // all a single file's build ever needed - would build yesterday's copy of
    // every other one.
    void SaveEveryDirty() {
        for (int i = 0; i < sheets_->Count; ++i) {
            Sheet^ sheet = sheets_[i];
            if (sheet->path == nullptr || !sheet->box->Modified) continue;
            try {
                System::IO::File::WriteAllText(sheet->path,
                                               sheet->box->Text->Replace("\r\n", "\n"));
                sheet->box->Modified = false;
                MarkTab(sheet);
            } catch (Exception^ problem) {
                what_->Text = problem->Message;
            }
        }
    }

    // ---- keeping the window awake while something slow happens -------------
    //
    // Building with cl takes seconds and starting cdb takes seconds more, and
    // a program under a debugger can sit at a breakpoint for as long as it
    // likes. Doing any of that on the thread that paints leaves a window that
    // does not repaint, cannot be moved, and cannot even be photographed -
    // which is how this was noticed, when a screenshot of it debugging C++
    // could never be taken while the same screenshot of C worked.
    //
    // So the slow part goes to another thread and this one keeps pumping
    // messages until it is done. The caller keeps its straight-line shape,
    // because when WhileBusy returns we are back on the painting thread with
    // the answer in hand. What the caller loses is the right to start a second
    // one while the first is running, which is what busy_ refuses - and that
    // matters more than it sounds: two threads in one Ed1Debugger would be
    // two conversations down one pipe.

    literal int WorkBuild = 1;
    literal int WorkStart = 2;
    literal int WorkGo = 3;
    literal int WorkStepOver = 4;
    literal int WorkStepInto = 5;
    literal int WorkStepOut = 6;
    literal int WorkResume = 7;

    // Run on the worker thread. It touches native handles and reads String^
    // members, which are immutable, and no control at all - a control touched
    // from here would throw, and rightly.
    void DoPendingWork() {
        array<Byte>^ archBytes = Utf8Of(arch_ == nullptr ? "" : arch_);
        pin_ptr<Byte> arch = &archBytes[0];

        switch (pending_) {
            case WorkBuild: {
                array<Byte>^ sourceBytes = Utf8Of(path_);
                pin_ptr<Byte> source = &sourceBytes[0];
                array<Byte>^ cc1Bytes = Utf8Of(cc1_);
                pin_ptr<Byte> cc1 = &cc1Bytes[0];
                array<Byte>^ clBytes = Utf8Of(cl_);
                pin_ptr<Byte> cl = &clBytes[0];

                built_ = ed1_build_program(reinterpret_cast<const char*>(cc1),
                                           reinterpret_cast<const char*>(cl), workKind_,
                                           reinterpret_cast<const char*>(source),
                                           workLanguage_,
                                           reinterpret_cast<const char*>(arch), config_);
                workResult_ = ed1_program_ok(built_);
                break;
            }
            case WorkStart:
                workResult_ = ed1_debugger_start(
                    debugger_, ed1_debugger_for(workKind_, reinterpret_cast<const char*>(arch)),
                    ed1_program_path(built_));
                break;
            case WorkGo:       ed1_debugger_run(debugger_); break;
            case WorkResume:   ed1_debugger_resume(debugger_); break;
            case WorkStepOver: ed1_debugger_step_over(debugger_); break;
            case WorkStepInto: ed1_debugger_step_into(debugger_); break;
            case WorkStepOut:  ed1_debugger_step_out(debugger_); break;
            default: break;
        }
    }

    // False when something slow is already running, which is the caller's cue
    // to do nothing at all.
    bool WhileBusy(int what) {
        if (busy_) { what_->Text = "still working - give it a moment"; return false; }

        busy_ = true;
        pending_ = what;
        workResult_ = 0;

        System::Threading::Thread^ worker = gcnew System::Threading::Thread(
            gcnew System::Threading::ThreadStart(this, &MainForm::DoPendingWork));
        worker->IsBackground = true;   // never keeps the program alive by itself
        worker->Start();

        while (!worker->Join(50)) Application::DoEvents();

        busy_ = false;
        return true;
    }

    // ---- stopping on a line ------------------------------------------------

    System::Collections::Generic::List<int>^ BreaksFor(String^ file) {
        if (file == nullptr) return nullptr;
        String^ key = OneName(file);
        System::Collections::Generic::List<int>^ lines = nullptr;
        if (!breaks_->TryGetValue(key, lines)) {
            lines = gcnew System::Collections::Generic::List<int>();
            breaks_[key] = lines;
        }
        breakNames_[key] = file;   // the newest spelling is the one to show
        return lines;
    }

    int CaretLine() {
        return text_->GetLineFromCharIndex(text_->SelectionStart) + 1;
    }

    void OnToggleBreak(Object^, EventArgs^) {
        if (path_ == nullptr) {
            what_->Text = "save the file first - a breakpoint is on a line of a file";
            return;
        }

        System::Collections::Generic::List<int>^ lines = BreaksFor(path_);
        int line = CaretLine();

        if (lines->Contains(line)) {
            lines->Remove(line);
            if (ed1_debugger_running(debugger_) != 0) SetEveryBreakpoint();
            what_->Text = String::Format("breakpoint off line {0}", line);
        } else {
            lines->Add(line);
            if (ed1_debugger_running(debugger_) != 0) {
                array<Byte>^ bytes = Utf8Of(path_);
                pin_ptr<Byte> pinned = &bytes[0];
                ed1_debugger_break(debugger_, reinterpret_cast<const char*>(pinned), line);
            }
            what_->Text = String::Format("breakpoint on line {0}", line);
        }
        Current()->gutter->Invalidate();
    }

    // The whole set, rather than one taken away: neither debugger promises the
    // numbering of what it hands out, and there are never enough breakpoints
    // here for the difference to matter.
    void SetEveryBreakpoint() {
        ed1_debugger_clear(debugger_);
        for each (System::Collections::Generic::KeyValuePair<String^,
                      System::Collections::Generic::List<int>^> pair in breaks_) {
            String^ named = nullptr;
            if (!breakNames_->TryGetValue(pair.Key, named)) named = pair.Key;
            array<Byte>^ bytes = Utf8Of(named);
            pin_ptr<Byte> pinned = &bytes[0];
            for each (int line in pair.Value)
                ed1_debugger_break(debugger_, reinterpret_cast<const char*>(pinned), line);
        }
    }

    void OnDebug(Object^, EventArgs^) {
        if (ed1_debugger_running(debugger_) != 0) {
            // Carrying on can take as long as the program takes to reach the
            // next breakpoint, which is why this is not done here either.
            if (!WhileBusy(WorkResume)) return;
            ShowStop();
            return;
        }

        if (path_ == nullptr) { what_->Text = "open a file first"; return; }
        ForgetError();   // this build is about to say its own
        OnSave(nullptr, nullptr);

        int language = LanguageNow();
        int kind = ed1_resolve(toolKind_, language);
        if (ed1_can_compile(kind, language) == 0) {
            what_->Text = FromUtf8(ed1_refusal(kind, language));
            return;
        }

        array<Byte>^ archBytes = Utf8Of(arch_);
        pin_ptr<Byte> arch = &archBytes[0];

        if (ed1_runs_here(kind, reinterpret_cast<const char*>(arch)) == 0) {
            what_->Text = FromUtf8(ed1_why_not_run(kind, reinterpret_cast<const char*>(arch)));
            return;
        }
        if (ed1_debugger_for(kind, reinterpret_cast<const char*>(arch)) == 0) {
            what_->Text = FromUtf8(
                ed1_no_debugger_because(kind, reinterpret_cast<const char*>(arch)));
            return;
        }
        if (config_ != ED1_CONFIG_DEBUG) {
            what_->Text = "release is built without -g - choose Debug build, then F8";
            return;
        }

        array<Byte>^ sourceBytes = Utf8Of(path_);
        pin_ptr<Byte> source = &sourceBytes[0];
        array<Byte>^ cc1Bytes = Utf8Of(cc1_);
        pin_ptr<Byte> cc1 = &cc1Bytes[0];
        array<Byte>^ clBytes = Utf8Of(cl_);
        pin_ptr<Byte> cl = &clBytes[0];

        console_->Text = "$ building for the debugger\r\n";
        panel_->SelectedIndex = 0;
        what_->Text = "building for the debugger ...";
        Application::DoEvents();

        if (built_ != nullptr) { ed1_program_free(built_); built_ = nullptr; }

        // cl runs on the other thread; this one goes on painting.
        workKind_ = kind;
        workLanguage_ = language;
        if (!WhileBusy(WorkBuild)) return;

        console_->Text += FromUtf8(ed1_program_output(built_))->Replace("\n", "\r\n");

        if (workResult_ == 0) {
            if (ed1_program_has_error(built_) != 0) {
                int line = ed1_program_error_line(built_);
                int column = ed1_program_error_column(built_);
                String^ message = FromUtf8(ed1_program_error_message(built_));
                RememberError(line, column, message, nullptr);
                GoTo(line, column);
                panel_->SelectedIndex = 0;   // the compiler's words are on the Console
                what_->Text = String::Format("{0}:{1}: error: {2}", line, column, message);
            } else {
                what_->Text = FromUtf8(ed1_toolchain_name(kind)) + " built no program - see the console";
            }
            ed1_program_free(built_);
            built_ = nullptr;
            return;
        }

        what_->Text = "starting the debugger ...";
        if (!WhileBusy(WorkStart)) return;
        if (workResult_ == 0) {
            what_->Text = FromUtf8(ed1_debugger_name(
                              ed1_debugger_for(kind, reinterpret_cast<const char*>(arch)))) +
                          " could not be started - is it installed?";
            ed1_program_free(built_);
            built_ = nullptr;
            return;
        }

        SetEveryBreakpoint();
        if (!WhileBusy(WorkGo)) return;
        ShowStop();
    }

    void OnStepOver(Object^, EventArgs^) { Step(0); }
    void OnStepInto(Object^, EventArgs^) { Step(1); }
    void OnStepOut(Object^, EventArgs^) { Step(2); }

    void Step(int how) {
        if (ed1_debugger_running(debugger_) == 0) {
            what_->Text = "nothing is running - F8 starts it";
            return;
        }
        int what = (how == 1) ? WorkStepInto : (how == 2) ? WorkStepOut : WorkStepOver;
        if (!WhileBusy(what)) return;
        ShowStop();
    }

    void OnDebugStop(Object^, EventArgs^) {
        if (ed1_debugger_running(debugger_) == 0) {
            what_->Text = "nothing is running";
            return;
        }
        EndDebugging();
        what_->Text = "debugging stopped";
    }

    void EndDebugging() {
        ed1_debugger_stop(debugger_);
        if (built_ != nullptr) { ed1_program_free(built_); built_ = nullptr; }
        stopFile_ = nullptr;
        stopLine_ = 0;
        ShowStoppedLine(-1);   // the bar goes with the arrow
        Current()->gutter->Invalidate();
    }

    void ShowStop() {
        // What the program printed on its way here belongs in the console,
        // which is where its output goes when it is run without a debugger.
        // The debugger's own words are taken out on the native side, so this
        // and the terminal half show the same thing.
        String^ printed = Lines(FromUtf8(ed1_stop_output(debugger_)));
        if (!String::IsNullOrEmpty(printed)) {
            console_->AppendText(printed);
            ShowConsoleEnd();
        }

        panel_->SelectedIndex = 1;   // the Debug tab

        if (ed1_stop_exited(debugger_) != 0) {
            int status = ed1_stop_status(debugger_);
            debug_->Text = String::Format(
                "the program ran to the end and returned {0}\r\n\r\n"
                "F8 starts it again. The breakpoints are still where you put them.", status);
            EndDebugging();
            what_->Text = String::Format("the program returned {0}", status);
            return;
        }

        if (ed1_stop_stopped(debugger_) == 0) {
            String^ heard = Lines(FromUtf8(ed1_stop_said(debugger_)));

            // Stepping off the end of main lands in the code that started the
            // program, which was not compiled here. That is a real place to be
            // standing and not a failure: the debugger stays running, F8
            // carries on from it, and Stop debugging still leaves. The
            // terminal has always said so and this said the debugger had died
            // and ended the session - the same step, two answers.
            if (ed1_stop_no_source(debugger_) != 0) {
                stopFile_ = nullptr;
                stopLine_ = 0;
                ShowStoppedLine(-1);
                Current()->gutter->Invalidate();
                debug_->Text =
                    "stopped where there is no source to show\r\n\r\n"
                    "Stepping past the end of main arrives in the code that\r\n"
                    "started it, which was not compiled here. F8 carries on to\r\n"
                    "the end, and Stop debugging leaves it.\r\n\r\n" + heard;
                what_->Text = "stopped where there is no source - F8 carries on";
                return;
            }

            // With what it said under it. The terminal half has always printed
            // this and the window said only the sentence, which is the least
            // useful moment to be told nothing: a debugger that has stopped
            // answering has usually just explained itself.
            debug_->Text = String::IsNullOrEmpty(heard)
                ? "the debugger stopped answering"
                : "the debugger stopped answering\r\n\r\n" + heard;
            EndDebugging();
            what_->Text = "the debugger stopped answering - see the Debug tab";
            return;
        }

        stopFile_ = FromUtf8(ed1_stop_file(debugger_));
        stopLine_ = ed1_stop_line(debugger_);
        String^ function = FromUtf8(ed1_stop_function(debugger_));

        // The caret follows it, but only into the file it is actually in.
        if (path_ != nullptr && stopLine_ > 0 &&
            System::IO::Path::GetFileName(stopFile_) == System::IO::Path::GetFileName(path_)) {
            GoTo(stopLine_, 1);
            ShowStoppedLine(stopLine_ - 1);
        }

        System::Text::StringBuilder^ said = gcnew System::Text::StringBuilder();
        said->AppendFormat("stopped at {0}:{1}",
                           System::IO::Path::GetFileName(stopFile_), stopLine_);
        if (!String::IsNullOrEmpty(function)) said->AppendFormat(" in {0}", function);
        said->Append("\r\n\r\n");

        int howMany = ed1_locals_count(debugger_);
        if (howMany == 0) {
            said->Append("  (nothing in scope here)\r\n");
        } else {
            for (int i = 0; i < howMany; ++i) {
                String^ type = FromUtf8(ed1_local_type(debugger_, i));
                said->AppendFormat("  {0} = {1}", FromUtf8(ed1_local_name(debugger_, i)),
                                   FromUtf8(ed1_local_value(debugger_, i)));
                if (!String::IsNullOrEmpty(type)) said->AppendFormat("   [{0}]", type);
                said->Append("\r\n");
            }
        }
        // Who is waiting for it. The first frame is where it is standing and
        // the line at the top already says that, so what is worth showing is
        // what is above it - and a program in main has nothing above it.
        int deep = ed1_stack_count(debugger_);
        if (deep > 1) {
            said->Append("\r\ncalled from\r\n");
            for (int i = 1; i < deep; ++i)
                said->AppendFormat("{0}\r\n", FromUtf8(ed1_stack_text(debugger_, i)));
        }

        said->Append("\r\nF8 carries on   F7 steps over   F6 steps into   F9 sets a breakpoint");
        if (deep > 1) said->Append("\r\nDouble-click a frame, or press enter on it, to go there");
        debug_->Text = said->ToString();

        Current()->gutter->Invalidate();
        what_->Text = String::Format("{0}:{1}{2}", System::IO::Path::GetFileName(stopFile_),
                                     stopLine_,
                                     String::IsNullOrEmpty(function) ? "" : " in " + function);
    }

    // Moves the caret and nothing else. It used to bring the Console forward as
    // well, which suited the three callers that go to a compiler's error - and
    // silently undid the fourth, which goes to the line a program stopped on
    // and had just brought the Debug tab forward. Choosing the panel is the
    // caller's business; each of the three says so for itself now.
    void RememberError(int line, int column, String^ message, String^ file) {
        errorLine_ = line;
        errorColumn_ = column;
        errorMessage_ = message;
        errorFile_ = file;
    }

    // Forgotten when a build starts, so that Enter on the console never takes
    // you to something an earlier build said and this one did not.
    void ForgetError() {
        errorLine_ = 0;
        errorColumn_ = 0;
        errorMessage_ = nullptr;
        errorFile_ = nullptr;
    }

    void GoToError() {
        if (errorMessage_ == nullptr) { what_->Text = "no error to go to"; return; }
        if (errorFile_ != nullptr && !SamePath(path_, errorFile_) &&
            System::IO::File::Exists(errorFile_))
            OpenPath(errorFile_);
        GoTo(errorLine_, errorColumn_);
        what_->Text = String::Format("{0}:{1}: error: {2}", errorLine_, errorColumn_,
                                     errorMessage_);
    }

    void OnConsoleKey(Object^, KeyEventArgs^ e) {
        if (e->KeyCode != Keys::Enter) return;
        e->SuppressKeyPress = true;   // a read-only box would beep at it
        GoToError();
    }

    void OnConsoleDoubleClick(Object^, EventArgs^) { GoToError(); }

    void OnDebugKey(Object^, KeyEventArgs^ e) {
        if (e->KeyCode != Keys::Enter) return;
        e->SuppressKeyPress = true;   // a read-only box would beep at it
        GoToFrame();
    }

    void OnDebugDoubleClick(Object^, EventArgs^) { GoToFrame(); }

    // The frame on the line that was clicked, or that the caret is on. Which
    // line that is comes from the box; which frame is on it is the core's
    // answer, matched against what the core wrote there - see dbg_frameLine.
    //
    // This goes to where the call came from and no further. The program is
    // still standing where it stopped, the arrow in the gutter still marks
    // that line, and the variables are still that frame's: going to a line is
    // not stepping.
    void GoToFrame() {
        if (debug_->Lines->Length == 0) return;
        int row = debug_->GetLineFromCharIndex(debug_->SelectionStart);
        if (row < 0 || row >= debug_->Lines->Length) return;

        pin_ptr<Byte> line = &Utf8Of(debug_->Lines[row])[0];
        int which = ed1_stack_on_line(debugger_, reinterpret_cast<const char*>(line));
        if (which < 0) {
            what_->Text = "that line is not a frame - a frame is one under \"called from\"";
            return;
        }

        String^ file = FromUtf8(ed1_stack_file(debugger_, which));
        int at = ed1_stack_line(debugger_, which);
        if (file->Length > 0 && !SamePath(path_, file) && System::IO::File::Exists(file))
            OpenPath(file);
        GoTo(at, 1);
        what_->Text = String::Format("{0}:{1} in {2} - where the call came from",
                                     System::IO::Path::GetFileName(file), at,
                                     FromUtf8(ed1_stack_function(debugger_, which)));
    }

    void GoTo(int line, int column) {
        int row = line - 1;
        if (row < 0) row = 0;
        if (row >= text_->Lines->Length) row = text_->Lines->Length - 1;

        int at = text_->GetFirstCharIndexFromLine(row) + CharacterColumn(row, column - 1);
        if (at < 0) at = 0;
        text_->Select(at, 0);
        text_->ScrollToCaret();
        Recolour();
        text_->Focus();
    }

    // A read-only box selects all of itself when it is given the keyboard,
    // which looks like a mistake rather than a highlight.
    // Shown *and* given the keyboard, as Ctrl+0 does for the project pane -
    // otherwise the panel is reachable by mouse alone, and Enter on the console
    // is a key nobody can press. Ctrl+4 is the way back to the file, as before.
    void ShowPanel(int which) {
        panel_->SelectedIndex = which;
        if (which == 0) console_->Focus();
        else if (which == 1) debug_->Focus();
        else assembly_->Focus();
        // After the focus, not before it: taking the keyboard is what makes a
        // read-only box select all of itself, so clearing the selection first
        // clears nothing. The note above this said so and I did it anyway.
        console_->SelectionLength = 0;
        debug_->SelectionLength = 0;
        assembly_->SelectionLength = 0;
    }

    void OnShowConsole(Object^, EventArgs^) { ShowPanel(0); }
    void OnShowDebug(Object^, EventArgs^) { ShowPanel(1); }
    void OnShowAssembly(Object^, EventArgs^) { ShowPanel(2); }

    // Which target, which compiler and which of debug and release - said by a
    // tick beside the one in force. They were announced on the message line
    // when picked and nowhere after that, so the next thing to happen took the
    // answer away with it; the terminal has all three on its status bar for as
    // long as the editor is running. Called wherever the three can change,
    // which includes opening a project: an ed1.json carries all of them, and a
    // tick that only followed the menus would start lying the moment one was
    // opened.
    void SayBuild() {
        if (build_ == nullptr) return;
        int language = LanguageNow();
        int kind = ed1_resolve(toolKind_, language);
        String^ said = FromUtf8(ed1_language_name(language)) + "  " +
                       FromUtf8(ed1_config_name(config_)) + "  " +
                       FromUtf8(ed1_toolchain_name(kind));
        // The star means the file picked the compiler, not the menu - the same
        // mark, in the same place, as the terminal's.
        if (toolKind_ == ED1_TOOL_AUTO) said += "*";
        // The target is shown only when it means something: cl builds for the
        // host it was installed as, and offering a choice that changes nothing
        // would be the status bar telling a lie. Editor::drawStatus says the
        // same thing in the same words.
        if (ed1_uses_arch(kind) != 0) said += "  " + arch_;
        build_->Text = said;
    }

    void ShowChoices() {
        for each (ToolStripMenuItem^ one in targetItems_)
            one->Checked = String::Equals(one->Text, arch_, StringComparison::Ordinal);
        toolAutoItem_->Checked = toolKind_ == ED1_TOOL_AUTO;
        toolCc1Item_->Checked = toolKind_ == ED1_TOOL_CC1;
        toolClItem_->Checked = toolKind_ == ED1_TOOL_MSVC;
        debugConfigItem_->Checked = config_ == ED1_CONFIG_DEBUG;
        releaseConfigItem_->Checked = config_ == ED1_CONFIG_RELEASE;
        SayBuild();
    }

    // Debug and release are two, so this is a toggle. The compilers and the
    // targets are three each, so those go round - and going round rather than
    // between two is why automatic is never more than two presses away, which
    // is the reason the terminal gives for its own.
    void NextConfig() {
        if (config_ == ED1_CONFIG_DEBUG) OnReleaseConfig(nullptr, nullptr);
        else OnDebugConfig(nullptr, nullptr);
    }

    void NextTool() {
        if (toolKind_ == ED1_TOOL_AUTO) OnToolCc1(nullptr, nullptr);
        else if (toolKind_ == ED1_TOOL_CC1) OnToolCl(nullptr, nullptr);
        else OnToolAuto(nullptr, nullptr);
    }

    void NextTarget() {
        if (targetItems_ == nullptr || targetItems_->Count == 0) return;
        int at = 0;
        for (int i = 0; i < targetItems_->Count; ++i)
            if (String::Equals(targetItems_[i]->Text, arch_, StringComparison::Ordinal)) {
                at = i;
                break;
            }
        // OnTarget reads the target's name off the item it was given, so the
        // item is what it is handed rather than a name looked up twice.
        OnTarget(targetItems_[(at + 1) % targetItems_->Count], nullptr);
    }

    void OnDebugConfig(Object^, EventArgs^) {
        config_ = ED1_CONFIG_DEBUG;
        ShowChoices();
        what_->Text = "debug";
    }
    void OnReleaseConfig(Object^, EventArgs^) {
        config_ = ED1_CONFIG_RELEASE;
        ShowChoices();
        what_->Text = "release";
    }
    void OnTarget(Object^ sender, EventArgs^) {
        arch_ = safe_cast<ToolStripMenuItem^>(sender)->Text;
        ShowChoices();
        RefreshDebugTab();   // what the target can carry is part of what it says
        what_->Text = "target: " + arch_;
    }
    void OnToolAuto(Object^, EventArgs^) {
        toolKind_ = ED1_TOOL_AUTO;
        ShowChoices();
        RefreshDebugTab();
        what_->Text = "compiler: chosen by the file";
    }
    void OnToolCc1(Object^, EventArgs^) {
        toolKind_ = ED1_TOOL_CC1;
        ShowChoices();
        RefreshDebugTab();
        what_->Text = "compiler: cc1";
    }
    void OnToolCl(Object^, EventArgs^) {
        toolKind_ = ED1_TOOL_MSVC;
        ShowChoices();
        RefreshDebugTab();
        what_->Text = "compiler: cl";
    }
};

}  // namespace ed1gui
