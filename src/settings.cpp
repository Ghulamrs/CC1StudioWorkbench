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

    Json root = Json::object();
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
