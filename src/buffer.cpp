#include "buffer.h"

#include <cerrno>
#include <cstring>
#include <fstream>

namespace editor {

Buffer::Buffer()
    : lines_(1, std::string()), lastKind_(EditNone), dirty_(false), finalNewline_(true) {}

void Buffer::beginEdit(EditKind kind, size_t cx, size_t cy) {
    // Only typing and erasing run together. EditOther means exactly what it
    // says - a newline, a re-layout, a replace - and each one is its own step
    // even when the last change was also one.
    bool runs = (kind == EditTyping || kind == EditErasing);
    if (runs && kind == lastKind_ && !undo_.empty()) return;

    Snapshot before;
    before.lines = lines_;
    before.cx = cx;
    before.cy = cy;
    undo_.push_back(before);

    if (undo_.size() > kMaxSteps) undo_.erase(undo_.begin());

    // Anything done afresh throws away what was undone: there is one past, and
    // a future only for as long as nothing else happens.
    redo_.clear();
    lastKind_ = kind;
}

bool Buffer::undo(size_t& cx, size_t& cy) {
    if (undo_.empty()) return false;

    Snapshot now;
    now.lines = lines_;
    now.cx = cx;
    now.cy = cy;
    redo_.push_back(now);

    Snapshot before = undo_.back();
    undo_.pop_back();
    lines_ = before.lines;
    cx = before.cx;
    cy = before.cy;

    // Marked changed either way. Undoing back to what was last saved leaves
    // the file on disk right and the star showing, which errs the safe way.
    dirty_ = true;
    lastKind_ = EditNone;
    return true;
}

bool Buffer::redo(size_t& cx, size_t& cy) {
    if (redo_.empty()) return false;

    Snapshot now;
    now.lines = lines_;
    now.cx = cx;
    now.cy = cy;
    undo_.push_back(now);

    Snapshot ahead = redo_.back();
    redo_.pop_back();
    lines_ = ahead.lines;
    cx = ahead.cx;
    cy = ahead.cy;

    dirty_ = true;
    lastKind_ = EditNone;
    return true;
}

Buffer::LoadResult Buffer::load(const std::string& path, std::string& error) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        path_ = path;
        if (errno == ENOENT) return NewFile;
        error = path + ": " + std::strerror(errno);
        return Failed;
    }

    std::vector<std::string> lines;
    std::string line;
    bool sawNewline = false;
    while (std::getline(in, line)) {
        // getline strips the newline but not the carriage return a file
        // written on Windows carries in front of it.
        if (!line.empty() && line[line.size() - 1] == '\r') line.resize(line.size() - 1);
        lines.push_back(line);
        sawNewline = !in.eof();
    }
    if (in.bad()) {
        error = path + ": " + std::strerror(errno);
        return Failed;
    }

    if (lines.empty()) lines.push_back(std::string());

    lines_.swap(lines);
    path_ = path;
    // A file just opened has no past worth going back to.
    undo_.clear();
    redo_.clear();
    lastKind_ = EditNone;
    dirty_ = false;
    finalNewline_ = sawNewline || lines_.size() == 1;
    return Opened;
}

bool Buffer::save(std::string& error) {
    if (path_.empty()) {
        error = "no file name";
        return false;
    }

    std::ofstream out(path_.c_str(), std::ios::binary | std::ios::trunc);
    if (!out) {
        error = path_ + ": " + std::strerror(errno);
        return false;
    }

    for (size_t i = 0; i < lines_.size(); ++i) {
        out << lines_[i];
        if (i + 1 < lines_.size() || finalNewline_) out << '\n';
    }
    out.flush();
    if (!out) {
        error = path_ + ": " + std::strerror(errno);
        return false;
    }

    dirty_ = false;
    return true;
}

void Buffer::replaceLine(size_t row, const std::string& text) {
    if (lines_[row] == text) return;
    lines_[row] = text;
    dirty_ = true;
}

void Buffer::replaceAll(const std::vector<std::string>& lines) {
    if (lines_ == lines) return;
    lines_ = lines;
    if (lines_.empty()) lines_.push_back(std::string());
    dirty_ = true;
}

void Buffer::insertChar(size_t row, size_t col, char c) {
    std::string& text = lines_[row];
    if (col > text.size()) col = text.size();
    text.insert(text.begin() + static_cast<long>(col), c);
    dirty_ = true;
}

void Buffer::eraseChar(size_t row, size_t col) {
    std::string& text = lines_[row];
    if (col >= text.size()) return;
    text.erase(text.begin() + static_cast<long>(col));
    dirty_ = true;
}

void Buffer::splitLine(size_t row, size_t col) {
    std::string& text = lines_[row];
    if (col > text.size()) col = text.size();
    std::string tail = text.substr(col);
    text.resize(col);
    lines_.insert(lines_.begin() + static_cast<long>(row) + 1, tail);
    dirty_ = true;
}

void Buffer::joinLine(size_t row) {
    if (row + 1 >= lines_.size()) return;
    lines_[row] += lines_[row + 1];
    lines_.erase(lines_.begin() + static_cast<long>(row) + 1);
    dirty_ = true;
}

}  // namespace editor
