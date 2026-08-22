#!/usr/bin/env python3
"""Writes an Xcode project for each of the three programs, and a workspace.

    python3 tools/make-xcodeproj.py            write them
    python3 tools/make-xcodeproj.py --check    say whether they are current

Three command line tools, built by clang++, from three separate repositories:

    ed1   this editor            RStudio/Editor.xcodeproj
    cc1   the C compiler         ../Compiler-C/cc1.xcodeproj
    shc   the Shalimar compiler  ../Compiler-S/shc.xcodeproj

and RStudio.xcworkspace, which opens all three at once so that a change to a
compiler and the change to the editor that goes with it are one build and one
issue list.

**Each project is generated from that repository's own Makefile.** A hand-kept
project drifts: someone adds a file to the Makefile, forgets the project, and
Xcode quietly builds yesterday's program - which is not an error, just a
smaller program, so nothing says so. That has now happened twice here. Once
when sources were added and this was not re-run, and once when this script
stopped reading a Makefile variable that had been added to it. --check is the
answer to both: it rebuilds every project in memory and compares.

The identifiers are derived from the file names and the product, so
regenerating an unchanged project produces a byte-identical file and the
comparison is exact.

A project is written into the repository it belongs to, so its paths are
relative to that repository and mean something inside it. The workspace lives
here and reaches the other two with ../ - which is the one thing in this that
assumes all three are checked out side by side.
"""

import hashlib
import os
import re
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIBLINGS = os.path.dirname(HERE)


def ident(product, *parts):
    """A stable 24-hex-digit identifier, seeded by the product it belongs to.

    Xcode wants these unique within a project. Seeding with the product as well
    as the name keeps three projects from sharing identifiers, which is not
    strictly a fault across separate files but makes two of them impossible to
    tell apart when reading a diff.
    """
    digest = hashlib.sha1((product + ":" + ":".join(parts)).encode()).hexdigest()
    return digest[:24].upper()


def from_makefile(root, variables):
    """The .cpp named by these Makefile variables, relative to the repository."""
    text = open(os.path.join(root, "Makefile")).read()
    found = []
    for variable in variables:
        match = re.search(r"^%s *:?= (.*?)(?=\n[A-Z#]|\n\n)" % variable, text, re.S | re.M)
        if not match:
            sys.exit("could not find %s in %s/Makefile" % (variable, root))
        # A slash in the middle: src/backend/X86_64.cpp and runtime/Shortest.cpp
        # are both one path. A character class without one matched nothing at
        # all rather than failing, which is how the Shalimar half went missing.
        found += re.findall(r"([A-Za-z0-9_]+(?:/[A-Za-z0-9_]+)*\.cpp)", match.group(1))
    return found


def by_glob(root, directories):
    """The .cpp in these directories - for a Makefile that says $(wildcard ...).

    Compiler-C's does, so there is no list to read and the directories are the
    list. Sorted, so that a project regenerated on two machines is the same
    file on both.
    """
    found = []
    for directory in directories:
        where = os.path.join(root, directory)
        if not os.path.isdir(where):
            sys.exit("no %s in %s" % (directory, root))
        for f in sorted(os.listdir(where)):
            if f.endswith(".cpp"):
                found.append(directory + "/" + f)
    return found


def headers_under(root, directories):
    found = []
    for directory in directories:
        top = os.path.join(root, directory)
        for where, _, files in os.walk(top):
            for f in files:
                if f.endswith(".h"):
                    found.append(os.path.relpath(os.path.join(where, f), root))
    return sorted(found)


def rstudio_sources():
    """SRC and SHM_SRC, minus the Windows terminal, which clang here cannot build."""
    names = from_makefile(HERE, ("SRC", "SHM_SRC"))
    names = [n for n in names if not n.endswith("terminal_win.cpp")]
    # $(TERM_SRC) is chosen by the Makefile at build time; on a Mac it is this.
    if "src/terminal.cpp" not in names:
        names.append("src/terminal.cpp")
    return sorted(set(names))


# The three, and where each one's sources come from. Everything specific to a
# project is here; nothing below this knows which one it is writing.
def projects():
    return [
        {
            "product": "ed1",
            "root": HERE,
            "out": os.path.join(HERE, "Editor.xcodeproj"),
            "sources": rstudio_sources(),
            "headers": headers_under(HERE, ("src",)),
            "include": "$(SRCROOT)/src",
            # The editor drives these two, so building it builds them first.
            # Not a link dependency - all three are separate programs and
            # nothing of cc1 or shc ends up inside ed1 - but a real ordering:
            # a change to a compiler and the change to the editor that goes
            # with it are one build and one issue list, which is the whole
            # reason for a workspace rather than three windows.
            "depends": [("cc1", "../Compiler-C/cc1.xcodeproj"),
                        ("shc", "../Compiler-S/shc.xcodeproj")],
        },
        {
            "product": "cc1",
            "root": os.path.join(SIBLINGS, "Compiler-C"),
            "out": os.path.join(SIBLINGS, "Compiler-C", "cc1.xcodeproj"),
            # Its Makefile says $(wildcard src/*.cpp) $(wildcard src/backend/*.cpp),
            # so the directories are the list.
            "sources": by_glob(os.path.join(SIBLINGS, "Compiler-C"),
                               ("src", "src/backend")),
            "headers": headers_under(os.path.join(SIBLINGS, "Compiler-C"), ("src",)),
            "include": "$(SRCROOT)/src $(SRCROOT)/lib",
        },
        {
            "product": "shc",
            "root": os.path.join(SIBLINGS, "Compiler-S"),
            "out": os.path.join(SIBLINGS, "Compiler-S", "shc.xcodeproj"),
            # SOURCES names runtime/Shortest.cpp as well as src/, which is why
            # paths here are relative to the repository and not to src/.
            "sources": sorted(set(from_makefile(os.path.join(SIBLINGS, "Compiler-S"),
                                                ("SOURCES",)))),
            "headers": headers_under(os.path.join(SIBLINGS, "Compiler-S"),
                                     ("src", "runtime")),
            "include": "$(SRCROOT)/src $(SRCROOT)/runtime",
        },
    ]


# Xcode's own template turns on warnings none of these three Makefiles uses -
# -Wshorten-64-to-32 among them - and with warnings as errors that makes the
# project refuse a program `make` builds without complaint. cc1 failed exactly
# that way the first time this workspace was tried.
#
# The point of generating these from the Makefiles is that Xcode builds what
# make builds. A project that is *stricter* than the build it mirrors diverges
# just as surely as one that is laxer; it simply fails instead of passing. So
# the Xcode-only extras are turned off and the flag set is the Makefile's:
# -Wall -Wextra -pedantic, as errors.
COMMON = """				ALWAYS_SEARCH_USER_PATHS = NO;
				CLANG_CXX_LANGUAGE_STANDARD = "c++14";
				CLANG_ENABLE_OBJC_ARC = YES;
				CLANG_WARN_IMPLICIT_SIGN_CONVERSION = NO;
				CODE_SIGN_STYLE = Automatic;
				GCC_TREAT_WARNINGS_AS_ERRORS = YES;
				GCC_WARN_64_TO_32_BIT_CONVERSION = NO;
				MACOSX_DEPLOYMENT_TARGET = 10.15;
				PRODUCT_NAME = %s;
				SDKROOT = macosx;
				USER_HEADER_SEARCH_PATHS = "%s";
				WARNING_CFLAGS = (
					"-Wall",
					"-Wextra",
					"-pedantic",
				);"""


def project_text(spec):
    product = spec["product"]
    cpps = spec["sources"]
    hpps = spec["headers"]

    def i(*parts):
        return ident(product, *parts)

    PROJECT = i("project")
    TARGET = i("target")
    PRODUCT = i("product")
    MAIN_GROUP = i("group", "main")
    SRC_GROUP = i("group", "src")
    PRODUCTS_GROUP = i("group", "products")
    SOURCES_PHASE = i("phase", "sources")
    FRAMEWORKS = i("phase", "frameworks")
    PROJECT_CONFIGS = i("configlist", "project")
    TARGET_CONFIGS = i("configlist", "target")

    common = COMMON % (product, spec["include"])

    # Depending on a target in another project takes five objects per
    # dependency, and the remote identifiers have to be the ones that project
    # actually used - which is why ident() is seeded by product and derived
    # rather than invented. Getting one wrong gives Xcode a project it calls
    # damaged, with no clue which reference it could not follow.
    depends = spec.get("depends", [])
    dep_ids = []
    for other, where in depends:
        dep_ids.append({
            "product": other,
            "path": where,
            "file": i("projectref", other),          # the .xcodeproj on disk
            "group": i("projectproducts", other),    # its Products group, here
            "refproxy": i("referenceproxy", other),  # its product, seen from here
            "prodproxy": i("containerproxy", "product", other),
            "depproxy": i("containerproxy", "target", other),
            "dependency": i("targetdependency", other),
            # the two identifiers that belong to the *other* project
            "remote_target": ident(other, "target"),
            "remote_product": ident(other, "product"),
        })

    def config_id(which, name):
        return i("config", which, name)

    def build_configuration(which, name, extra):
        return ("\t\t%s /* %s */ = {\n\t\t\tisa = XCBuildConfiguration;\n"
                "\t\t\tbuildSettings = {\n%s\n%s\n\t\t\t};\n\t\t\tname = %s;\n\t\t};\n"
                % (config_id(which, name), name, common, extra, name))

    lines = []
    lines.append("// !$*UTF8*$!\n{\n\tarchiveVersion = 1;\n\tclasses = {\n\t};\n"
                 "\tobjectVersion = 56;\n\tobjects = {\n")

    lines.append("\n/* Begin PBXBuildFile section */\n")
    for name in cpps:
        lines.append("\t\t%s /* %s in Sources */ = {isa = PBXBuildFile; "
                     "fileRef = %s /* %s */; };\n"
                     % (i("build", name), name, i("file", name), name))
    lines.append("/* End PBXBuildFile section */\n")

    lines.append("\n/* Begin PBXFileReference section */\n")
    for name in cpps:
        lines.append("\t\t%s /* %s */ = {isa = PBXFileReference; "
                     "lastKnownFileType = sourcecode.cpp.cpp; path = %s; "
                     "sourceTree = \"<group>\"; };\n" % (i("file", name), name, name))
    for name in hpps:
        lines.append("\t\t%s /* %s */ = {isa = PBXFileReference; "
                     "lastKnownFileType = sourcecode.c.h; path = %s; "
                     "sourceTree = \"<group>\"; };\n" % (i("file", name), name, name))
    lines.append("\t\t%s /* %s */ = {isa = PBXFileReference; "
                 "explicitFileType = \"compiled.mach-o.executable\"; "
                 "includeInIndex = 0; path = %s; sourceTree = BUILT_PRODUCTS_DIR; };\n"
                 % (PRODUCT, product, product))
    for d in dep_ids:
        lines.append("\t\t%s /* %s.xcodeproj */ = {isa = PBXFileReference; "
                     "lastKnownFileType = \"wrapper.pb-project\"; name = %s.xcodeproj; "
                     "path = %s; sourceTree = \"<group>\"; };\n"
                     % (d["file"], d["product"], d["product"], d["path"]))
    lines.append("/* End PBXFileReference section */\n")

    if dep_ids:
        lines.append("\n/* Begin PBXContainerItemProxy section */\n")
        for d in dep_ids:
            # proxyType 2 is the other project's product; 1 is its target.
            lines.append("\t\t%s /* PBXContainerItemProxy */ = {\n"
                         "\t\t\tisa = PBXContainerItemProxy;\n"
                         "\t\t\tcontainerPortal = %s /* %s.xcodeproj */;\n"
                         "\t\t\tproxyType = 2;\n"
                         "\t\t\tremoteGlobalIDString = %s;\n"
                         "\t\t\tremoteInfo = %s;\n\t\t};\n"
                         % (d["prodproxy"], d["file"], d["product"],
                            d["remote_product"], d["product"]))
            lines.append("\t\t%s /* PBXContainerItemProxy */ = {\n"
                         "\t\t\tisa = PBXContainerItemProxy;\n"
                         "\t\t\tcontainerPortal = %s /* %s.xcodeproj */;\n"
                         "\t\t\tproxyType = 1;\n"
                         "\t\t\tremoteGlobalIDString = %s;\n"
                         "\t\t\tremoteInfo = %s;\n\t\t};\n"
                         % (d["depproxy"], d["file"], d["product"],
                            d["remote_target"], d["product"]))
        lines.append("/* End PBXContainerItemProxy section */\n")

    lines.append("\n/* Begin PBXFrameworksBuildPhase section */\n")
    lines.append("\t\t%s /* Frameworks */ = {\n\t\t\tisa = PBXFrameworksBuildPhase;\n"
                 "\t\t\tbuildActionMask = 2147483647;\n\t\t\tfiles = (\n\t\t\t);\n"
                 "\t\t\trunOnlyForDeploymentPostprocessing = 0;\n\t\t};\n" % FRAMEWORKS)
    lines.append("/* End PBXFrameworksBuildPhase section */\n")

    lines.append("\n/* Begin PBXGroup section */\n")
    referenced = "".join("\t\t\t\t%s /* %s.xcodeproj */,\n" % (d["file"], d["product"])
                         for d in dep_ids)
    lines.append("\t\t%s = {\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = (\n"
                 "\t\t\t\t%s /* Sources */,\n%s\t\t\t\t%s /* Products */,\n"
                 "\t\t\t);\n\t\t\tsourceTree = \"<group>\";\n\t\t};\n"
                 % (MAIN_GROUP, SRC_GROUP, referenced, PRODUCTS_GROUP))
    children = "".join("\t\t\t\t%s /* %s */,\n" % (i("file", n), n) for n in cpps + hpps)
    # The group has a name and no path: every file carries its own path from
    # the repository root, which is the only shape that takes src/, src/backend/
    # and runtime/ in one list.
    lines.append("\t\t%s /* Sources */ = {\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = (\n%s"
                 "\t\t\t);\n\t\t\tname = Sources;\n\t\t\tsourceTree = \"<group>\";\n\t\t};\n"
                 % (SRC_GROUP, children))
    lines.append("\t\t%s /* Products */ = {\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = (\n"
                 "\t\t\t\t%s /* %s */,\n\t\t\t);\n\t\t\tname = Products;\n"
                 "\t\t\tsourceTree = \"<group>\";\n\t\t};\n"
                 % (PRODUCTS_GROUP, PRODUCT, product))
    for d in dep_ids:
        lines.append("\t\t%s /* Products */ = {\n\t\t\tisa = PBXGroup;\n"
                     "\t\t\tchildren = (\n\t\t\t\t%s /* %s */,\n\t\t\t);\n"
                     "\t\t\tname = Products;\n\t\t\tsourceTree = \"<group>\";\n\t\t};\n"
                     % (d["group"], d["refproxy"], d["product"]))
    lines.append("/* End PBXGroup section */\n")

    if dep_ids:
        lines.append("\n/* Begin PBXReferenceProxy section */\n")
        for d in dep_ids:
            lines.append("\t\t%s /* %s */ = {\n\t\t\tisa = PBXReferenceProxy;\n"
                         "\t\t\tfileType = \"compiled.mach-o.executable\";\n"
                         "\t\t\tpath = %s;\n\t\t\tremoteRef = %s /* PBXContainerItemProxy */;\n"
                         "\t\t\tsourceTree = BUILT_PRODUCTS_DIR;\n\t\t};\n"
                         % (d["refproxy"], d["product"], d["product"], d["prodproxy"]))
        lines.append("/* End PBXReferenceProxy section */\n")

    lines.append("\n/* Begin PBXNativeTarget section */\n")
    lines.append("\t\t%s /* %s */ = {\n\t\t\tisa = PBXNativeTarget;\n"
                 "\t\t\tbuildConfigurationList = %s;\n\t\t\tbuildPhases = (\n"
                 "\t\t\t\t%s /* Sources */,\n\t\t\t\t%s /* Frameworks */,\n\t\t\t);\n"
                 "\t\t\tbuildRules = (\n\t\t\t);\n\t\t\tdependencies = (\n%s\t\t\t);\n"
                 "\t\t\tname = %s;\n\t\t\tproductName = %s;\n"
                 "\t\t\tproductReference = %s /* %s */;\n"
                 "\t\t\tproductType = \"com.apple.product-type.tool\";\n\t\t};\n"
                 % (TARGET, product, TARGET_CONFIGS, SOURCES_PHASE, FRAMEWORKS,
                    "".join("\t\t\t\t%s /* PBXTargetDependency */,\n" % d["dependency"]
                            for d in dep_ids),
                    product, product, PRODUCT, product))
    lines.append("/* End PBXNativeTarget section */\n")

    if dep_ids:
        lines.append("\n/* Begin PBXTargetDependency section */\n")
        for d in dep_ids:
            lines.append("\t\t%s /* PBXTargetDependency */ = {\n"
                         "\t\t\tisa = PBXTargetDependency;\n\t\t\tname = %s;\n"
                         "\t\t\ttargetProxy = %s /* PBXContainerItemProxy */;\n\t\t};\n"
                         % (d["dependency"], d["product"], d["depproxy"]))
        lines.append("/* End PBXTargetDependency section */\n")

    lines.append("\n/* Begin PBXProject section */\n")
    lines.append("\t\t%s /* Project object */ = {\n\t\t\tisa = PBXProject;\n"
                 "\t\t\tattributes = {\n\t\t\t\tBuildIndependentTargetsInParallel = 1;\n"
                 "\t\t\t\tLastUpgradeCheck = 1600;\n\t\t\t};\n"
                 "\t\t\tbuildConfigurationList = %s;\n"
                 "\t\t\tdevelopmentRegion = en;\n\t\t\thasScannedForEncodings = 0;\n"
                 "\t\t\tknownRegions = (\n\t\t\t\ten,\n\t\t\t\tBase,\n\t\t\t);\n"
                 "\t\t\tmainGroup = %s;\n\t\t\tproductRefGroup = %s /* Products */;\n"
                 "\t\t\tprojectDirPath = \"\";\n%s\t\t\tprojectRoot = \"\";\n"
                 "\t\t\ttargets = (\n\t\t\t\t%s /* %s */,\n\t\t\t);\n\t\t};\n"
                 % (PROJECT, PROJECT_CONFIGS, MAIN_GROUP, PRODUCTS_GROUP,
                    ("\t\t\tprojectReferences = (\n" +
                     "".join("\t\t\t\t{\n\t\t\t\t\tProductGroup = %s /* Products */;\n"
                             "\t\t\t\t\tProjectRef = %s /* %s.xcodeproj */;\n\t\t\t\t},\n"
                             % (d["group"], d["file"], d["product"]) for d in dep_ids) +
                     "\t\t\t);\n") if dep_ids else "",
                    TARGET, product))
    lines.append("/* End PBXProject section */\n")

    lines.append("\n/* Begin PBXSourcesBuildPhase section */\n")
    compiled = "".join("\t\t\t\t%s /* %s in Sources */,\n" % (i("build", n), n)
                       for n in cpps)
    lines.append("\t\t%s /* Sources */ = {\n\t\t\tisa = PBXSourcesBuildPhase;\n"
                 "\t\t\tbuildActionMask = 2147483647;\n\t\t\tfiles = (\n%s\t\t\t);\n"
                 "\t\t\trunOnlyForDeploymentPostprocessing = 0;\n\t\t};\n"
                 % (SOURCES_PHASE, compiled))
    lines.append("/* End PBXSourcesBuildPhase section */\n")

    lines.append("\n/* Begin XCBuildConfiguration section */\n")
    debug = "\t\t\t\tDEBUG_INFORMATION_FORMAT = dwarf;\n\t\t\t\tGCC_OPTIMIZATION_LEVEL = 0;"
    release = ("\t\t\t\tDEBUG_INFORMATION_FORMAT = \"dwarf-with-dsym\";\n"
               "\t\t\t\tGCC_OPTIMIZATION_LEVEL = 2;")
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
                     % (listing, which, config_id(which, "Debug"),
                        config_id(which, "Release")))
    lines.append("/* End XCConfigurationList section */\n")

    lines.append("\t};\n\trootObject = %s /* Project object */;\n}\n" % PROJECT)
    return "".join(lines)


def workspace_text(specs):
    """RStudio.xcworkspace - the three projects, opened together.

    The editor first, because that is what somebody is usually here for, and
    the two compilers under it in the order the editor reaches for them.
    """
    rows = []
    for spec in specs:
        where = os.path.relpath(spec["out"], HERE)
        rows.append('   <FileRef\n      location = "group:%s">\n   </FileRef>\n' % where)
    return ('<?xml version="1.0" encoding="UTF-8"?>\n<Workspace\n   version = "1.0">\n'
            + "".join(rows) + '</Workspace>\n')


def main():
    checking = "--check" in sys.argv[1:]
    specs = projects()
    stale = []

    wanted = [(os.path.join(s["out"], "project.pbxproj"), project_text(s), s["product"])
              for s in specs]
    wanted.append((os.path.join(HERE, "RStudio.xcworkspace", "contents.xcworkspacedata"),
                   workspace_text(specs), "workspace"))

    for path, text, what in wanted:
        if checking:
            there = open(path).read() if os.path.exists(path) else None
            if there != text:
                stale.append(what)
            continue
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            f.write(text)

    if checking:
        if stale:
            print("out of date with the Makefiles: " + ", ".join(stale))
            print("Xcode would build something other than what make builds.")
            print("  python3 tools/make-xcodeproj.py")
            return 1
        print("all three projects and the workspace are what the Makefiles say")
        return 0

    for spec in specs:
        print("%-4s %d sources, %d headers  ->  %s"
              % (spec["product"], len(spec["sources"]), len(spec["headers"]),
                 os.path.relpath(spec["out"], SIBLINGS)))
    print("RStudio.xcworkspace opens all three")
    return 0


if __name__ == "__main__":
    sys.exit(main())
