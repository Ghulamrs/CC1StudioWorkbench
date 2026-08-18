#include "buffer.h"

#include <cerrno>
#include <cstring>
#include <fstream>

namespace editor {

Buffer::Buffer()
    : lines_(1, std::string()), lastKind_(EditNone), savedAt_(0), dirty_(false),
      finalNewline_(true) {}

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

    if (undo_.size() > kMaxSteps) {
        undo_.erase(undo_.begin());
        // Everything below has shifted down one. If the saved point was the
        // one just dropped, it can never be recognised again.
        if (savedAt_ > 0) --savedAt_;
        else savedAt_ = -1;
    }

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

    // Back at the depth the file was written at is back at what is on disk.
    dirty_ = !(savedAt_ >= 0 && static_cast<size_t>(savedAt_) == undo_.size());
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

    dirty_ = !(savedAt_ >= 0 && static_cast<size_t>(savedAt_) == undo_.size());
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
    savedAt_ = 0;
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

    // Where the history stood when this text reached the disk, so that undoing
    // back to here can be recognised as being unmodified again.
    savedAt_ = static_cast<long>(undo_.size());
    // Whatever is typed next starts a step of its own rather than joining the
    // run that was in progress - otherwise the saved depth would be a depth
    // the text no longer matches.
    lastKind_ = EditNone;
    dirty_ = false;
    return true;
}

Range ordered(size_t rowA, size_t colA, size_t rowB, size_t colB) {
    Range range;
    bool firstIsEarlier = (rowA < rowB) || (rowA == rowB && colA <= colB);
    range.fromRow = firstIsEarlier ? rowA : rowB;
    range.fromCol = firstIsEarlier ? colA : colB;
    range.toRow = firstIsEarlier ? rowB : rowA;
    range.toCol = firstIsEarlier ? colB : colA;
    return range;
}

std::string Buffer::textIn(const Range& range) const {
    if (range.fromRow >= lines_.size()) return std::string();

    size_t lastRow = range.toRow < lines_.size() ? range.toRow : lines_.size() - 1;
    size_t from = range.fromCol < lines_[range.fromRow].size() ? range.fromCol
                                                              : lines_[range.fromRow].size();
    size_t to = range.toCol < lines_[lastRow].size() ? range.toCol : lines_[lastRow].size();

    if (range.fromRow == lastRow) {
        if (to <= from) return std::string();
        return lines_[range.fromRow].substr(from, to - from);
    }

    std::string out = lines_[range.fromRow].substr(from);
    for (size_t row = range.fromRow + 1; row < lastRow; ++row) {
        out += "\n";
        out += lines_[row];
    }
    out += "\n";
    out += lines_[lastRow].substr(0, to);
    return out;
}

void Buffer::eraseRange(const Range& range) {
    if (range.fromRow >= lines_.size()) return;

    size_t lastRow = range.toRow < lines_.size() ? range.toRow : lines_.size() - 1;
    size_t from = range.fromCol < lines_[range.fromRow].size() ? range.fromCol
                                                              : lines_[range.fromRow].size();
    size_t to = range.toCol < lines_[lastRow].size() ? range.toCol : lines_[lastRow].size();

    if (range.fromRow == lastRow) {
        if (to <= from) return;
        lines_[range.fromRow].erase(from, to - from);
        dirty_ = true;
        return;
    }

    // What is left of the first line, joined to what is left of the last, and
    // everything between them gone.
    lines_[range.fromRow] = lines_[range.fromRow].substr(0, from) +
                            lines_[lastRow].substr(to);
    lines_.erase(lines_.begin() + static_cast<long>(range.fromRow) + 1,
                 lines_.begin() + static_cast<long>(lastRow) + 1);
    dirty_ = true;
}

void Buffer::insertText(size_t row, size_t col, const std::string& text,
                        size_t& endRow, size_t& endCol) {
    if (row >= lines_.size()) row = lines_.size() - 1;
    if (col > lines_[row].size()) col = lines_[row].size();

    std::vector<std::string> pieces;
    std::string piece;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            pieces.push_back(piece);
            piece.clear();
        } else if (text[i] != '\r') {
            piece += text[i];
        }
    }
    pieces.push_back(piece);

    if (pieces.size() == 1) {
        lines_[row].insert(col, pieces[0]);
        endRow = row;
        endCol = col + pieces[0].size();
        dirty_ = true;
        return;
    }

    std::string tail = lines_[row].substr(col);
    lines_[row] = lines_[row].substr(0, col) + pieces[0];

    for (size_t i = 1; i < pieces.size(); ++i) {
        std::string line = pieces[i];
        if (i + 1 == pieces.size()) line += tail;
        lines_.insert(lines_.begin() + static_cast<long>(row) + static_cast<long>(i), line);
    }

    endRow = row + pieces.size() - 1;
    endCol = pieces[pieces.size() - 1].size();
    dirty_ = true;
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
