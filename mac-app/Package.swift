// swift-tools-version:5.9
//
// SwiftUI prototype editor for BPP control files. Subprocess-calls the
// existing bpp-lint binary, renders diagnostics live as you type, and
// applies a small syntax-highlighting scheme (comments, known/unknown
// keywords, '=' punctuation).
//
// Build & run from this directory:
//     swift run BppLintEditor
//
// Or double-click Package.swift to open the project in Xcode.
//
// Requires:
//   * Swift toolchain (comes with Xcode).
//   * bpp-lint on $PATH, or BPP_LINT_BINARY env var pointing at it.
//
import PackageDescription

let package = Package(
    name: "BppLintEditor",
    platforms: [.macOS(.v13)],
    targets: [
        .executableTarget(
            name: "BppLintEditor",
            path: "Sources/BppLintEditor"
        )
    ]
)
