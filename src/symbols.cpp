#include "symbols.h"

#include <cstdlib>

namespace editor {

namespace {

Demangler howToRead = 0;

bool space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

std::string trimmed(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && space(s[a])) ++a;
    size_t b = s.size();
    while (b > a && space(s[b - 1])) --b;
    return s.substr(a, b - a);
}

// The word at `at`, and where it ended.
std::string word(const std::string& s, size_t at, size_t& after) {
    while (at < s.size() && space(s[at])) ++at;
    size_t start = at;
    while (at < s.size() && !space(s[at])) ++at;
    after = at;
    return s.substr(start, at - start);
}

bool isAll(const std::string& s, const char* what) { return s == what; }

// A label a person did not write: cc1's .L.str.0 and L.main.end.0, MASM's
// $SG5346 and $_L_str_0. They are the compiler's own bookkeeping.
bool madeUp(const std::string& name) {
    if (name.empty()) return true;
    if (name[0] == '$') return true;
    if (name.compare(0, 3, ".L.") == 0) return true;
    if (name.compare(0, 2, "L.") == 0) return true;
    return false;
}

// "as expected\000" and 'first is %d', 0aH, 00H and 97, 115, 32 are all the
// same thing said three ways.
std::string readText(const std::string& rest) {
    std::string out;
    size_t at = 0;

    while (at < rest.size()) {
        if (space(rest[at]) || rest[at] == ',') {
            ++at;
            continue;
        }

        if (rest[at] == '"' || rest[at] == '\'') {
            char closer = rest[at++];
            while (at < rest.size() && rest[at] != closer) {
                if (rest[at] == '\\' && at + 1 < rest.size()) {
                    // \000 and friends end the string as far as reading it goes.
                    char escape = rest[at + 1];
                    if (escape == '0') return out;
                    if (escape == 'n') out += ' ';
                    at += 2;
                    continue;
                }
                out += rest[at++];
            }
            ++at;
            continue;
        }

        // A number, in MASM's 0aH or in plain decimal.
        size_t start = at;
        while (at < rest.size() && rest[at] != ',' && !space(rest[at])) ++at;
        std::string number = rest.substr(start, at - start);
        if (number.empty()) continue;

        long value = 0;
        if (number.size() > 1 && (number[number.size() - 1] == 'H' ||
                                  number[number.size() - 1] == 'h'))
            value = std::strtol(number.substr(0, number.size() - 1).c_str(), 0, 16);
        else
            value = std::strtol(number.c_str(), 0, 10);

        if (value == 0) return out;               // the terminator ends it
        if (value == 10 || value == 13) out += ' ';
        else if (value >= 32 && value < 127) out += static_cast<char>(value);
    }
    return out;
}

}  // namespace

std::vector<Symbol> symbolsIn(const std::vector<std::string>& assembly) {
    std::vector<Symbol> found;

    // Which function the reader is inside, so that the stack it takes can be
    // hung on the right name.
    size_t inside = found.size();
    bool prologue = false;

    // arm64 cannot subtract a large immediate from the stack pointer, so cc1
    // puts the number in a register first. The register and its value are kept
    // until the subtraction that uses them turns up.
    std::string held;
    long heldValue = 0;

    for (size_t row = 0; row < assembly.size(); ++row) {
        std::string line = trimmed(assembly[row]);
        if (line.empty() || line[0] == ';') continue;

        size_t after = 0;
        std::string first = word(line, 0, after);
        std::string second = word(line, after, after);

        Symbol made;
        made.line = row + 1;

        if (isAll(first, ".globl") || isAll(first, ".global") || isAll(first, "PUBLIC")) {
            made.kind = SymbolExported;
            made.name = second;
            found.push_back(made);
            continue;
        }

        if (isAll(first, "EXTERN") || isAll(first, "EXTRN") || isAll(first, ".extern")) {
            made.kind = SymbolExternal;
            // EXTERN puts:PROC - the name is what comes before the colon.
            size_t colon = second.find(':');
            made.name = colon == std::string::npos ? second : second.substr(0, colon);
            found.push_back(made);
            continue;
        }

        if (isAll(second, "PROC")) {           // MASM: name PROC [FRAME]
            made.kind = SymbolFunction;
            made.name = first;
            found.push_back(made);
            inside = found.size() - 1;
            prologue = true;
            continue;
        }

        if (isAll(second, "ENDP")) {
            inside = found.size();
            prologue = false;
            continue;
        }

        // A DB on its own continues the string named on the line before it -
        // MASM breaks long ones across lines.
        if (isAll(first, "DB") && !found.empty() && found.back().kind == SymbolText) {
            found.back().detail += readText(line.substr(after - second.size()));
            continue;
        }

        if (isAll(second, "DB") || isAll(second, ".asciz")) {   // MASM: name DB bytes
            std::string said = readText(line.substr(after));
            if (!said.empty()) {
                made.kind = SymbolText;
                made.name = first;
                made.detail = said;
                found.push_back(made);
            }
            continue;
        }

        if (isAll(first, ".ascii") || isAll(first, ".asciz") || isAll(first, ".string")) {
            std::string said = readText(line.substr(after - second.size()));
            if (!said.empty() && !found.empty() && found.back().kind == SymbolText &&
                found.back().detail.empty()) {
                found.back().detail = said;    // the label came on the line before
            } else if (!said.empty()) {
                made.kind = SymbolText;
                made.name = "(string)";
                made.detail = said;
                found.push_back(made);
            }
            continue;
        }

        // A label at the head of a line: a function in the GNU spelling, or a
        // name for a string about to be given.
        if (line[line.size() - 1] == ':') {
            std::string name = line.substr(0, line.size() - 1);
            if (name.find(' ') != std::string::npos) continue;

            if (madeUp(name)) {
                // A string's label, waiting for the .ascii on the next line.
                made.kind = SymbolText;
                made.name = name;
                found.push_back(made);
                continue;
            }
            made.kind = SymbolFunction;
            made.name = name;
            found.push_back(made);
            inside = found.size() - 1;
            prologue = true;
            continue;
        }

        if (!prologue || inside >= found.size()) continue;

        // 'mov x9, #16' on its own means nothing; it means something when the
        // next subtraction from the stack pointer uses x9.
        if (isAll(first, "mov")) {
            std::string into = second;
            if (!into.empty() && into[into.size() - 1] == ',') into.resize(into.size() - 1);
            size_t hash = line.find('#');
            if (hash != std::string::npos) {
                held = into;
                heldValue = std::strtol(line.c_str() + hash + 1, 0, 10);
            }
            continue;
        }

        // How much stack the function takes, from the first subtraction in its
        // prologue: 'sub rsp, 16', 'sub sp, sp, #16', or 'sub sp, sp, x9'.
        if (isAll(first, "sub") && found[inside].detail.empty()) {
            long bytes = 0;

            size_t hash = line.find('#');
            if (hash != std::string::npos) {
                bytes = std::strtol(line.c_str() + hash + 1, 0, 10);
            } else {
                size_t comma = line.find_last_of(',');
                if (comma != std::string::npos) {
                    std::string last = trimmed(line.substr(comma + 1));
                    bytes = std::strtol(last.c_str(), 0, 10);
                    if (bytes == 0 && !held.empty() && last == held) bytes = heldValue;
                }
            }

            if (bytes > 0) {
                found[inside].detail = "stack " + std::to_string(bytes) + " bytes";
                prologue = false;
            }
        }
    }

    // Labels that turned out to name nothing are the compiler's bookkeeping.
    std::vector<Symbol> kept;
    for (size_t i = 0; i < found.size(); ++i) {
        if (found[i].kind == SymbolText && found[i].detail.empty()) continue;
        if (howToRead && !found[i].name.empty() && found[i].name[0] == '?')
            found[i].name = howToRead(found[i].name);
        kept.push_back(found[i]);
    }
    return kept;
}

void setDemangler(Demangler how) { howToRead = how; }

std::vector<std::string> describe(const std::vector<Symbol>& symbols) {
    std::vector<std::string> out;

    const unsigned char order[4] = {SymbolFunction, SymbolExported, SymbolExternal,
                                    SymbolText};
    const char* headings[4] = {"functions", "exported", "called but not defined",
                               "strings in the binary"};

    for (int which = 0; which < 4; ++which) {
        std::vector<const Symbol*> mine;
        for (size_t i = 0; i < symbols.size(); ++i)
            if (symbols[i].kind == order[which]) mine.push_back(&symbols[i]);
        if (mine.empty()) continue;

        if (!out.empty()) out.push_back("");
        out.push_back(std::string(headings[which]) + "  (" + std::to_string(mine.size()) +
                      ")");

        for (size_t i = 0; i < mine.size(); ++i) {
            std::string name = mine[i]->name;
            if (name.size() > 58) name = name.substr(0, 55) + "...";

            std::string line = "  " + name;
            if (line.size() < 26) line.resize(26, ' ');
            else line += "  ";
            if (!mine[i]->detail.empty()) line += mine[i]->detail;
            out.push_back(line);
        }
    }

    if (out.empty()) out.push_back("nothing built yet - Ctrl-B, or F7 in the window");
    return out;
}

}  // namespace editor
