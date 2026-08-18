#ifndef EDITOR_BUFFER_H
#define EDITOR_BUFFER_H

#include <cstddef>
#include <string>
#include <vector>

namespace editor {

// What kind of change is being made, which decides where one undo step ends
// and the next begins. A run of the same kind is one step: typing a word and
// then undoing should give the word back, not one letter at a time.
enum EditKind {
    EditNone = 0,
    EditTyping,
    EditErasing,
    EditOther      // anything that stands alone: a newline, a re-layout, a replace
};

// The text being edited, and nothing about how it is shown. A buffer always
// holds at least one line, empty if need be, so that no caller has to ask
// whether there is a line to put the cursor on.
class Buffer {
public:
    enum LoadResult {
        Opened,   // the file was there and has been read
        NewFile,  // no such file yet; the buffer is empty and the name is kept
        Failed    // the file is there but could not be read
    };

    Buffer();

    LoadResult load(const std::string& path, std::string& error);
    bool save(std::string& error);

    size_t lineCount() const { return lines_.size(); }
    const std::string& line(size_t row) const { return lines_[row]; }
    const std::vector<std::string>& lines() const { return lines_; }

    void replaceLine(size_t row, const std::string& text);
    void replaceAll(const std::vector<std::string>& lines);

    const std::string& path() const { return path_; }
    void setPath(const std::string& path) { path_ = path; }

    bool dirty() const { return dirty_; }

    // Called before a change, with where the caret is, so that undoing puts it
    // back where it was as well as putting the text back. Snapshots rather
    // than inverse operations: a source file is a few thousand short strings,
    // and the simple version is the one that cannot be subtly wrong.
    void beginEdit(EditKind kind, size_t cx, size_t cy);

    // Ends the current run, so the next change of the same kind starts a new
    // step. Moving the caret does this, which is what stops a whole session's
    // typing from collapsing into one undo.
    void breakRun() { lastKind_ = EditNone; }

    bool undo(size_t& cx, size_t& cy);
    bool redo(size_t& cx, size_t& cy);
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    size_t undoDepth() const { return undo_.size(); }

    void insertChar(size_t row, size_t col, char c);
    void eraseChar(size_t row, size_t col);   // erases the character at col
    void splitLine(size_t row, size_t col);   // col onwards becomes a new line
    void joinLine(size_t row);                // appends row + 1 onto row

private:
    // Where the text and the caret were before one step's worth of changes.
    struct Snapshot {
        std::vector<std::string> lines;
        size_t cx;
        size_t cy;
    };

    // Enough to go back a long way and not enough to matter: a hundred copies
    // of a thousand-line file is a few megabytes, and files here are smaller.
    static const size_t kMaxSteps = 100;

    std::vector<std::string> lines_;
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    EditKind lastKind_;

    // How deep the history was when the file was last written. Undoing back to
    // that depth is undoing back to what is on disk, so the file is not
    // modified any more - which is the only way to know that without comparing
    // the whole text. -1 means the saved point has fallen off the end of the
    // capped history and can no longer be recognised.
    long savedAt_;
    std::string path_;
    bool dirty_;
    // Whether the file ended with a newline when it was read. Kept so that
    // saving gives back the file that was opened rather than quietly adding a
    // line ending the author did not put there.
    bool finalNewline_;
};

}  // namespace editor

#endif
