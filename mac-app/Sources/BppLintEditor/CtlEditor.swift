import SwiftUI
import AppKit

// SwiftUI wrapper around NSTextView so we can apply per-character styling.
// Two-way binds `text`; `highlightedLine` optionally scrolls / shades a line
// (used when a diagnostic is selected in the side panel).
struct CtlEditor: NSViewRepresentable {
    @Binding var text: String
    var highlightedLine: Int?

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

        if let line = highlightedLine {
            scrollTo(line: line, in: tv)
        }
    }

    func makeCoordinator() -> Coordinator { Coordinator(parent: self) }

    final class Coordinator: NSObject, NSTextViewDelegate {
        var parent: CtlEditor
        init(parent: CtlEditor) { self.parent = parent }

        func textDidChange(_ note: Notification) {
            guard let tv = note.object as? NSTextView,
                  let storage = tv.textStorage else { return }
            // Re-style the (now-mutated) text immediately so colors track
            // the keystroke. Attribute-only changes do NOT retrigger
            // textDidChange, so this is safe.
            BppCtlHighlighter.apply(to: storage, baseFont: tv.font)

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

    private func scrollTo(line: Int, in tv: NSTextView) {
        let nsStr = tv.string as NSString
        var currentLine = 1
        var location = 0
        nsStr.enumerateSubstrings(in: NSRange(location: 0, length: nsStr.length),
                                  options: [.byLines, .substringNotRequired]) { _, lineRange, _, stop in
            if currentLine == line {
                location = lineRange.location
                stop.pointee = true
            }
            currentLine += 1
        }
        tv.scrollRangeToVisible(NSRange(location: location, length: 0))
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
