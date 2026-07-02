import Foundation

// A reference from one diagnostic to another location in the buffer that
// participates in the same underlying problem. Cross-keyword diagnostics
// (BPP120-127) fire on one keyword line but are *caused* by the values of
// other keywords elsewhere -- e.g. a `speciesmodelprior` error is triggered by
// the `speciesdelimitation` and `speciestree` settings. The linter names those
// partners in its message / note text; we resolve those names (and any explicit
// "line N" mentions) back to the lines where they are assigned in the buffer so
// the UI can link an error to the settings that provoked it.
struct RelatedRef: Identifiable, Hashable {
    let id = UUID()
    let line: Int
    let column: Int?
    let keyword: String?
    let snippet: String

    func hash(into hasher: inout Hasher) { hasher.combine(id) }
    static func == (a: RelatedRef, b: RelatedRef) -> Bool { a.id == b.id }
}

enum RelationResolver {

    /// Build a map of `diagnostic.id -> [RelatedRef]` from the diagnostics and
    /// the buffer `text` they were produced from. Only diagnostics that mention
    /// other keywords / lines get entries.
    static func resolve(_ diagnostics: [Diagnostic], in text: String) -> [UUID: [RelatedRef]] {
        let lines = text.components(separatedBy: "\n")
        let assignments = keywordAssignments(lines)

        var out: [UUID: [RelatedRef]] = [:]
        for diag in diagnostics {
            let refs = related(for: diag, assignments: assignments, lines: lines)
            if !refs.isEmpty { out[diag.id] = refs }
        }
        return out
    }

    // MARK: - Buffer indexing

    // Map each known keyword to the (1-based line, 1-based column) of its FIRST
    // `keyword = ...` assignment in the buffer.
    private static func keywordAssignments(_ lines: [String]) -> [String: (line: Int, column: Int)] {
        var map: [String: (line: Int, column: Int)] = [:]
        for (i, raw) in lines.enumerated() {
            guard let eq = raw.range(of: "=") else { continue }
            let key = raw[raw.startIndex..<eq.lowerBound]
            let leading = key.prefix(while: { $0 == " " || $0 == "\t" })
            let name = key.dropFirst(leading.count)
                .trimmingCharacters(in: .whitespaces)
                .lowercased()
            guard !name.isEmpty, BppKeywords.isValid(name) else { continue }
            if map[name] == nil {
                map[name] = (line: i + 1, column: leading.count + 1)
            }
        }
        return map
    }

    // MARK: - Per-diagnostic resolution

    private static func related(for diag: Diagnostic,
                                assignments: [String: (line: Int, column: Int)],
                                lines: [String]) -> [RelatedRef] {
        let haystack = [diag.message, diag.note, diag.fix]
            .compactMap { $0 }
            .joined(separator: " ")

        var seenLines = Set<Int>()
        var result: [RelatedRef] = []

        func add(line: Int, column: Int?, keyword: String?) {
            guard line >= 1, line <= lines.count else { return }
            if diag.lineNumber == line { return }          // don't point at self
            guard seenLines.insert(line).inserted else { return }
            let snippet = lines[line - 1].trimmingCharacters(in: .whitespaces)
            result.append(RelatedRef(line: line, column: column, keyword: keyword, snippet: snippet))
        }

        // 1. Known keyword names mentioned anywhere in the message text
        //    (quoted or not, e.g. "'usedata = 0'" or "(speciesdelimitation=1)").
        for kw in mentionedKeywords(haystack) {
            if let a = assignments[kw] {
                add(line: a.line, column: a.column, keyword: kw)
            }
        }

        // 2. Explicit "line N" mentions.
        for n in lineMentions(haystack) {
            add(line: n, column: nil, keyword: nil)
        }

        return result.sorted { $0.line < $1.line }
    }

    // MARK: - Text extraction

    // Whole-word keyword matches. Uses lookarounds rather than \b because '&'
    // (in "species&tree") is not a word character and would break \b anchoring.
    private static let keywordRegex: NSRegularExpression = {
        // Longest first so "species&tree" wins over any prefix.
        let alt = BppKeywords.known
            .sorted { $0.count > $1.count }
            .map { NSRegularExpression.escapedPattern(for: $0) }
            .joined(separator: "|")
        let pattern = "(?<![A-Za-z0-9_&])(?:\(alt))(?![A-Za-z0-9_])"
        return try! NSRegularExpression(pattern: pattern, options: [.caseInsensitive])
    }()

    private static func mentionedKeywords(_ s: String) -> [String] {
        let ns = s as NSString
        var found: [String] = []
        var seen = Set<String>()
        keywordRegex.enumerateMatches(in: s, range: NSRange(location: 0, length: ns.length)) { m, _, _ in
            guard let m = m else { return }
            let kw = ns.substring(with: m.range).lowercased()
            if seen.insert(kw).inserted { found.append(kw) }
        }
        return found
    }

    private static let lineRegex = try! NSRegularExpression(
        pattern: "\\bline\\s+([0-9]+)", options: [.caseInsensitive])

    private static func lineMentions(_ s: String) -> [Int] {
        let ns = s as NSString
        var out: [Int] = []
        lineRegex.enumerateMatches(in: s, range: NSRange(location: 0, length: ns.length)) { m, _, _ in
            guard let m = m, m.numberOfRanges >= 2,
                  let n = Int(ns.substring(with: m.range(at: 1))) else { return }
            out.append(n)
        }
        return out
    }
}
