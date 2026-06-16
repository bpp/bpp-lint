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

    func hash(into hasher: inout Hasher) { hasher.combine(id) }
    static func == (a: Diagnostic, b: Diagnostic) -> Bool { a.id == b.id }
}

struct LintResult {
    var diagnostics: [Diagnostic]
    var failure: String?   // non-nil only if the linter itself couldn't run
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
        proc.arguments = ["--color=never", "--codes", tempFile.path]
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

        // bpp-lint exit 2 = invocation failure; 0/1 = normal lint outcomes
        if proc.terminationStatus == 2 {
            let err = (try? errPipe.fileHandleForReading.readToEnd())
                .flatMap { String(data: $0, encoding: .utf8) }
                ?? "(no stderr)"
            return LintResult(diagnostics: [], failure: "bpp-lint exit 2: \(err.trimmingCharacters(in: .whitespacesAndNewlines))")
        }

        let errData = (try? errPipe.fileHandleForReading.readToEnd()) ?? Data()
        let errString = String(data: errData, encoding: .utf8) ?? ""
        return LintResult(diagnostics: parse(errString, tempFilePath: tempFile.path), failure: nil)
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

    // MARK: - Output parsing
    //
    // bpp-lint diagnostic format (with --codes):
    //   <path>:<line>:<col>: <severity> [<code>]: <message>
    //   <path>: <severity> [<code>]: <message>      (file-level, no line/col)
    // Continuation lines:
    //   "  note: <text>"
    //   "  fix:  <text>"

    nonisolated static func parse(_ text: String, tempFilePath: String) -> [Diagnostic] {
        var result: [Diagnostic] = []
        let lines = text.split(separator: "\n", omittingEmptySubsequences: false).map(String.init)
        var idx = 0
        while idx < lines.count {
            if var diag = parseLine(lines[idx], tempFilePath: tempFilePath) {
                var j = idx + 1
                while j < lines.count {
                    let s = lines[j]
                    if let note = strip(prefix: "  note:", from: s) {
                        diag.note = note
                        j += 1
                    } else if let fix = strip(prefix: "  fix:", from: s) {
                        diag.fix = fix
                        j += 1
                    } else {
                        break
                    }
                }
                result.append(diag)
                idx = j
            } else {
                idx += 1
            }
        }
        return result
    }

    private nonisolated static func strip(prefix: String, from s: String) -> String? {
        guard s.hasPrefix(prefix) else { return nil }
        return String(s.dropFirst(prefix.count)).trimmingCharacters(in: .whitespaces)
    }

    private nonisolated static func parseLine(_ line: String, tempFilePath: String) -> Diagnostic? {
        guard line.hasPrefix(tempFilePath + ":") else { return nil }
        let body = String(line.dropFirst(tempFilePath.count + 1))

        // File-level diagnostics start with a space (no line/col).
        if body.hasPrefix(" ") {
            let (sev, code, msg) = parseSeverityCodeMessage(String(body.dropFirst()))
            return Diagnostic(lineNumber: nil, column: nil,
                              severity: sev, code: code, message: msg)
        }

        // Line-anchored: "LINE:COL: severity[ [code]]: message"
        let parts = body.split(separator: ":", maxSplits: 2, omittingEmptySubsequences: false)
        guard parts.count >= 3,
              let ln = Int(parts[0]),
              let col = Int(parts[1]) else { return nil }
        let tail = String(parts[2]).drop(while: { $0 == " " })
        let (sev, code, msg) = parseSeverityCodeMessage(String(tail))
        return Diagnostic(lineNumber: ln, column: col,
                          severity: sev, code: code, message: msg)
    }

    private nonisolated static func parseSeverityCodeMessage(_ s: String) -> (Severity, String?, String) {
        // Split at the first ": " — the boundary between "<severity>[ [code]]"
        // and "<message>". Note that messages can also contain ": ", so we
        // split only once.
        guard let r = s.range(of: ": ") else {
            return (.info, nil, s)
        }
        let header  = String(s[s.startIndex..<r.lowerBound])
        let message = String(s[r.upperBound...])

        let tokens = header.split(separator: " ", maxSplits: 1, omittingEmptySubsequences: true)
        let sev: Severity
        switch String(tokens.first ?? "") {
        case "error":   sev = .error
        case "warning": sev = .warning
        case "info":    sev = .info
        default:        sev = .info
        }

        var code: String? = nil
        if tokens.count > 1 {
            let codeTok = String(tokens[1])
            if codeTok.hasPrefix("["), codeTok.hasSuffix("]") {
                code = String(codeTok.dropFirst().dropLast())
            }
        }
        return (sev, code, message)
    }
}
