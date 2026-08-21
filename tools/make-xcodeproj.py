#!/usr/bin/env python3
"""Writes Editor.xcodeproj - a macOS command line tool target, built by clang++.

The project file is generated rather than hand-kept because a hand-kept one
drifts: someone adds a file to the Makefile, forgets this, and Xcode quietly
builds yesterday's editor. The source list here is read out of the Makefile, so
there is one list and it is the Makefile's.

    python3 tools/make-xcodeproj.py

The identifiers are derived from the file names rather than made up fresh each
run, so regenerating an unchanged project produces an unchanged file.
"""

import hashlib
import os
import re
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def ident(*parts):
    """A stable 24-hex-digit Xcode identifier for a thing with this name."""
    digest = hashlib.sha1("ed1:".join(parts).encode()).hexdigest()
    return digest[:24].upper()


def sources_from_makefile():
    """The SRC list, minus the Windows terminal, which clang here cannot build."""
    text = open(os.path.join(HERE, "Makefile")).read()
    match = re.search(r"^SRC := (.*?)(?=\n[A-Z])", text, re.S | re.M)
    if not match:
        sys.exit("could not find SRC in the Makefile")

    names = re.findall(r"src/([A-Za-z0-9_]+\.cpp)", match.group(1))
    names = [n for n in names if n != "terminal_win.cpp"]

    # $(TERM_SRC) is chosen by the Makefile at build time; on a Mac it is this.
    if "terminal.cpp" not in names:
        names.append("terminal.cpp")
    return sorted(set(names))


def headers():
    return sorted(f for f in os.listdir(os.path.join(HERE, "src")) if f.endswith(".h"))


PROJECT = ident("project")
TARGET = ident("target")
PRODUCT = ident("product")
MAIN_GROUP = ident("group", "main")
SRC_GROUP = ident("group", "src")
PRODUCTS_GROUP = ident("group", "products")
SOURCES_PHASE = ident("phase", "sources")
PROJECT_CONFIGS = ident("configlist", "project")
TARGET_CONFIGS = ident("configlist", "target")

COMMON = """				ALWAYS_SEARCH_USER_PATHS = NO;
				CLANG_CXX_LANGUAGE_STANDARD = "c++14";
				CLANG_ENABLE_OBJC_ARC = YES;
				CODE_SIGN_STYLE = Automatic;
				GCC_TREAT_WARNINGS_AS_ERRORS = YES;
				MACOSX_DEPLOYMENT_TARGET = 10.15;
				PRODUCT_NAME = ed1;
				SDKROOT = macosx;
				USER_HEADER_SEARCH_PATHS = "$(SRCROOT)/src";
				WARNING_CFLAGS = (
					"-Wall",
					"-Wextra",
					"-pedantic",
				);"""


# Four of them: a Debug and a Release for the project, and the same again for
# the target. They cannot be shared between the two lists - Xcode reads a
# project with one configuration object in two lists as damaged.
def config_id(which, name):
    return ident("config", which, name)


def build_configuration(which, name, extra):
    return """		%s /* %s */ = {
			isa = XCBuildConfiguration;
			buildSettings = {
%s
%s
			};
			name = %s;
		};
""" % (config_id(which, name), name, COMMON, extra, name)


def main():
    cpps = sources_from_makefile()
    hpps = headers()

    lines = []
    lines.append("// !$*UTF8*$!\n{\n\tarchiveVersion = 1;\n\tclasses = {\n\t};\n"
                 "\tobjectVersion = 56;\n\tobjects = {\n")

    lines.append("\n/* Begin PBXBuildFile section */\n")
    for name in cpps:
        lines.append("\t\t%s /* %s in Sources */ = {isa = PBXBuildFile; "
                     "fileRef = %s /* %s */; };\n"
                     % (ident("build", name), name, ident("file", name), name))
    lines.append("/* End PBXBuildFile section */\n")

    lines.append("\n/* Begin PBXFileReference section */\n")
    for name in cpps:
        lines.append("\t\t%s /* %s */ = {isa = PBXFileReference; "
                     "lastKnownFileType = sourcecode.cpp.cpp; path = %s; "
                     "sourceTree = \"<group>\"; };\n"
                     % (ident("file", name), name, name))
    for name in hpps:
        lines.append("\t\t%s /* %s */ = {isa = PBXFileReference; "
                     "lastKnownFileType = sourcecode.c.h; path = %s; "
                     "sourceTree = \"<group>\"; };\n"
                     % (ident("file", name), name, name))
    lines.append("\t\t%s /* ed1 */ = {isa = PBXFileReference; "
                 "explicitFileType = \"compiled.mach-o.executable\"; "
                 "includeInIndex = 0; path = ed1; sourceTree = BUILT_PRODUCTS_DIR; };\n"
                 % PRODUCT)
    lines.append("/* End PBXFileReference section */\n")

    lines.append("\n/* Begin PBXFrameworksBuildPhase section */\n")
    lines.append("\t\t%s /* Frameworks */ = {\n\t\t\tisa = PBXFrameworksBuildPhase;\n"
                 "\t\t\tbuildActionMask = 2147483647;\n\t\t\tfiles = (\n\t\t\t);\n"
                 "\t\t\trunOnlyForDeploymentPostprocessing = 0;\n\t\t};\n"
                 % ident("phase", "frameworks"))
    lines.append("/* End PBXFrameworksBuildPhase section */\n")

    lines.append("\n/* Begin PBXGroup section */\n")
    lines.append("\t\t%s = {\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = (\n"
                 "\t\t\t\t%s /* src */,\n\t\t\t\t%s /* Products */,\n"
                 "\t\t\t);\n\t\t\tsourceTree = \"<group>\";\n\t\t};\n"
                 % (MAIN_GROUP, SRC_GROUP, PRODUCTS_GROUP))

    children = "".join("\t\t\t\t%s /* %s */,\n" % (ident("file", n), n)
                       for n in cpps + hpps)
    lines.append("\t\t%s /* src */ = {\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = (\n%s"
                 "\t\t\t);\n\t\t\tpath = src;\n\t\t\tsourceTree = \"<group>\";\n\t\t};\n"
                 % (SRC_GROUP, children))
    lines.append("\t\t%s /* Products */ = {\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = (\n"
                 "\t\t\t\t%s /* ed1 */,\n\t\t\t);\n\t\t\tname = Products;\n"
                 "\t\t\tsourceTree = \"<group>\";\n\t\t};\n" % (PRODUCTS_GROUP, PRODUCT))
    lines.append("/* End PBXGroup section */\n")

    lines.append("\n/* Begin PBXNativeTarget section */\n")
    lines.append("\t\t%s /* ed1 */ = {\n\t\t\tisa = PBXNativeTarget;\n"
                 "\t\t\tbuildConfigurationList = %s;\n\t\t\tbuildPhases = (\n"
                 "\t\t\t\t%s /* Sources */,\n\t\t\t\t%s /* Frameworks */,\n\t\t\t);\n"
                 "\t\t\tbuildRules = (\n\t\t\t);\n\t\t\tdependencies = (\n\t\t\t);\n"
                 "\t\t\tname = ed1;\n\t\t\tproductName = ed1;\n"
                 "\t\t\tproductReference = %s /* ed1 */;\n"
                 "\t\t\tproductType = \"com.apple.product-type.tool\";\n\t\t};\n"
                 % (TARGET, TARGET_CONFIGS, SOURCES_PHASE,
                    ident("phase", "frameworks"), PRODUCT))
    lines.append("/* End PBXNativeTarget section */\n")

    lines.append("\n/* Begin PBXProject section */\n")
    lines.append("\t\t%s /* Project object */ = {\n\t\t\tisa = PBXProject;\n"
                 "\t\t\tattributes = {\n\t\t\t\tBuildIndependentTargetsInParallel = 1;\n"
                 "\t\t\t\tLastUpgradeCheck = 1600;\n\t\t\t};\n"
                 "\t\t\tbuildConfigurationList = %s;\n"
                 "\t\t\tdevelopmentRegion = en;\n\t\t\thasScannedForEncodings = 0;\n"
                 "\t\t\tknownRegions = (\n\t\t\t\ten,\n\t\t\t\tBase,\n\t\t\t);\n"
                 "\t\t\tmainGroup = %s;\n\t\t\tproductRefGroup = %s /* Products */;\n"
                 "\t\t\tprojectDirPath = \"\";\n\t\t\tprojectRoot = \"\";\n"
                 "\t\t\ttargets = (\n\t\t\t\t%s /* ed1 */,\n\t\t\t);\n\t\t};\n"
                 % (PROJECT, PROJECT_CONFIGS, MAIN_GROUP, PRODUCTS_GROUP, TARGET))
    lines.append("/* End PBXProject section */\n")

    lines.append("\n/* Begin PBXSourcesBuildPhase section */\n")
    compiled = "".join("\t\t\t\t%s /* %s in Sources */,\n" % (ident("build", n), n)
                       for n in cpps)
    lines.append("\t\t%s /* Sources */ = {\n\t\t\tisa = PBXSourcesBuildPhase;\n"
                 "\t\t\tbuildActionMask = 2147483647;\n\t\t\tfiles = (\n%s\t\t\t);\n"
                 "\t\t\trunOnlyForDeploymentPostprocessing = 0;\n\t\t};\n"
                 % (SOURCES_PHASE, compiled))
    lines.append("/* End PBXSourcesBuildPhase section */\n")

    lines.append("\n/* Begin XCBuildConfiguration section */\n")
    debug = ("\t\t\t\tGCC_OPTIMIZATION_LEVEL = 0;\n"
             "\t\t\t\tONLY_ACTIVE_ARCH = YES;")
    release = "\t\t\t\tGCC_OPTIMIZATION_LEVEL = 2;"
    for which in ("project", "target"):
        lines.append(build_configuration(which, "Debug", debug))
        lines.append(build_configuration(which, "Release", release))
    lines.append("/* End XCBuildConfiguration section */\n")

    lines.append("\n/* Begin XCConfigurationList section */\n")
    for listing, which in ((PROJECT_CONFIGS, "project"), (TARGET_CONFIGS, "target")):
        lines.append("\t\t%s /* %s */ = {\n\t\t\tisa = XCConfigurationList;\n"
                     "\t\t\tbuildConfigurations = (\n"
                     "\t\t\t\t%s /* Debug */,\n\t\t\t\t%s /* Release */,\n\t\t\t);\n"
                     "\t\t\tdefaultConfigurationIsVisible = 0;\n"
                     "\t\t\tdefaultConfigurationName = Debug;\n\t\t};\n"
                     % (listing, which,
                        config_id(which, "Debug"), config_id(which, "Release")))
    lines.append("/* End XCConfigurationList section */\n")

    lines.append("\t};\n\trootObject = %s /* Project object */;\n}\n" % PROJECT)

    out = os.path.join(HERE, "Editor.xcodeproj")
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "project.pbxproj"), "w") as f:
        f.write("".join(lines))

    print("Editor.xcodeproj written - %d sources, %d headers" % (len(cpps), len(hpps)))


if __name__ == "__main__":
    main()
