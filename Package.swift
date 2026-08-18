// swift-tools-version:5.9
//
// This exists so the editor can be opened, built, indexed and debugged in
// Xcode - "open Package.swift", or File > Open on this file. It is not how it
// is built for use: that is `make` here and `build.bat` on Windows, and both
// stay the authority. Nothing in src/ is arranged to suit this file.
//
// One thing to know before reaching for the Run button: ed1 is a terminal
// program, and Xcode's console is a pipe rather than a terminal. Started from
// there it cannot put the terminal into raw mode, so it will print escape
// sequences instead of drawing. Build and debug it in Xcode; run it in
// Terminal, and attach to it from Xcode if you want breakpoints while it runs.

import PackageDescription

let package = Package(
    name: "ed1",
    // std::filesystem, which the project pane and the file commands use,
    // arrived in the macOS libraries in 10.15. Without saying so here the
    // package defaults to older than that and every use of it is unavailable.
    platforms: [.macOS(.v10_15)],
    // Declared so that Xcode has something to make a scheme out of. Without a
    // product it resolves the package and then offers nothing to build.
    products: [
        .executable(name: "ed1", targets: ["ed1"])
    ],
    targets: [
        .executableTarget(
            name: "ed1",
            path: "src",
            // The Windows half of the terminal, which has no business being
            // compiled here, and the object directory make writes into.
            exclude: ["terminal_win.cpp", "obj"]
        )
    ],
    cxxLanguageStandard: .cxx17
)
