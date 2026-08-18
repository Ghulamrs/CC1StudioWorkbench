#include "toolchain.h"

#include <cstdlib>

namespace editor {

namespace {

#ifdef _WIN32
const char kSep = '\\';
#else
const char kSep = '/';
#endif

std::string tempDir() {
#ifdef _WIN32
    const char* t = std::getenv("TEMP");
    if (!t) t = std::getenv("TMP");
    return t ? t : ".";
#else
    const char* t = std::getenv("TMPDIR");
    return t ? t : "/tmp";
#endif
}

std::string quote(const std::string& s) { return "\"" + s + "\""; }

}  // namespace

const char* toolchainName(ToolchainKind kind) {
    switch (kind) {
        case ToolMsvc: return "cl";
        default:       return "cc1";
    }
}

const char* defaultProgram(ToolchainKind kind) {
    switch (kind) {
        case ToolMsvc: return "cl";
        default:       return "cc1";
    }
}

bool usesArch(ToolchainKind kind) { return kind == ToolCc1; }

Recipe assemblyRecipe(const Toolchain& tool, const std::string& source,
                      const std::string& arch) {
    Recipe recipe;
    std::string stem = tempDir() + kSep + "ed1-build";

    if (tool.kind == ToolMsvc) {
        recipe.assemblyPath = stem + ".asm";
        std::string obj = stem + ".obj";
        // /diagnostics:column is what turns 'bad.c(3)' into 'bad.c(3,13)'. The
        // editor wants the column, and cl does not give one without being
        // asked. /FAs writes the listing with the source interleaved.
        recipe.command = quote(tool.program) + " /nologo /c /diagnostics:column /FAs" +
                         " /Fa" + quote(recipe.assemblyPath) +
                         " /Fo" + quote(obj) + " " + quote(source);
        recipe.leftovers.push_back(obj);
        return recipe;
    }

    recipe.assemblyPath = stem + ".s";
    recipe.command = quote(tool.program) + " -S " + quote(source) + " -o " +
                     quote(recipe.assemblyPath) + " -arch " + arch;
    return recipe;
}

std::string shownCommand(const Toolchain& tool, const std::string& source,
                         const std::string& arch) {
    if (tool.kind == ToolMsvc)
        return tool.program + " /c /diagnostics:column /FAs " + source;
    return tool.program + " -S " + source + " -arch " + arch;
}

}  // namespace editor
