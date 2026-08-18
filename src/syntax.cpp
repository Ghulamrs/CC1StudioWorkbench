#include "syntax.h"

#include <cstring>

namespace editor {

namespace {

bool identChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_';
}

bool digit(char c) { return c >= '0' && c <= '9'; }

// C89 through C11, which is what cc1 accepts.
const char* const kCKeywords[] = {
    "auto", "break", "case", "continue", "default", "do", "else", "enum", "extern",
    "for", "goto", "if", "inline", "register", "restrict", "return", "sizeof",
    "static", "struct", "switch", "typedef", "union", "volatile", "while",
    "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic",
    "_Noreturn", "_Static_assert", "_Thread_local", 0};

const char* const kCTypes[] = {
    "char", "const", "double", "float", "int", "long", "short", "signed",
    "unsigned", "void", "size_t", "ptrdiff_t", "wchar_t", "FILE", 0};

// Added on top of the C lists, not instead of them.
const char* const kCppKeywords[] = {
    "alignas", "alignof", "catch", "class", "constexpr", "const_cast", "decltype",
    "delete", "dynamic_cast", "explicit", "export", "false", "friend", "mutable",
    "namespace", "new", "noexcept", "nullptr", "operator", "private", "protected",
    "public", "reinterpret_cast", "static_assert", "static_cast", "template",
    "this", "throw", "true", "try", "typeid", "typename", "using", "virtual", 0};

const char* const kCppTypes[] = {"auto", "bool", "std", "string", "vector", 0};

bool inList(const char* const* list, const std::string& word) {
    for (size_t i = 0; list[i]; ++i)
        if (word == list[i]) return true;
    return false;
}

bool endsWith(const std::string& s, const char* suffix) {
    size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    // Compared without case, so a .C and a .S are found as well as a .c and .s.
    for (size_t i = 0; i < n; ++i) {
        char a = s[s.size() - n + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

void markAsm(const std::string& line, std::vector<unsigned char>& kind) {
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;

    for (; i < line.size(); ++i) {
        char c = line[i];

        // Three spellings of a comment reach this pane: ';' from MASM, '#'
        // from the GNU assemblers, and '//' from both.
        if (c == ';' || c == '#' || (c == '/' && i + 1 < line.size() && line[i + 1] == '/')) {
            for (size_t j = i; j < line.size(); ++j) kind[j] = KindComment;
            return;
        }

        if (c == '"') {
            size_t j = i;
            kind[j++] = KindString;
            while (j < line.size()) {
                kind[j] = KindString;
                if (line[j] == '\\' && j + 1 < line.size()) { kind[++j] = KindString; }
                else if (line[j] == '"') break;
                ++j;
            }
            i = j;
            continue;
        }

        if (c == '.' && (i == 0 || !identChar(line[i - 1]))) {
            size_t j = i + 1;
            while (j < line.size() && identChar(line[j])) ++j;
            // A directive, unless a colon makes it a label - cc1 writes both,
            // and .L.str.0: is a label whose name happens to start with a dot.
            size_t k = j;
            while (k < line.size() && (line[k] == '.' || identChar(line[k]))) ++k;
            unsigned char what = (k < line.size() && line[k] == ':') ? KindLabel : KindPreproc;
            for (size_t m = i; m < (what == KindLabel ? k : j); ++m) kind[m] = what;
            i = (what == KindLabel ? k : j) - 1;
            continue;
        }

        if (digit(c)) {
            size_t j = i;
            while (j < line.size() && (identChar(line[j]) || line[j] == '.')) ++j;
            for (size_t m = i; m < j; ++m) kind[m] = KindNumber;
            i = j - 1;
            continue;
        }

        if (identChar(c)) {
            size_t j = i;
            while (j < line.size() && identChar(line[j])) ++j;
            if (j < line.size() && line[j] == ':') {
                for (size_t m = i; m <= j; ++m) kind[m] = KindLabel;
                i = j;
            } else if (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t') {
                // The first word on the line is the mnemonic.
                bool first = true;
                for (size_t m = 0; m < i; ++m)
                    if (line[m] != ' ' && line[m] != '\t') first = false;
                if (first) for (size_t m = i; m < j; ++m) kind[m] = KindKeyword;
                i = j - 1;
            } else {
                i = j - 1;
            }
            continue;
        }
    }
}

}  // namespace

Language languageFor(const std::string& path) {
    if (endsWith(path, ".c")) return LangC;
    if (endsWith(path, ".h")) return LangC;
    if (endsWith(path, ".cpp") || endsWith(path, ".cc") || endsWith(path, ".cxx") ||
        endsWith(path, ".hpp") || endsWith(path, ".hh") || endsWith(path, ".hxx") ||
        endsWith(path, ".ipp"))
        return LangCpp;
    if (endsWith(path, ".s") || endsWith(path, ".asm")) return LangAsm;
    return LangPlain;
}

const char* languageName(Language lang) {
    switch (lang) {
        case LangC:   return "C";
        case LangCpp: return "C++";
        case LangAsm: return "asm";
        default:      return "text";
    }
}

const char* colourFor(unsigned char kind) {
    switch (kind) {
        case KindKeyword: return "94";   // bright blue
        case KindType:    return "36";   // cyan
        case KindString:  return "32";   // green
        case KindChar:    return "32";
        case KindComment: return "90";   // grey
        case KindPreproc: return "35";   // magenta
        case KindNumber:  return "33";   // yellow
        case KindLabel:   return "93";   // bright yellow
        default:          return "39";   // whatever the terminal calls normal
    }
}

void advanceState(const std::string& line, Language lang, SyntaxState& state) {
    if (lang == LangPlain || lang == LangAsm) {
        state.comment = false;
        return;
    }
    for (size_t i = 0; i < line.size(); ++i) {
        if (state.comment) {
            if (line[i] == '*' && i + 1 < line.size() && line[i + 1] == '/') {
                state.comment = false;
                ++i;
            }
            continue;
        }
        char c = line[i];
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') return;
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
            state.comment = true;
            ++i;
            continue;
        }
        if (c == '"' || c == '\'') {
            char closer = c;
            ++i;
            while (i < line.size()) {
                if (line[i] == '\\' && i + 1 < line.size()) ++i;
                else if (line[i] == closer) break;
                ++i;
            }
        }
    }
}

std::vector<unsigned char> highlight(const std::string& line, Language lang,
                                     SyntaxState& state) {
    std::vector<unsigned char> kind(line.size(), KindNormal);
    if (lang == LangPlain || line.empty()) {
        state.string = false;
        return kind;
    }
    if (lang == LangAsm) {
        markAsm(line, kind);
        return kind;
    }

    size_t i = 0;

    // A line that began inside a block comment stays in one until it closes.
    if (state.comment) {
        while (i < line.size()) {
            kind[i] = KindComment;
            if (line[i] == '*' && i + 1 < line.size() && line[i + 1] == '/') {
                kind[++i] = KindComment;
                ++i;
                state.comment = false;
                break;
            }
            ++i;
        }
        if (state.comment) return kind;
    }

    // The preprocessor, marked before anything else so that the < > of an
    // include is a string rather than two comparisons.
    size_t head = i;
    while (head < line.size() && (line[head] == ' ' || line[head] == '\t')) ++head;
    bool include = false;
    if (head < line.size() && line[head] == '#') {
        size_t j = head + 1;
        while (j < line.size() && (line[j] == ' ' || line[j] == '\t')) ++j;
        size_t wordStart = j;
        while (j < line.size() && identChar(line[j])) ++j;
        for (size_t m = head; m < j; ++m) kind[m] = KindPreproc;
        include = line.compare(wordStart, j - wordStart, "include") == 0;
        i = j;
    }

    for (; i < line.size(); ++i) {
        char c = line[i];

        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
            for (size_t j = i; j < line.size(); ++j) kind[j] = KindComment;
            return kind;
        }

        if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
            kind[i] = kind[i + 1] = KindComment;
            i += 2;
            state.comment = true;
            while (i < line.size()) {
                kind[i] = KindComment;
                if (line[i] == '*' && i + 1 < line.size() && line[i + 1] == '/') {
                    kind[++i] = KindComment;
                    state.comment = false;
                    break;
                }
                ++i;
            }
            if (state.comment) return kind;
            continue;
        }

        if (c == '"' || (include && c == '<')) {
            char closer = (c == '<') ? '>' : '"';
            size_t j = i;
            kind[j] = KindString;
            ++j;
            while (j < line.size()) {
                kind[j] = KindString;
                if (closer == '"' && line[j] == '\\' && j + 1 < line.size()) {
                    kind[++j] = KindString;   // an escaped quote ends nothing
                } else if (line[j] == closer) {
                    break;
                }
                ++j;
            }
            i = (j < line.size()) ? j : line.size() - 1;
            continue;
        }

        if (c == '\'') {
            size_t j = i;
            kind[j] = KindChar;
            ++j;
            while (j < line.size()) {
                kind[j] = KindChar;
                if (line[j] == '\\' && j + 1 < line.size()) kind[++j] = KindChar;
                else if (line[j] == '\'') break;
                ++j;
            }
            i = (j < line.size()) ? j : line.size() - 1;
            continue;
        }

        if (digit(c) && (i == 0 || !identChar(line[i - 1]))) {
            size_t j = i;
            while (j < line.size() && (identChar(line[j]) || line[j] == '.')) ++j;
            for (size_t m = i; m < j; ++m) kind[m] = KindNumber;
            i = j - 1;
            continue;
        }

        if (identChar(c) && !digit(c)) {
            size_t j = i;
            while (j < line.size() && identChar(line[j])) ++j;
            std::string word = line.substr(i, j - i);

            unsigned char what = KindNormal;
            if (inList(kCKeywords, word)) what = KindKeyword;
            else if (inList(kCTypes, word)) what = KindType;
            else if (lang == LangCpp && inList(kCppKeywords, word)) what = KindKeyword;
            else if (lang == LangCpp && inList(kCppTypes, word)) what = KindType;

            if (what != KindNormal)
                for (size_t m = i; m < j; ++m) kind[m] = what;
            i = j - 1;
            continue;
        }
    }

    return kind;
}

}  // namespace editor
