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
    virtual void OnShown(EventArgs^ e) override {
        Form::OnShown(e);
        text_->Select(0, 0);
        text_->Focus();
    }

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
    String^ projectDirectory_;
    String^ needle_;      // what was last searched for
    bool colouring_;

    TreeView^ tree_;
    TabControl^ files_;
    System::Collections::Generic::List<Sheet^>^ sheets_;
    // The box of whichever tab is in front. Kept as a field so that everything
    // written when there was only ever one file still reads the same.
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

    void Start(String^ projectDirectory, array<String^>^ files) {
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
        if (files != nullptr)
            for (int i = 0; i < files->Length; ++i)
                if (files[i] != nullptr) OpenPath(files[i]);
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
        file->DropDownItems->Add(
            Item("Close", Keys::Control | Keys::W, gcnew EventHandler(this, &MainForm::OnCloseFile)));
        file->DropDownItems->Add("Exit", nullptr, gcnew EventHandler(this, &MainForm::OnExit));
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
            Item("Lay out file", Keys::Control | Keys::L, gcnew EventHandler(this, &MainForm::OnLayOut)));
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
        project->DropDownItems->Add(
            Item("New file...", Keys::Control | Keys::N,
                 gcnew EventHandler(this, &MainForm::OnNewFile)));
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

        // There is always a sheet, even before a file is opened, so nothing
        // below has to ask whether there is somewhere to type.
        Sheet^ first = MakeSheet(nullptr, "");
        text_ = first->box;

        console_->Text = "cc1 or cl output appears here.  F7 builds.";
        SayDebugTab();
    }

    // A menu item with its key, since there are a dozen of them now. The
    // handler arrives already made: a managed member function has no address
    // to take, so the delegate is built at the call.
    ToolStripMenuItem^ Item(String^ label, Keys key, EventHandler^ handler) {
        ToolStripMenuItem^ item = gcnew ToolStripMenuItem(label, nullptr, handler);
        item->ShortcutKeys = key;
        return item;
    }

    // There is no input box in Windows Forms, so here is one.
    String^ Ask(String^ title, String^ initial) {
        Form^ box = gcnew Form();
        box->Text = title;
        box->FormBorderStyle = System::Windows::Forms::FormBorderStyle::FixedDialog;
        box->StartPosition = System::Windows::Forms::FormStartPosition::CenterParent;
        box->MinimizeBox = false;
        box->MaximizeBox = false;
        box->ClientSize = System::Drawing::Size(440, 96);

        TextBox^ entry = gcnew TextBox();
        entry->Text = initial == nullptr ? "" : initial;
        entry->SetBounds(12, 16, 416, 26);
        entry->Font = gcnew System::Drawing::Font("Consolas", 10.0f);
        entry->SelectAll();

        Button^ yes = gcnew Button();
        yes->Text = "OK";
        yes->DialogResult = System::Windows::Forms::DialogResult::OK;
        yes->SetBounds(266, 56, 78, 28);

        Button^ no = gcnew Button();
        no->Text = "Cancel";
        no->DialogResult = System::Windows::Forms::DialogResult::Cancel;
        no->SetBounds(350, 56, 78, 28);

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

    Sheet^ SheetFor(String^ path) {
        for (int i = 0; i < sheets_->Count; ++i)
            if (sheets_[i]->path != nullptr &&
                String::Equals(sheets_[i]->path, path, StringComparison::OrdinalIgnoreCase))
                return sheets_[i];
        return nullptr;
    }

    // A tab, a box, and the numbers down its left.
    Sheet^ MakeSheet(String^ path, String^ contents) {
        Sheet^ sheet = gcnew Sheet();
        sheet->path = path;

        sheet->box = gcnew RichTextBox();
        sheet->box->Dock = DockStyle::Fill;
        sheet->box->Font = gcnew System::Drawing::Font("Consolas", 11.0f);
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

        sheet->gutter = gcnew Panel();
        sheet->gutter->Dock = DockStyle::Left;
        sheet->gutter->Width = 52;
        sheet->gutter->BackColor = System::Drawing::Color::FromArgb(245, 245, 245);
        sheet->gutter->Tag = sheet->box;
        sheet->gutter->Paint += gcnew PaintEventHandler(this, &MainForm::OnGutterPaint);
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
                                : "ed1 - " + System::IO::Path::GetFileName(path_);
        what_->Text = path_ == nullptr
                          ? "untitled"
                          : System::IO::Path::GetFileName(path_) + "  " +
                                text_->Lines->Length + " lines";
        text_->Focus();
        sheet->gutter->Invalidate();
    }

    // ---- the numbers down the left ----------------------------------------

    void OnTextChanged(Object^, EventArgs^) {
        Sheet^ sheet = Current();
        if (sheet == nullptr) return;

        // Wide enough for the last line, and it does not shrink back as you
        // scroll - a gutter that changed width would take the text with it.
        int digits = sheet->box->Lines->Length < 1 ? 1
                                                   : sheet->box->Lines->Length.ToString()->Length;
        int wanted = 22 + 9 * digits;
        if (wanted > sheet->gutter->Width) sheet->gutter->Width = wanted;
        sheet->gutter->Invalidate();
    }

    void OnScrolled(Object^, EventArgs^) {
        Sheet^ sheet = Current();
        if (sheet != nullptr) sheet->gutter->Invalidate();
    }

    void OnGutterPaint(Object^ sender, PaintEventArgs^ e) {
        Panel^ panel = safe_cast<Panel^>(sender);
        RichTextBox^ box = safe_cast<RichTextBox^>(panel->Tag);
        if (box == nullptr) return;

        int lines = box->Lines->Length;
        if (lines < 1) lines = 1;

        int first = box->GetLineFromCharIndex(box->GetCharIndexFromPosition(
            System::Drawing::Point(1, 1)));
        int caretLine = box->GetLineFromCharIndex(box->SelectionStart);

        System::Drawing::Brush^ quiet =
            gcnew System::Drawing::SolidBrush(System::Drawing::Color::FromArgb(150, 150, 150));
        System::Drawing::Brush^ here =
            gcnew System::Drawing::SolidBrush(System::Drawing::Color::FromArgb(60, 60, 60));

        for (int row = first; row < lines; ++row) {
            int at = box->GetFirstCharIndexFromLine(row);
            if (at < 0) break;
            System::Drawing::Point where = box->GetPositionFromCharIndex(at);
            if (where.Y > panel->Height) break;

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

    void OnUndo(Object^, EventArgs^) {
        // The box keeps its own history, and it is the one the typing went
        // into - there is no sense in keeping a second one beside it.
        if (text_->CanUndo) text_->Undo();
    }
    void OnRedo(Object^, EventArgs^) { text_->Redo(); }
    void OnCut(Object^, EventArgs^) { text_->Cut(); }
    void OnCopy(Object^, EventArgs^) { text_->Copy(); }
    void OnPaste(Object^, EventArgs^) {
        text_->Paste();
        Recolour();
    }
    void OnSelectAll(Object^, EventArgs^) { text_->SelectAll(); }

    // ---- finding ----------------------------------------------------------

    void OnFind(Object^, EventArgs^) {
        String^ want = Ask("Find", needle_);
        if (want == nullptr || want->Length == 0) return;
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
        text_->Focus();
        what_->Text = String::Format("{0} - line {1}", needle_, foundRow + 1);
    }

    void OnReplace(Object^, EventArgs^) {
        String^ want = Ask("Replace what", needle_);
        if (want == nullptr || want->Length == 0) return;
        String^ with = Ask("Replace \"" + want + "\" with", "");
        if (with == nullptr) return;

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
        if (pick->ShowDialog() != System::Windows::Forms::DialogResult::OK) return;
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
            what_->Text = why->Length > 0 ? why : "no ed1.json in that directory";

            // No project file still leaves a directory that paths are counted
            // from, so the file commands work either way.
            ed1_project_set_root(project_, reinterpret_cast<const char*>(pinned));
            return;
        }

        FillTree();

        indentWidth_ = ed1_project_indent_width(project_);
        indentTabs_ = ed1_project_indent_tabs(project_);
        indentCase_ = ed1_project_case_indent(project_);
        toolKind_ = ed1_project_toolchain(project_);
        config_ = ed1_project_config(project_);
        arch_ = FromUtf8(ed1_project_arch(project_));

        what_->Text = String::Format("{0} - {1} groups",
                                     FromUtf8(ed1_project_name(project_)),
                                     ed1_project_groups(project_));
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

        what_->Text = String::Format("{0} - {1} groups",
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
        String^ name = Ask("New file (name, or one directory and a name)", "");
        if (name == nullptr || name->Length == 0) return;

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

    void OnRenameFile(Object^, EventArgs^) {
        String^ target = TargetFile();
        if (target == nullptr) { what_->Text = "no file to rename"; return; }

        array<Byte>^ was = Utf8Of(target);
        pin_ptr<Byte> wasPin = &was[0];
        String^ shown = FromUtf8(ed1_project_relative(project_,
                                                      reinterpret_cast<const char*>(wasPin)));

        String^ name = Ask("Rename " + shown + " to", shown);
        if (name == nullptr || name->Length == 0) return;

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
            if (!String::Equals(sheets_[i]->path, target, StringComparison::OrdinalIgnoreCase))
                continue;
            sheets_[i]->path = now;
            sheets_[i]->page->Text = System::IO::Path::GetFileName(now);
        }
        if (String::Equals(path_, target, StringComparison::OrdinalIgnoreCase)) {
            path_ = now;
            Text = "ed1 - " + System::IO::Path::GetFileName(now);
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
            if (!String::Equals(sheets_[i]->path, target, StringComparison::OrdinalIgnoreCase))
                continue;
            Sheet^ sheet = sheets_[i];
            sheets_->Remove(sheet);
            files_->TabPages->Remove(sheet->page);
        }
        if (sheets_->Count == 0) MakeSheet(nullptr, "");
        OnSheetChanged(nullptr, nullptr);
        FillTree();
    }

    void OnMoveToGroup(Object^, EventArgs^) {
        String^ target = TargetFile();
        if (target == nullptr) { what_->Text = "no file to move"; return; }

        String^ group = Ask("Move to group", GroupUnderCursor());
        if (group == nullptr || group->Length == 0) return;

        array<Byte>^ path = Utf8Of(target);
        pin_ptr<Byte> pathPin = &path[0];
        array<Byte>^ into = Utf8Of(group);
        pin_ptr<Byte> intoPin = &into[0];

        if (Did(ed1_move_to_group(project_, reinterpret_cast<const char*>(pathPin),
                                  reinterpret_cast<const char*>(intoPin))))
            FillTree();
    }

    void OnAddThisFile(Object^, EventArgs^) {
        if (path_ == nullptr) { what_->Text = "save the file first, so it has a name"; return; }

        String^ group = Ask("Add to group", "Sources");
        if (group == nullptr || group->Length == 0) return;

        array<Byte>^ path = Utf8Of(path_);
        pin_ptr<Byte> pathPin = &path[0];
        array<Byte>^ into = Utf8Of(group);
        pin_ptr<Byte> intoPin = &into[0];

        if (Did(ed1_add_existing(project_, reinterpret_cast<const char*>(pathPin),
                                 reinterpret_cast<const char*>(intoPin))))
            FillTree();
    }

    void OnNewProject(Object^, EventArgs^) {
        String^ name = Ask("Project name", "Project");
        if (name == nullptr || name->Length == 0) return;

        array<Byte>^ where = Utf8Of(projectDirectory_ == nullptr ? "." : projectDirectory_);
        pin_ptr<Byte> wherePin = &where[0];
        array<Byte>^ called = Utf8Of(name);
        pin_ptr<Byte> calledPin = &called[0];
        array<Byte>^ first = Utf8Of(path_ == nullptr ? "" : path_);
        pin_ptr<Byte> firstPin = &first[0];

        if (Did(ed1_begin_project(project_, reinterpret_cast<const char*>(wherePin),
                                  reinterpret_cast<const char*>(calledPin),
                                  reinterpret_cast<const char*>(firstPin))))
            FillTree();
    }

    void OnSaveProject(Object^, EventArgs^) { Did(ed1_save_project(project_)); }

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
            spare->box->Modified = false;
            spare->page->Text = System::IO::Path::GetFileName(path);
            sheet = spare;
        } else {
            sheet = MakeSheet(path, contents);
        }
        text_ = sheet->box;
        path_ = path;
        Text = "ed1 - " + System::IO::Path::GetFileName(path);
        Recolour();
        OnTextChanged(nullptr, nullptr);
        text_->Select(0, 0);
        text_->Focus();
        what_->Text = System::IO::Path::GetFileName(path) + "  " + text_->Lines->Length + " lines";
    }

    void OnCloseFile(Object^, EventArgs^) {
        Sheet^ sheet = Current();
        if (sheet == nullptr) return;

        sheets_->Remove(sheet);
        files_->TabPages->Remove(sheet->page);
        if (sheets_->Count == 0) {
            MakeSheet(nullptr, "");
            OnSheetChanged(nullptr, nullptr);
        }
        what_->Text = "closed";
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

        int at = text_->GetFirstCharIndexFromLine(row) + CharacterColumn(row, column - 1);
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
