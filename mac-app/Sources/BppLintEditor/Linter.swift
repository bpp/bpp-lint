import Foundation
import SwiftUI

enum Severity: String {
    case error, warning, info

    var iconName: String {
        switch self {
        case .error:   return "xmark.octagon.fill"
        case .warning: return "exclamationmark.triangle.fill"
        case .info:    return "info.circle.fill"
        }
    }

    var color: Color {
        switch self {
        case .error:   return .red
        case .warning: return .yellow
        case .info:    return .blue
        }
    }

    // bpp-lint's JSON uses "note" for informational items; map to .info.
    init(json: String) {
        switch json {
        case "error":   self = .error
        case "warning": self = .warning
        default:        self = .info    // "note" / anything else
        }
    }
}

struct Diagnostic: Identifiable, Hashable {
    let id = UUID()
    let lineNumber: Int?
    let column: Int?
    let severity: Severity
    let code: String?
    let message: String
    var note: String?
    var fix: String?

    // For a BPP103 "using default" warning: the explicit assignment that would
    // silence it, as (keyword, value). nil for everything else. This now comes
    // straight from the linter's structured JSON `default` field -- no parsing
    // of the human message, and no annotation-stripping heuristics.
    var defaultAssignment: (keyword: String, value: String)?

    func hash(into hasher: inout Hasher) { hasher.combine(id) }
    static func == (a: Diagnostic, b: Diagnostic) -> Bool { a.id == b.id }
}

struct LintResult {
    var diagnostics: [Diagnostic]
    var failure: String?   // non-nil only if the linter itself couldn't run
}

// MARK: - JSON schema (subset of bpp-lint --json we consume)

private struct LintJSON: Decodable {
    let diagnostics: [Diag]

    struct Diag: Decodable {
        let code: String?
        let severity: String
        let line: Int
        let column: Int
        let message: String
        let suggestion: String?
        let fixable: Bool
        let suggestedFix: String?
        let defaultFill: DefaultFill?

        enum CodingKeys: String, CodingKey {
            case code, severity, line, column, message, suggestion, fixable
            case suggestedFix = "suggested_fix"
            case defaultFill  = "default"
        }
    }

    struct DefaultFill: Decodable {
        let keyword: String
        let value: String
    }
}

@MainActor
final class LinterRunner: ObservableObject {
    private var debounceTask: Task<Void, Never>?
    private let debounceMs: UInt64 = 250

    func scheduleLint(of text: String,
                      completion: @escaping (LintResult) -> Void) {
        debounceTask?.cancel()
        let snapshot = text
        debounceTask = Task { [debounceMs] in
            try? await Task.sleep(nanoseconds: debounceMs * 1_000_000)
            if Task.isCancelled { return }
            let result = await Self.runLint(on: snapshot)
            await MainActor.run { completion(result) }
        }
    }

    nonisolated static func runLint(on text: String) async -> LintResult {
        guard let binary = findBinary() else {
            return LintResult(diagnostics: [],
                              failure: "bpp-lint not found (set BPP_LINT_BINARY or add it to PATH)")
        }

        let tempDir = FileManager.default.temporaryDirectory
        let tempFile = tempDir.appendingPathComponent("bpp-lint-buffer-\(UUID().uuidString).ctl")
        do {
            try text.write(to: tempFile, atomically: true, encoding: .utf8)
        } catch {
            return LintResult(diagnostics: [], failure: "failed to write temp file: \(error.localizedDescription)")
        }
        defer { try? FileManager.default.removeItem(at: tempFile) }

        let proc = Process()
        proc.executableURL = URL(fileURLWithPath: binary)
        // Machine-readable report on stdout. Exit 0 = valid, 1 = has errors,
        // 2 = invocation failure (no JSON emitted).
        proc.arguments = ["--json", tempFile.path]
        let errPipe = Pipe()
        let outPipe = Pipe()
        proc.standardError = errPipe
        proc.standardOutput = outPipe

        do {
            try proc.run()
            proc.waitUntilExit()
        } catch {
            return LintResult(diagnostics: [], failure: "failed to launch bpp-lint: \(error.localizedDescription)")
        }

        let outData = (try? outPipe.fileHandleForReading.readToEnd()) ?? Data()

        if proc.terminationStatus == 2 {
            let err = (try? errPipe.fileHandleForReading.readToEnd())
                .flatMap { String(data: $0, encoding: .utf8) }
                ?? "(no stderr)"
            return LintResult(diagnostics: [],
                              failure: "bpp-lint exit 2: \(err.trimmingCharacters(in: .whitespacesAndNewlines))")
        }

        do {
            let report = try JSONDecoder().decode(LintJSON.self, from: outData)
            return LintResult(diagnostics: report.diagnostics.map(Self.convert), failure: nil)
        } catch {
            return LintResult(diagnostics: [],
                              failure: "could not parse bpp-lint JSON: \(error.localizedDescription)")
        }
    }

    private nonisolated static func convert(_ d: LintJSON.Diag) -> Diagnostic {
        // File-level diagnostics report line/column 0; surface them as nil.
        let line = d.line > 0 ? d.line : nil
        let col  = d.column > 0 ? d.column : nil
        let fill = d.defaultFill.map { (keyword: $0.keyword, value: $0.value) }
        return Diagnostic(lineNumber: line,
                          column: col,
                          severity: Severity(json: d.severity),
                          code: d.code,
                          message: d.message,
                          note: d.suggestion,
                          fix: d.suggestedFix,
                          defaultAssignment: fill)
    }

    // MARK: - Binary discovery

    private nonisolated static func findBinary() -> String? {
        let env = ProcessInfo.processInfo.environment
        if let custom = env["BPP_LINT_BINARY"], !custom.isEmpty,
           FileManager.default.isExecutableFile(atPath: custom) {
            return custom
        }
        // Search a small set of common locations first (faster than `which`).
        for candidate in [
            "/opt/homebrew/bin/bpp-lint",
            "/usr/local/bin/bpp-lint",
            "/usr/bin/bpp-lint",
        ] {
            if FileManager.default.isExecutableFile(atPath: candidate) {
                return candidate
            }
        }
        // Fall back to `which`, which respects PATH from the user's login shell.
        let proc = Process()
        proc.executableURL = URL(fileURLWithPath: "/usr/bin/env")
        proc.arguments = ["which", "bpp-lint"]
        let pipe = Pipe()
        proc.standardOutput = pipe
        proc.standardError = Pipe()
        do {
            try proc.run()
            proc.waitUntilExit()
            if proc.terminationStatus == 0 {
                let data = (try? pipe.fileHandleForReading.readToEnd()) ?? Data()
                let path = String(data: data, encoding: .utf8)?
                    .trimmingCharacters(in: .whitespacesAndNewlines)
                if let p = path, !p.isEmpty { return p }
            }
        } catch { }
        return nil
    }
}
