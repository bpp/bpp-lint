import SwiftUI
import AppKit

// A request to move the editor selection to a specific 1-based line (and
// optional column). `seq` increases monotonically so that re-selecting the
// same location still forces `updateNSView` to act -- SwiftUI only re-applies
// when a value changes, and line/column alone may repeat.
struct JumpRequest: Equatable {
    var line: Int
    var column: Int?
    var seq: Int
}

// SwiftUI wrapper around NSTextView so we can apply per-character styling.
// Two-way binds `text`; `jump` scrolls to and selects a token (used when a
// diagnostic -- or one of its related references -- is clicked in the side
// panel); `diagnostics` paints wavy underlines under offending tokens.
struct CtlEditor: NSViewRepresentable {
    @Binding var text: String
    var jump: JumpRequest?
    var diagnostics: [Diagnostic] = []

    func makeNSView(context: Context) -> NSScrollView {
        let scroll = NSTextView.scrollableTextView()
        guard let tv = scroll.documentView as? NSTextView else { return scroll }
        tv.delegate = context.coordinator
        tv.font = NSFont.monospacedSystemFont(ofSize: 13, weight: .regular)
        tv.isEditable = true
        tv.isSelectable = true
        tv.isFieldEditor = false
        tv.isAutomaticQuoteSubstitutionEnabled = false
        tv.isAutomaticDashSubstitutionEnabled = false
        tv.isAutomaticTextReplacementEnabled = false
        tv.isAutomaticSpellingCorrectionEnabled = false
        tv.isContinuousSpellCheckingEnabled = false
        tv.isRichText = false
        tv.allowsUndo = true
        tv.usesFindBar = true
        tv.textContainerInset = NSSize(width: 8, height: 8)
        tv.string = text
        tv.textColor = NSColor.textColor
        tv.typingAttributes = [
            .font: tv.font ?? NSFont.monospacedSystemFont(ofSize: 13, weight: .regular),
            .foregroundColor: NSColor.textColor,
        ]
        if let storage = tv.textStorage {
            BppCtlHighlighter.apply(to: storage, baseFont: tv.font)
        }
        // Focus the text view so keystrokes land in it on launch.
        DispatchQueue.main.async {
            tv.window?.makeFirstResponder(tv)
        }
        return scroll
    }

    func updateNSView(_ scroll: NSScrollView, context: Context) {
        // Refresh the coordinator's reference so its Binding writes go to
        // the current source-of-truth State setter (not a stale one).
        context.coordinator.parent = self

        guard let tv = scroll.documentView as? NSTextView,
              let storage = tv.textStorage else { return }

        if tv.string != text {
            // External text update (e.g. file open). Replace contents and
            // preserve selection where possible.
            let sel = tv.selectedRange()
            tv.string = text
            BppCtlHighlighter.apply(to: storage, baseFont: tv.font)
            let clamped = min(sel.location, (text as NSString).length)
            tv.setSelectedRange(NSRange(location: clamped, length: 0))
        }

        // Re-apply diagnostic underlines on the next runloop tick. AppKit
        // can be partway through its keystroke handling when SwiftUI fires
        // updateNSView (via a status-string state change, etc.), and any
        // layout-touching API (glyphRange, boundingRect, addToolTip)
        // throws an NSException if the textStorage is mid-transaction.
        // Deferring guarantees the current edit cycle has closed.
        let diagsSnapshot = diagnostics
        DispatchQueue.main.async { [weak tv] in
            guard let tv = tv, let storage = tv.textStorage else { return }
            Self.applyDiagnostics(diagsSnapshot, to: storage, in: tv)
        }

        if let j = jump, j.seq != context.coordinator.lastJumpSeq {
            context.coordinator.lastJumpSeq = j.seq
            let coord = context.coordinator
            DispatchQueue.main.async { [weak tv, weak coord] in
                guard let tv = tv, let coord = coord else { return }
                coord.jumpHighlightRange = Self.jump(to: j, in: tv)
            }
        }
    }

    // MARK: - Diagnostic squiggles
    //
    // NSSpellingState is the same attribute NSTextView uses internally for
    // misspelled-word squiggles. spellingState=1 paints a red wavy line
    // (error), spellingState=2 paints a green one (warning). Info-severity
    // gets a regular dotted blue underline since spellingState only
    // produces those two looks. Tooltips on hover via addToolTip(_:owner:).

    private static func applyDiagnostics(_ diagnostics: [Diagnostic],
                                         to storage: NSTextStorage,
                                         in textView: NSTextView) {
        let storageLen = storage.length
        let full = NSRange(location: 0, length: storageLen)

        storage.beginEditing()
        storage.removeAttribute(.spellingState, range: full)
        storage.removeAttribute(.underlineStyle, range: full)
        storage.removeAttribute(.underlineColor, range: full)
        storage.endEditing()

        guard storageLen > 0 else { return }
        let nsStr = storage.string as NSString

        for diag in diagnostics {
            guard let ln = diag.lineNumber, let col = diag.column else { continue }
            guard let range = tokenRange(line: ln, column: col, in: nsStr) else { continue }
            guard range.location >= 0,
                  range.location + range.length <= storage.length,
                  range.length > 0 else { continue }

            storage.beginEditing()
            switch diag.severity {
            case .error:
                storage.addAttribute(.spellingState, value: 1, range: range)
            case .warning:
                storage.addAttribute(.spellingState, value: 2, range: range)
            case .info:
                let style = NSUnderlineStyle.single.rawValue |
                            NSUnderlineStyle.patternDot.rawValue
                storage.addAttribute(.underlineStyle, value: style, range: range)
                storage.addAttribute(.underlineColor, value: NSColor.systemBlue, range: range)
            }
            storage.endEditing()
        }
        // Hover tooltips on the squiggle are deferred to a future pass --
        // glyphRange + addToolTip threw NSExceptions mid-edit-cycle. For
        // now, full diagnostic text is in the side panel.
    }

    // Compute the character range to underline for a diagnostic anchored
    // at (1-based) `line`:`column`. Underlines from `column` to the next
    // whitespace on that line ("the token the user typed"); falls back to
    // a single character if column already sits on whitespace.
    private static func tokenRange(line: Int, column: Int, in str: NSString) -> NSRange? {
        let total = str.length
        guard line >= 1, total > 0 else { return nil }

        var lineStart = 0
        var current = 1
        while current < line {
            let r = str.range(of: "\n",
                              range: NSRange(location: lineStart, length: total - lineStart))
            if r.location == NSNotFound { return nil }
            lineStart = r.location + 1
            current += 1
        }

        let restRange = NSRange(location: lineStart, length: total - lineStart)
        let nl = str.range(of: "\n", range: restRange)
        let lineEnd = (nl.location == NSNotFound) ? total : nl.location

        let colOffset = max(0, column - 1)
        let tokStart = min(lineStart + colOffset, lineEnd)
        var p = tokStart
        while p < lineEnd {
            let c = str.character(at: p)
            if c == 0x20 || c == 0x09 { break }
            p += 1
        }
        if p == tokStart {
            return tokStart < lineEnd
                ? NSRange(location: tokStart, length: 1)
                : nil
        }
        return NSRange(location: tokStart, length: p - tokStart)
    }

    func makeCoordinator() -> Coordinator { Coordinator(parent: self) }

    final class Coordinator: NSObject, NSTextViewDelegate {
        var parent: CtlEditor
        // Sequence number of the last jump we applied. A JumpRequest whose
        // `seq` matches this has already been handled and is ignored, so we
        // never fight the user's own scrolling on every re-render.
        var lastJumpSeq: Int = -1
        // Range of the current red jump highlight (a full line), kept so we can
        // clear it when the user moves the caret off that line.
        var jumpHighlightRange: NSRange?
        init(parent: CtlEditor) { self.parent = parent }

        // Clear the red jump highlight once the user clicks or selects away
        // from the highlighted line. A programmatic jump leaves the caret on
        // that line (and updates `jumpHighlightRange` only afterward), so this
        // fires only for user-driven selection changes elsewhere.
        func textViewDidChangeSelection(_ note: Notification) {
            guard let tv = note.object as? NSTextView,
                  let storage = tv.textStorage,
                  let hr = jumpHighlightRange else { return }
            let loc = tv.selectedRange().location
            if loc >= hr.location && loc <= hr.location + hr.length { return }
            storage.removeAttribute(.backgroundColor,
                                    range: NSRange(location: 0, length: storage.length))
            jumpHighlightRange = nil
        }

        func textDidChange(_ note: Notification) {
            guard let tv = note.object as? NSTextView,
                  let storage = tv.textStorage else { return }
            // Re-style the (now-mutated) text immediately so colors track
            // the keystroke. Attribute-only changes do NOT retrigger
            // textDidChange, so this is safe.
            BppCtlHighlighter.apply(to: storage, baseFont: tv.font)
            // The restyle wiped the background highlight; drop our record of it.
            jumpHighlightRange = nil

            // Defer the SwiftUI state write to the next runloop tick.
            // Writing into a @State binding synchronously from inside an
            // AppKit text-edit notification can be swallowed (or cause a
            // re-entrant updateNSView that clobbers the keystroke); the
            // DispatchQueue.main.async hop lets AppKit finish the event
            // before SwiftUI re-renders.
            let newText = tv.string
            DispatchQueue.main.async { [weak self] in
                self?.parent.text = newText
            }
        }
    }

    // Scroll to the token described by `req` and highlight it with a red
    // background. The caret is moved (zero-length) so there is no grey
    // selection band; the red highlight is cleared on the next jump here and
    // whenever the text is restyled on the next edit (see BppCtlHighlighter).
    static let jumpHighlightColor = NSColor.systemRed.withAlphaComponent(0.30)

    // Returns the range actually highlighted (a full line), so the coordinator
    // can track it and clear it when the user selects away from that line.
    @discardableResult
    private static func jump(to req: JumpRequest, in tv: NSTextView) -> NSRange? {
        guard let storage = tv.textStorage else { return nil }
        let full = NSRange(location: 0, length: storage.length)

        // Always clear any previous highlight first. A request with line < 1 is
        // a clear-only request -- e.g. the user selected a diagnostic that has
        // no source location -- so we stop here.
        storage.beginEditing()
        storage.removeAttribute(.backgroundColor, range: full)
        storage.endEditing()
        guard req.line >= 1 else { return nil }

        let nsStr = tv.string as NSString
        let range: NSRange
        if let col = req.column, let r = tokenRange(line: req.line, column: col, in: nsStr) {
            range = r
        } else if let r = lineContentRange(line: req.line, in: nsStr) {
            range = r
        } else {
            return nil
        }
        tv.setSelectedRange(NSRange(location: range.location, length: 0))
        tv.scrollRangeToVisible(range)

        // Highlight the whole line (not just the token) in red.
        let highlight = fullLineRange(line: req.line, in: nsStr) ?? range
        var applied: NSRange? = nil
        storage.beginEditing()
        if highlight.length > 0, highlight.location + highlight.length <= storage.length {
            storage.addAttribute(.backgroundColor, value: jumpHighlightColor, range: highlight)
            applied = highlight
        }
        storage.endEditing()
        return applied
    }

    // Full range of a 1-based `line`, from its first character to end of line
    // (excluding the trailing newline).
    private static func fullLineRange(line: Int, in str: NSString) -> NSRange? {
        let total = str.length
        guard line >= 1, total > 0 else { return nil }
        var lineStart = 0
        var current = 1
        while current < line {
            let r = str.range(of: "\n",
                              range: NSRange(location: lineStart, length: total - lineStart))
            if r.location == NSNotFound { return nil }
            lineStart = r.location + 1
            current += 1
        }
        let rest = NSRange(location: lineStart, length: total - lineStart)
        let nl = str.range(of: "\n", range: rest)
        let lineEnd = (nl.location == NSNotFound) ? total : nl.location
        return NSRange(location: lineStart, length: lineEnd - lineStart)
    }

    // Range covering the non-whitespace content of a 1-based `line`, used when
    // a related reference carries no specific column.
    private static func lineContentRange(line: Int, in str: NSString) -> NSRange? {
        let total = str.length
        guard line >= 1, total > 0 else { return nil }
        var lineStart = 0
        var current = 1
        while current < line {
            let r = str.range(of: "\n",
                              range: NSRange(location: lineStart, length: total - lineStart))
            if r.location == NSNotFound { return nil }
            lineStart = r.location + 1
            current += 1
        }
        let rest = NSRange(location: lineStart, length: total - lineStart)
        let nl = str.range(of: "\n", range: rest)
        let lineEnd = (nl.location == NSNotFound) ? total : nl.location
        var s = lineStart
        while s < lineEnd {
            let c = str.character(at: s)
            if c != 0x20 && c != 0x09 { break }
            s += 1
        }
        if s >= lineEnd { return NSRange(location: lineStart, length: 0) }
        return NSRange(location: s, length: lineEnd - s)
    }
}

// MARK: - Highlighter

enum BppCtlHighlighter {
    // Color choices follow Xcode's default "Plain Text" + accent conventions
    // so they look reasonable in both light and dark mode.
    static let commentColor    = NSColor.systemGray
    static let knownKeyColor   = NSColor.systemBlue
    static let unknownKeyColor = NSColor.systemRed
    static let distColor       = NSColor.systemPurple
    static let numberColor     = NSColor.systemTeal
    static let punctColor      = NSColor.tertiaryLabelColor
    static let textColor       = NSColor.textColor

    static let distributions: Set<String> = ["invgamma", "gamma", "beta", "dir", "iid"]

    static func apply(to storage: NSTextStorage, baseFont: NSFont?) {
        let full = NSRange(location: 0, length: storage.length)
        storage.beginEditing()
        // Reset to a clean baseline before reapplying per-line styling.
        storage.removeAttribute(.foregroundColor, range: full)
        storage.removeAttribute(.font, range: full)
        // Also clear any lingering jump highlight so it can't strand itself on
        // shifted character ranges after an edit.
        storage.removeAttribute(.backgroundColor, range: full)
        if let f = baseFont {
            storage.addAttribute(.font, value: f, range: full)
        }
        storage.addAttribute(.foregroundColor, value: textColor, range: full)

        let nsStr = storage.string as NSString
        nsStr.enumerateSubstrings(in: full,
                                  options: [.byLines, .substringNotRequired]) { _, lineRange, _, _ in
            highlightLine(in: storage, lineRange: lineRange, nsStr: nsStr)
        }
        storage.endEditing()
    }

    private static func highlightLine(in storage: NSTextStorage,
                                      lineRange: NSRange,
                                      nsStr: NSString) {
        // 1. Find comment start ('*' or '#'). bpp ctl has no quoted strings,
        //    so a plain forward scan is safe.
        var commentStart = -1
        for i in 0..<lineRange.length {
            let c = nsStr.character(at: lineRange.location + i)
            if c == 0x2A /* '*' */ || c == 0x23 /* '#' */ {
                commentStart = lineRange.location + i
                break
            }
        }

        // 2. Color the comment portion (to end of line).
        let contentEnd: Int
        if commentStart >= 0 {
            let commentRange = NSRange(location: commentStart,
                                       length: lineRange.location + lineRange.length - commentStart)
            storage.addAttribute(.foregroundColor, value: commentColor, range: commentRange)
            contentEnd = commentStart
        } else {
            contentEnd = lineRange.location + lineRange.length
        }

        let contentRange = NSRange(location: lineRange.location, length: contentEnd - lineRange.location)
        guard contentRange.length > 0 else { return }

        // 3. Locate '='. If absent, line is blank/continuation; leave default.
        let contentStr = nsStr.substring(with: contentRange) as NSString
        let eq = contentStr.range(of: "=")
        guard eq.location != NSNotFound else { return }

        // 4. Key range: trim ASCII whitespace.
        let keyAbsStart = contentRange.location
        let keyAbsEnd   = contentRange.location + eq.location
        var ks = keyAbsStart
        while ks < keyAbsEnd, isAsciiSpace(nsStr.character(at: ks)) { ks += 1 }
        var ke = keyAbsEnd
        while ke > ks, isAsciiSpace(nsStr.character(at: ke - 1)) { ke -= 1 }
        if ks < ke {
            let keyRange = NSRange(location: ks, length: ke - ks)
            let key = nsStr.substring(with: keyRange).lowercased()
            let color = BppKeywords.isValid(key) ? knownKeyColor : unknownKeyColor
            storage.addAttribute(.foregroundColor, value: color, range: keyRange)
        }

        // 5. '=' punctuation.
        let eqAbs = NSRange(location: contentRange.location + eq.location, length: 1)
        storage.addAttribute(.foregroundColor, value: punctColor, range: eqAbs)

        // 6. Value tokens: highlight distribution names and numeric literals.
        let valueAbsStart = contentRange.location + eq.location + 1
        let valueAbsEnd   = contentEnd
        if valueAbsStart < valueAbsEnd {
            highlightValueTokens(in: storage,
                                 range: NSRange(location: valueAbsStart, length: valueAbsEnd - valueAbsStart),
                                 nsStr: nsStr)
        }
    }

    private static func highlightValueTokens(in storage: NSTextStorage,
                                             range: NSRange,
                                             nsStr: NSString) {
        var i = range.location
        let end = range.location + range.length
        while i < end {
            // skip whitespace
            while i < end, isAsciiSpace(nsStr.character(at: i)) { i += 1 }
            if i >= end { break }
            let tokStart = i
            while i < end, !isAsciiSpace(nsStr.character(at: i)) { i += 1 }
            let tokRange = NSRange(location: tokStart, length: i - tokStart)
            let tok = nsStr.substring(with: tokRange)
            if distributions.contains(tok.lowercased()) {
                storage.addAttribute(.foregroundColor, value: distColor, range: tokRange)
            } else if isNumericToken(tok) {
                storage.addAttribute(.foregroundColor, value: numberColor, range: tokRange)
            }
        }
    }

    private static func isAsciiSpace(_ c: unichar) -> Bool {
        c == 0x20 || c == 0x09
    }

    private static func isNumericToken(_ s: String) -> Bool {
        guard !s.isEmpty else { return false }
        // Accept ints, floats, scientific notation, leading sign.
        return Double(s) != nil
    }
}

// MARK: - Known keywords (mirrors src/keywords.c)

enum BppKeywords {
    // Modern BPP 4.x keywords (inference + simulate). Source of truth: the
    // C tap-level catalogue in src/keywords.c. Update when that table grows.
    static let known: Set<String> = [
        // inference
        "seed", "arch", "nloci", "print", "model", "clock", "phase",
        "burnin", "wprior", "seqfile", "jobname", "usedata", "nsample",
        "scaling", "threads", "imapfile", "datefile", "tauprior", "heredity",
        "finetune", "sampfreq", "phiprior", "geneflow", "cleandata",
        "locusrate", "migration", "traitfile", "thetaprior", "checkpoint",
        "alphaprior", "thetamodel", "printlocus", "speciestree",
        "loadbalance", "species&tree", "constraintfile", "bayesfactorbeta",
        "debug_migration", "speciesmodelprior", "speciesdelimitation",
        // simulate
        "qrates", "seqerr", "treefile", "seqdates", "basefreqs",
        "concatfile", "loci&length", "modelparafile", "alpha_siterate"
    ]

    static func isValid(_ s: String) -> Bool {
        known.contains(s.lowercased())
    }
}
