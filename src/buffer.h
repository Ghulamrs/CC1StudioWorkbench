#ifndef EDITOR_BUFFER_H
#define EDITOR_BUFFER_H

#include <cstddef>
#include <string>
#include <vector>

namespace editor {

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

    void insertChar(size_t row, size_t col, char c);
    void eraseChar(size_t row, size_t col);   // erases the character at col
    void splitLine(size_t row, size_t col);   // col onwards becomes a new line
    void joinLine(size_t row);                // appends row + 1 onto row

private:
    std::vector<std::string> lines_;
    std::string path_;
    bool dirty_;
    // Whether the file ended with a newline when it was read. Kept so that
    // saving gives back the file that was opened rather than quietly adding a
    // line ending the author did not put there.
    bool finalNewline_;
};

}  // namespace editor

#endif
