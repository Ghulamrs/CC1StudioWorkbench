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

public ref class MainForm : public Form {
public:
    MainForm() { Start(nullptr, nullptr); }
    MainForm(String^ projectDirectory, String^ file) { Start(projectDirectory, file); }

protected:
    ~MainForm() { this->!MainForm(); }
    !MainForm() {
        if (project_ != nullptr) {
            ed1_project_free(project_);
            project_ = nullptr;
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

    String^ path_;
    bool colouring_;

    TreeView^ tree_;
    RichTextBox^ text_;
    TabControl^ panel_;
    TextBox^ console_;
    TextBox^ debug_;
    RichTextBox^ assembly_;
    StatusStrip^ status_;
    ToolStripStatusLabel^ where_;
    ToolStripStatusLabel^ what_;

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

    void Start(String^ projectDirectory, String^ file) {
        project_ = ed1_project_new();
        arch_ = "x86_64-windows";
        cc1_ = "cc1";
        cl_ = "cl";
        toolKind_ = ED1_TOOL_AUTO;
        config_ = ED1_CONFIG_DEBUG;
        indentWidth_ = 4;
        indentTabs_ = 0;
        indentCase_ = 0;

        Lay();

        if (projectDirectory != nullptr) LoadProject(projectDirectory);
        if (file != nullptr) OpenPath(file);
    }

    void Lay() {
        Text = "ed1";
        Width = 1100;
        Height = 760;
        colouring_ = false;

        MenuStrip^ bar = gcnew MenuStrip();

        ToolStripMenuItem^ file = gcnew ToolStripMenuItem("&File");
        file->DropDownItems->Add("Open project...", nullptr,
                                 gcnew EventHandler(this, &MainForm::OnOpenProject));
        file->DropDownItems->Add("Open file...", nullptr,
                                 gcnew EventHandler(this, &MainForm::OnOpenFile));
        ToolStripMenuItem^ save = gcnew ToolStripMenuItem(
            "Save", nullptr, gcnew EventHandler(this, &MainForm::OnSave));
        save->ShortcutKeys = static_cast<Keys>(Keys::Control | Keys::S);
        file->DropDownItems->Add(save);
        file->DropDownItems->Add("Exit", nullptr, gcnew EventHandler(this, &MainForm::OnExit));
        bar->Items->Add(file);

        ToolStripMenuItem^ edit = gcnew ToolStripMenuItem("&Edit");
        ToolStripMenuItem^ layout = gcnew ToolStripMenuItem(
            "Lay out file", nullptr, gcnew EventHandler(this, &MainForm::OnLayOut));
        layout->ShortcutKeys = static_cast<Keys>(Keys::Control | Keys::L);
        edit->DropDownItems->Add(layout);
        bar->Items->Add(edit);

        ToolStripMenuItem^ build = gcnew ToolStripMenuItem("&Build");
        ToolStripMenuItem^ compile = gcnew ToolStripMenuItem(
            "Compile", nullptr, gcnew EventHandler(this, &MainForm::OnCompile));
        compile->ShortcutKeys = Keys::F7;
        build->DropDownItems->Add(compile);
        build->DropDownItems->Add("Debug", nullptr,
                                  gcnew EventHandler(this, &MainForm::OnDebugConfig));
        build->DropDownItems->Add("Release", nullptr,
                                  gcnew EventHandler(this, &MainForm::OnReleaseConfig));
        bar->Items->Add(build);

        ToolStripMenuItem^ target = gcnew ToolStripMenuItem("&Target");
        for (int i = 0; i < 3; ++i)
            target->DropDownItems->Add(gcnew ToolStripMenuItem(
                FromUtf8(ed1_arch(i)), nullptr, gcnew EventHandler(this, &MainForm::OnTarget)));
        bar->Items->Add(target);

        ToolStripMenuItem^ tools = gcnew ToolStripMenuItem("Too&ls");
        tools->DropDownItems->Add("By language", nullptr,
                                  gcnew EventHandler(this, &MainForm::OnToolAuto));
        tools->DropDownItems->Add("cc1", nullptr, gcnew EventHandler(this, &MainForm::OnToolCc1));
        tools->DropDownItems->Add("MSVC (cl)", nullptr,
                                  gcnew EventHandler(this, &MainForm::OnToolCl));
        bar->Items->Add(tools);

        MainMenuStrip = bar;
        Controls->Add(bar);

        // The same four regions as the terminal one: project, text, panel and
        // status - by splitters here instead of by counting rows.
        SplitContainer^ outer = gcnew SplitContainer();
        outer->Dock = DockStyle::Fill;
        outer->Orientation = Orientation::Horizontal;

        SplitContainer^ upper = gcnew SplitContainer();
        upper->Dock = DockStyle::Fill;

        tree_ = gcnew TreeView();
        tree_->Dock = DockStyle::Fill;
        tree_->NodeMouseDoubleClick +=
            gcnew TreeNodeMouseClickEventHandler(this, &MainForm::OnTreeOpen);
        upper->Panel1->Controls->Add(tree_);

        text_ = gcnew RichTextBox();
        text_->Dock = DockStyle::Fill;
        text_->Font = gcnew System::Drawing::Font("Consolas", 11.0f);
        text_->WordWrap = false;
        text_->AcceptsTab = true;
        text_->HideSelection = false;
        text_->KeyDown += gcnew KeyEventHandler(this, &MainForm::OnKeyDown);
        text_->SelectionChanged += gcnew EventHandler(this, &MainForm::OnCaretMoved);
        upper->Panel2->Controls->Add(text_);
        outer->Panel1->Controls->Add(upper);

        panel_ = gcnew TabControl();
        panel_->Dock = DockStyle::Fill;

        console_ = ReadOnlyBox();
        debug_ = ReadOnlyBox();
        assembly_ = gcnew RichTextBox();
        assembly_->Dock = DockStyle::Fill;
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

        status_ = gcnew StatusStrip();
        what_ = gcnew ToolStripStatusLabel("no file");
        where_ = gcnew ToolStripStatusLabel("1:1");
        status_->Items->Add(what_);
        status_->Items->Add(where_);
        Controls->Add(status_);

        console_->Text = "cc1 or cl output appears here.  F7 builds.";
        SayDebugTab();
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

    void SayDebugTab() {
        // The same thing the terminal one says, and for the same reason.
        debug_->Text = String::Join(
            "\r\n", gcnew array<String^>{
                        "Variables, watches and the call stack belong here.", "",
                        "Nothing to show yet: cc1 emits no debug information -",
                        "no -g, no DWARF, no CodeView - so a debugger has no",
                        "symbols to read. Until the compiler emits some, this",
                        "tab stays empty rather than inventing values."});
    }

    // ---- laying out and colouring -----------------------------------------

    int LanguageNow() {
        array<Byte>^ bytes = Utf8Of(path_ == nullptr ? "" : path_);
        pin_ptr<Byte> pinned = &bytes[0];
        return ed1_language_for(reinterpret_cast<const char*>(pinned));
    }

    void OnLayOut(Object^, EventArgs^) {
        array<Byte>^ bytes = Utf8Of(text_->Text->Replace("\r\n", "\n"));
        pin_ptr<Byte> pinned = &bytes[0];

        String^ laid = TakeUtf8(ed1_reindent(reinterpret_cast<const char*>(pinned),
                                             indentWidth_, indentTabs_, indentCase_));

        int caret = text_->SelectionStart;
        text_->Text = laid->Replace("\n", "\r\n");
        text_->SelectionStart = Math::Min(caret, text_->TextLength);
        Recolour();
        what_->Text = String::Format("laid out - {0} lines", text_->Lines->Length);
    }

    void OnKeyDown(Object^, KeyEventArgs^ e) {
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

    void Recolour() {
        if (colouring_) return;
        colouring_ = true;

        int caret = text_->SelectionStart;
        int language = LanguageNow();
        int state = 0;
        int at = 0;

        array<String^>^ all = text_->Lines;
        for (int row = 0; row < all->Length; ++row) {
            array<Byte>^ bytes = Utf8Of(all[row]);
            pin_ptr<Byte> linePin = &bytes[0];

            array<Byte>^ kinds = gcnew array<Byte>(bytes->Length);
            pin_ptr<Byte> kindPin = &kinds[0];

            int howMany = ed1_highlight(reinterpret_cast<const char*>(linePin), language,
                                        &state, kindPin, kinds->Length);

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
            at += all[row]->Length + 1;
        }

        text_->Select(caret, 0);
        text_->SelectionColor = System::Drawing::Color::Black;
        colouring_ = false;
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

    void OnCaretMoved(Object^, EventArgs^) {
        int caret = text_->SelectionStart;
        int row = text_->GetLineFromCharIndex(caret);
        where_->Text =
            String::Format("{0}:{1}", row + 1, caret - text_->GetFirstCharIndexFromLine(row) + 1);
    }

    // ---- files and the project --------------------------------------------

    void OnOpenProject(Object^, EventArgs^) {
        FolderBrowserDialog^ pick = gcnew FolderBrowserDialog();
        if (pick->ShowDialog() != System::Windows::Forms::DialogResult::OK) return;
        LoadProject(pick->SelectedPath);
    }

    void LoadProject(String^ directory) {
        tree_->Nodes->Clear();

        array<Byte>^ bytes = Utf8Of(directory);
        pin_ptr<Byte> pinned = &bytes[0];

        array<Byte>^ error = gcnew array<Byte>(512);
        pin_ptr<Byte> errorPin = &error[0];

        int loaded = ed1_project_load(project_, reinterpret_cast<const char*>(pinned),
                                      reinterpret_cast<char*>(errorPin), error->Length);
        if (loaded == 0) {
            String^ why = FromUtf8(reinterpret_cast<const char*>(errorPin));
            what_->Text = why->Length > 0 ? why : "no ed1.json in that directory";
            return;
        }

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

        what_->Text = String::Format("{0} - {1} groups",
                                     FromUtf8(ed1_project_name(project_)), groups);
    }

    void OnTreeOpen(Object^, TreeNodeMouseClickEventArgs^ e) {
        if (e->Node == nullptr || e->Node->Tag == nullptr) return;
        OpenPath(safe_cast<String^>(e->Node->Tag));
    }

    void OnOpenFile(Object^, EventArgs^) {
        OpenFileDialog^ pick = gcnew OpenFileDialog();
        pick->Filter = "C and C++|*.c;*.h;*.cpp;*.hpp|All files|*.*";
        if (pick->ShowDialog() != System::Windows::Forms::DialogResult::OK) return;
        OpenPath(pick->FileName);
    }

    void OpenPath(String^ path) {
        try {
            text_->Text = System::IO::File::ReadAllText(path);
        } catch (Exception^ problem) {
            what_->Text = problem->Message;
            return;
        }
        path_ = path;
        Text = "ed1 - " + System::IO::Path::GetFileName(path);
        Recolour();
        what_->Text = System::IO::Path::GetFileName(path) + "  " + text_->Lines->Length + " lines";
    }

    void OnSave(Object^, EventArgs^) {
        if (path_ == nullptr) return;
        System::IO::File::WriteAllText(path_, text_->Text);
        what_->Text = System::IO::Path::GetFileName(path_) + " written";
    }

    void OnExit(Object^, EventArgs^) { Close(); }

    // ---- building ----------------------------------------------------------

    void OnCompile(Object^, EventArgs^) {
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

        if (ed1_build_has_error(built) != 0) {
            int line = ed1_build_error_line(built);
            int column = ed1_build_error_column(built);
            String^ message = FromUtf8(ed1_build_error_message(built));
            ed1_build_free(built);

            GoTo(line, column);
            what_->Text = String::Format("{0}:{1}: error: {2}", line, column, message);
            return;
        }

        if (ed1_build_ok(built) == 0) {
            what_->Text = FromUtf8(ed1_toolchain_name(kind)) + " failed - see the console";
            ed1_build_free(built);
            return;
        }

        assembly_->Text = FromUtf8(ed1_build_assembly(built))->Replace("\n", "\r\n");
        int lines = ed1_build_assembly_lines(built);
        ed1_build_free(built);

        panel_->SelectedIndex = 2;
        what_->Text = String::Format("{0} lines of assembly", lines);
    }

    void GoTo(int line, int column) {
        int row = line - 1;
        if (row < 0) row = 0;
        if (row >= text_->Lines->Length) row = text_->Lines->Length - 1;

        int at = text_->GetFirstCharIndexFromLine(row) + column - 1;
        if (at < 0) at = 0;
        text_->Select(at, 0);
        text_->ScrollToCaret();
        text_->Focus();
        panel_->SelectedIndex = 0;
    }

    void OnDebugConfig(Object^, EventArgs^) {
        config_ = ED1_CONFIG_DEBUG;
        what_->Text = "debug";
    }
    void OnReleaseConfig(Object^, EventArgs^) {
        config_ = ED1_CONFIG_RELEASE;
        what_->Text = "release";
    }
    void OnTarget(Object^ sender, EventArgs^) {
        arch_ = safe_cast<ToolStripMenuItem^>(sender)->Text;
        what_->Text = "target: " + arch_;
    }
    void OnToolAuto(Object^, EventArgs^) {
        toolKind_ = ED1_TOOL_AUTO;
        what_->Text = "compiler: chosen by the file";
    }
    void OnToolCc1(Object^, EventArgs^) {
        toolKind_ = ED1_TOOL_CC1;
        what_->Text = "compiler: cc1";
    }
    void OnToolCl(Object^, EventArgs^) {
        toolKind_ = ED1_TOOL_MSVC;
        what_->Text = "compiler: cl";
    }
};

}  // namespace ed1gui
