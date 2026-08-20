#include "settings.h"

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
    if (!why.empty() || !root.is(Json::Object)) return Json::object();
    return root;
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
