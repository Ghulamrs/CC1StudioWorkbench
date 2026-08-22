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
//
// C++14 and no further, which is what this arena compiles: cl is told
// /std:c++14, so colouring a word cl would refuse to accept would be the
// editor promising something the compiler will not do. `final` and `override`
// are in because they are read as keywords even though the standard calls them
// contextual, and the alternative tokens - and, or, not_eq and the rest - are
// in because they are keywords, however rarely anybody writes them.
const char* const kCppKeywords[] = {
    "alignas", "alignof", "and", "and_eq", "asm", "bitand", "bitor", "catch",
    "class", "compl", "constexpr", "const_cast", "decltype", "delete",
    "dynamic_cast", "explicit", "export", "false", "final", "friend", "mutable",
    "namespace", "new", "noexcept", "not", "not_eq", "nullptr", "operator", "or",
    "or_eq", "override", "private", "protected", "public", "reinterpret_cast",
    "static_assert", "static_cast", "template", "this", "thread_local", "throw",
    "true", "try", "typeid", "typename", "using", "virtual", "xor", "xor_eq", 0};

// The built-in types, and the names from the standard library that turn up in
// ordinary code often enough to be worth telling apart from a variable. A
// library name is not a keyword and this does not pretend otherwise - they are
// coloured as types, which is what they are.
const char* const kCppTypes[] = {
    "auto", "bool", "char16_t", "char32_t", "nullptr_t",
    "std", "string", "wstring", "vector", "array", "deque", "list", "forward_list",
    "map", "multimap", "set", "multiset", "unordered_map", "unordered_set",
    "pair", "tuple", "bitset", "initializer_list",
    "unique_ptr", "shared_ptr", "weak_ptr",
    "istream", "ostream", "iostream", "ifstream", "ofstream", "fstream",
    "stringstream", "istringstream", "ostringstream",
    "exception", "runtime_error", "logic_error", "bad_alloc",
    "function", "thread", "mutex", "atomic", "chrono", 0};

// Shalimar's whole vocabulary. Thirteen words, and three of them - int, real
// and char - are the names of the conversions as well, which is resolved by
// position rather than by spelling.
const char* const kShalimarKeywords[] = {
    "if", "elseif", "else", "while", "for", "to", "step", "fun", "return",
    "break", "continue", 0};

const char* const kShalimarTypes[] = {
    "int", "real", "char", 0};

// The twenty built-ins. Not keywords - a program cannot define a function of
// the same name, which is close enough to being reserved that colouring them
// tells the truth.
const char* const kShalimarBuiltins[] = {
    "abs", "sqrt", "log", "exp", "hypot", "sin", "cos", "tan", "asin", "acos",
    "atan", "atan2", "pow", "round", "ceil", "floor", "trunc", "max", "min",
    "len", 0};

// Read-only and reserved: neither can be declared, assigned, taken as a
// parameter name, or used as a loop counter. One name, one meaning - so
// colouring them as the numbers they are is not a guess.
const char* const kShalimarConstants[] = { "pi", "e", 0 };

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

// Shalimar, which is simpler than C in exactly the places a highlighter cares
// about: one kind of comment, one kind of literal, no escapes inside it, no
// preprocessor, and no character literal at all - an apostrophe is not a token
// there, so it colours nothing.
void markShalimar(const std::string& line, std::vector<unsigned char>& kind) {
    size_t i = 0;

    // '?' and '??' are commands and must be the first token on their line, so
    // they are looked for only there. Nothing else in the language carries
    // layout, and this is the one place it does.
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i < line.size() && line[i] == '?') {
        kind[i] = KindPreproc;
        if (i + 1 < line.size() && line[i + 1] == '?') kind[++i] = KindPreproc;
        ++i;
    }

    for (; i < line.size(); ++i) {
        char c = line[i];

        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
            for (size_t j = i; j < line.size(); ++j) kind[j] = KindComment;
            return;
        }

        // No escapes: the first closing quote always ends it, and a literal
        // that reaches the end of the line was never closed.
        if (c == '"') {
            size_t j = i;
            kind[j] = KindString;
            for (++j; j < line.size(); ++j) {
                kind[j] = KindString;
                if (line[j] == '"') break;
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
            const bool afterDot = i > 0 && line[i - 1] == '.';
            size_t j = i;
            while (j < line.size() && identChar(line[j])) ++j;
            std::string word = line.substr(i, j - i);

            unsigned char what = KindNormal;
            if (afterDot) {
                // row, col and dim mean something only here. Everywhere else
                // they are ordinary names, and 'row : 9' declares a variable.
                if (word == "row" || word == "col" || word == "dim") what = KindType;
            } else if (inList(kShalimarKeywords, word)) {
                what = KindKeyword;
            } else if (inList(kShalimarTypes, word)) {
                what = KindType;
            } else if (inList(kShalimarConstants, word)) {
                what = KindNumber;
            } else if (inList(kShalimarBuiltins, word)) {
                what = KindType;
            } else if (word == "prec") {
                // Also contextual: a directive at the head of a print item and
                // an ordinary name anywhere else, told apart by the bracket.
                size_t k = j;
                while (k < line.size() && line[k] == ' ') ++k;
                if (k < line.size() && line[k] == '(') what = KindPreproc;
            }

            if (what != KindNormal)
                for (size_t m = i; m < j; ++m) kind[m] = what;
            i = j - 1;
            continue;
        }
    }
}

}  // namespace

Language languageFor(const std::string& path) {
    if (endsWith(path, ".shl") || endsWith(path, ".shm")) return LangShalimar;
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
        case LangC:        return "C";
        case LangCpp:      return "C++";
        case LangShalimar: return "Shalimar";
        case LangAsm:      return "asm";
        default:           return "text";
    }
}

const char* languageSuffix(Language lang) {
    switch (lang) {
        case LangC:        return "c";
        case LangCpp:      return "cpp";
        case LangShalimar: return "shl";
        case LangAsm:      return "s";
        default:           return "txt";
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
    // Shalimar has no block comment and no literal that outlives its line, so
    // nothing it writes can leave anything behind. That is the same reason
    // plain text and assembly need no walk.
    if (lang == LangPlain || lang == LangAsm || lang == LangShalimar) {
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
    if (lang == LangShalimar) {
        markShalimar(line, kind);
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
