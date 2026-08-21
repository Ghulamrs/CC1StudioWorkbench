#include "settings.h"

#include <cctype>
#include <cstdio>

#include "json.h"
#include "path.h"

namespace editor {
namespace settings {

std::string fileName() {
    std::string home = path::homeDir();
    if (home.empty()) return std::string();
    return path::join(home, ".ed1config.json");
}

namespace {

// The file as it stands, or an empty object. Everything written here is
// read-modify-write: the file holds more than one thing now, and a writer that
// builds a fresh object throws away whatever it did not know about.
// A pointer made once and never destroyed, not a string. A function-local
// static with a destructor registers an atexit handler, and in the mixed-mode
// binary that corrupts the heap - the hazard debugger.cpp's dbg_program was
// written around, and this file is linked into the same binary.
std::string* moved = 0;
bool movedTo() { return moved != 0; }
void rememberMoved(const std::string& where) { moved = new std::string(where); }

bool writeAll(const Json& root);

Json readAll() {
    std::string where = fileName();
    if (where.empty()) return Json::object();

    FILE* in = std::fopen(where.c_str(), "rb");
    if (!in) return Json::object();
    std::string text;
    char chunk[1024];
    size_t got;
    while ((got = std::fread(chunk, 1, sizeof chunk, in)) > 0) text.append(chunk, got);
    std::fclose(in);

    std::string why;
    Json root = Json::parse(text, why);
    if (why.empty() && root.is(Json::Object)) return root;

    // Unreadable. Three things, in this order: keep the old one, put a good
    // one in its place, and let the front end say so.
    //
    // Only when there was something in it, though: an empty file is nobody's
    // work, and renaming that would leave litter for no reason.
    bool anything = false;
    for (size_t i = 0; i < text.size(); ++i)
        if (!std::isspace(static_cast<unsigned char>(text[i]))) { anything = true; break; }

    if (anything && !movedTo()) {
        std::string aside = where + ".error";
        path::remove(aside);          // the newest bad one is the interesting one
        if (path::rename(where, aside)) {
            rememberMoved(aside);
            // A fresh one now rather than at the next setting changed, so that
            // what is on disk always matches what the editor believes, and so
            // that a file exists to be written to at all.
            writeAll(Json::object());
        }
    }
    return Json::object();
}

bool writeAll(const Json& root) {
    std::string where = fileName();
    if (where.empty()) return false;

    FILE* out = std::fopen(where.c_str(), "wb");
    if (!out) return false;
    std::string text = root.write();
    std::fwrite(text.data(), 1, text.size(), out);
    std::fclose(out);
    return true;
}

}  // namespace

bool plainFrame() { return readAll().get("plain").boolean(false); }

bool rememberPlainFrame(bool plain) {
    Json root = readAll();
    root.set("plain", Json::fromBool(plain));
    return writeAll(root);
}

std::string setAside() {
    readAll();   // the moving happens there, on the first read of a bad file
    return moved ? *moved : std::string();
}

std::string codeFont() { return readAll().get("font").text(std::string()); }

bool rememberCodeFont(const std::string& described) {
    Json root = readAll();
    root.set("font", Json::fromText(described));
    return writeAll(root);
}

std::string lastProject() {
    std::string where = fileName();
    if (where.empty()) return std::string();

    // stdio, not <fstream> - see the note in buffer.cpp.
    FILE* in = std::fopen(where.c_str(), "rb");
    if (!in) return std::string();

    std::string text;
    char chunk[1024];
    size_t got;
    while ((got = std::fread(chunk, 1, sizeof chunk, in)) > 0) text.append(chunk, got);
    std::fclose(in);

    std::string why;
    Json root = Json::parse(text, why);
    if (!why.empty() || !root.is(Json::Object)) return std::string();

    std::string project = root.get("project").text("");
    if (project.empty() || !path::isDirectory(project)) return std::string();
    return project;
}

bool rememberProject(const std::string& directory) {
    std::string where = fileName();
    if (where.empty() || directory.empty()) return false;

    // Read, change one thing, write: this file holds more than the project
    // now, and building a fresh object here would throw the rest away.
    Json root = readAll();
    root.set("project", Json::fromText(path::absolute(directory)));

    FILE* out = std::fopen(where.c_str(), "wb");
    if (!out) return false;

    std::string text = root.write();
    std::fwrite(text.data(), 1, text.size(), out);
    std::fclose(out);
    return true;
}

}  // namespace settings
}  // namespace editor
