import SwiftUI
import AppKit

extension Notification.Name {
    static let ctlEditorOpenFile = Notification.Name("BppLintEditor.OpenFile")
    static let ctlEditorSaveFile = Notification.Name("BppLintEditor.SaveFile")
    static let ctlEditorRelint   = Notification.Name("BppLintEditor.Relint")
}

// SwiftUI apps launched via `swift run` (no Info.plist, no .app bundle)
// default to .accessory activation - no Dock icon, no menu bar, no key
// focus on windows. Force regular activation so the editor behaves as a
// normal foreground macOS app.
final class AppDelegate: NSObject, NSApplicationDelegate {
    override init() {
        super.init()
        // Suppress the system-injected "Start Dictation" and "Emoji & Symbols"
        // items AppKit otherwise appends to the Edit menu -- neither applies to
        // editing a BPP control file.
        UserDefaults.standard.register(defaults: [
            "NSDisabledDictationMenuItem": true,
            "NSDisabledCharacterPaletteMenuItem": true,
        ])
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)
        pruneEditMenu()
    }

    func applicationDidBecomeActive(_ notification: Notification) {
        // Re-prune in case the text services were re-attached on activation.
        pruneEditMenu()
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        true
    }

    // Remove Edit-menu submenus that carry no meaning for plain control-file
    // text: spell-checking is disabled on the editor, and there is nothing to
    // speak, transform, or auto-substitute.
    private func pruneEditMenu() {
        DispatchQueue.main.async {
            guard let editMenu = NSApp.mainMenu?.items
                .first(where: { $0.title == "Edit" })?.submenu else { return }
            let drop: Set<String> = [
                "Spelling and Grammar", "Substitutions", "Transformations",
                "Speech", "Start Dictation…", "Emoji & Symbols", "AutoFill",
            ]
            for item in editMenu.items where drop.contains(item.title) {
                editMenu.removeItem(item)
            }
            Self.collapseSeparators(editMenu)
        }
    }

    // Drop leading, trailing, and doubled separators left behind by removals.
    private static func collapseSeparators(_ menu: NSMenu) {
        var i = menu.items.count - 1
        while i > 0 {
            let cur = menu.items[i]
            if cur.isSeparatorItem,
               i == menu.items.count - 1 || menu.items[i - 1].isSeparatorItem {
                menu.removeItem(cur)
            }
            i -= 1
        }
        if let first = menu.items.first, first.isSeparatorItem {
            menu.removeItem(first)
        }
    }
}

@main
struct BppLintEditorApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate

    var body: some Scene {
        WindowGroup("BPP Lint Editor") {
            ContentView()
                .frame(minWidth: 900, minHeight: 600)
        }
        .commands {
            // File menu: Open replaces the default New/templates group, Save
            // takes the natural Save slot.
            CommandGroup(replacing: .newItem) {
                Button("Open…") {
                    NotificationCenter.default.post(name: .ctlEditorOpenFile, object: nil)
                }
                .keyboardShortcut("o", modifiers: .command)
            }
            CommandGroup(replacing: .saveItem) {
                Button("Save…") {
                    NotificationCenter.default.post(name: .ctlEditorSaveFile, object: nil)
                }
                .keyboardShortcut("s", modifiers: .command)
            }
            // Strip menus that don't apply to a plain-text control-file editor.
            CommandGroup(replacing: .textFormatting) { }
            CommandGroup(replacing: .toolbar) { }
            CommandGroup(replacing: .sidebar) { }
            CommandGroup(replacing: .help) { }
            // Custom Lint menu.
            CommandMenu("Lint") {
                Button("Re-run Linter") {
                    NotificationCenter.default.post(name: .ctlEditorRelint, object: nil)
                }
                .keyboardShortcut("r", modifiers: .command)
            }
        }
    }
}

struct ContentView: View {
    @State private var text: String = sampleControlFile
    @State private var diagnostics: [Diagnostic] = []
    @State private var relations: [UUID: [RelatedRef]] = [:]
    @State private var status: String = "linting…"
    @StateObject private var runner = LinterRunner()
    @State private var selectedDiagnosticID: Diagnostic.ID? = nil
    @State private var jump: JumpRequest? = nil
    @State private var jumpSeq: Int = 0

    var body: some View {
        VStack(spacing: 0) {
            statusBar
            Divider()
            HSplitView {
                CtlEditor(text: $text,
                          jump: jump,
                          diagnostics: diagnostics)
                    .frame(minWidth: 400)
                DiagnosticsPanel(
                    diagnostics: diagnostics,
                    relations: relations,
                    selection: $selectedDiagnosticID,
                    onJump: { line, col in jumpTo(line: line, column: col) },
                    onApplyDefault: { applyDefault($0) }
                )
                .frame(minWidth: 300)
            }
        }
        .onChange(of: text) { newValue in
            relint(newValue)
        }
        .onChange(of: selectedDiagnosticID) { id in
            guard let id, let d = diagnostics.first(where: { $0.id == id }) else {
                clearHighlight()
                return
            }
            if let ln = d.lineNumber {
                jumpTo(line: ln, column: d.column)
            } else {
                // Diagnostic has no source location (e.g. a file-level default
                // warning); drop the stale highlight rather than leaving it.
                clearHighlight()
            }
        }
        .onAppear {
            relint(text)
        }
        // Menu-bar commands route through NotificationCenter so the
        // top-level Scene can reach this view's state.
        .onReceive(NotificationCenter.default.publisher(for: .ctlEditorOpenFile)) { _ in
            openFile()
        }
        .onReceive(NotificationCenter.default.publisher(for: .ctlEditorSaveFile)) { _ in
            saveFile()
        }
        .onReceive(NotificationCenter.default.publisher(for: .ctlEditorRelint)) { _ in
            relint(text)
        }
    }

    // File actions live in the native menu bar (File menu). This bar just
    // reports the current lint status.
    private var statusBar: some View {
        HStack {
            Spacer()
            Text(status)
                .font(.caption.monospaced())
                .foregroundColor(.secondary)
        }
        .padding(8)
    }

    private func jumpTo(line: Int, column: Int?) {
        jumpSeq += 1
        jump = JumpRequest(line: line, column: column, seq: jumpSeq)
    }

    // Clear the editor's red jump highlight without moving the caret. Modeled
    // as a jump to a non-existent line (< 1), which the editor treats as
    // clear-only.
    private func clearHighlight() {
        jumpSeq += 1
        jump = JumpRequest(line: 0, column: nil, seq: jumpSeq)
    }

    // Turn an implicit default (BPP103 warning) into an explicit assignment by
    // appending "keyword = value" to the buffer. Appending is deliberate:
    // inserting mid-file could split the contiguous species&tree block, whereas
    // a keyword line at the end is always safe. Re-linting then drops the
    // warning.
    private func applyDefault(_ diagnostic: Diagnostic) {
        guard let a = diagnostic.defaultAssignment else { return }
        text = Self.appendingAssignment(keyword: a.keyword, value: a.value, to: text)
    }

    private static func appendingAssignment(keyword: String, value: String, to source: String) -> String {
        // No-op if the keyword is already assigned somewhere (shouldn't happen
        // for a "not set" warning, but guards against a double-click before the
        // re-lint lands).
        let kwRe = try! NSRegularExpression(
            pattern: "^\\s*" + NSRegularExpression.escapedPattern(for: keyword) + "\\s*=",
            options: [.caseInsensitive])
        for line in source.components(separatedBy: "\n") {
            let range = NSRange(location: 0, length: (line as NSString).length)
            if kwRe.firstMatch(in: line, range: range) != nil { return source }
        }
        var s = source
        if !s.isEmpty && !s.hasSuffix("\n") { s += "\n" }
        s += "\(keyword) = \(value)\n"
        return s
    }

    private func relint(_ s: String) {
        status = "linting…"
        runner.scheduleLint(of: s) { result in
            diagnostics = result.diagnostics
            relations = RelationResolver.resolve(result.diagnostics, in: s)
            if let err = result.failure {
                status = "lint error: \(err)"
            } else {
                let errs = result.diagnostics.filter { $0.severity == .error }.count
                let warns = result.diagnostics.filter { $0.severity == .warning }.count
                status = "\(errs) error\(errs == 1 ? "" : "s"), \(warns) warning\(warns == 1 ? "" : "s")"
            }
        }
    }

    private func openFile() {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        panel.allowedContentTypes = [.text, .data]
        if panel.runModal() == .OK, let url = panel.url,
           let s = try? String(contentsOf: url, encoding: .utf8) {
            text = s
        }
    }

    private func saveFile() {
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.text]
        panel.nameFieldStringValue = "untitled.ctl"
        if panel.runModal() == .OK, let url = panel.url {
            try? text.write(to: url, atomically: true, encoding: .utf8)
        }
    }
}

struct DiagnosticsPanel: View {
    let diagnostics: [Diagnostic]
    let relations: [UUID: [RelatedRef]]
    @Binding var selection: Diagnostic.ID?
    let onJump: (Int, Int?) -> Void
    let onApplyDefault: (Diagnostic) -> Void

    @State private var tab: Severity = .error

    private func items(_ sev: Severity) -> [Diagnostic] {
        diagnostics.filter { $0.severity == sev }
    }

    var body: some View {
        VStack(spacing: 0) {
            Picker("", selection: $tab) {
                Text("Errors (\(items(.error).count))").tag(Severity.error)
                Text("Warnings (\(items(.warning).count))").tag(Severity.warning)
                Text("Info (\(items(.info).count))").tag(Severity.info)
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            .padding(8)
            Divider()

            let current = items(tab)
            if current.isEmpty {
                VStack {
                    Spacer()
                    Text("No \(label(tab))")
                        .foregroundColor(.secondary)
                    Spacer()
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                List(current, selection: $selection) { d in
                    DiagnosticRow(diagnostic: d,
                                  related: relations[d.id] ?? [],
                                  onJump: onJump,
                                  onApplyDefault: onApplyDefault)
                        .tag(d.id)
                }
                .listStyle(.inset)
            }
        }
        .onAppear { selectSensibleTab() }
        .onChange(of: diagnostics.count) { _ in selectSensibleTab() }
    }

    // If the visible tab has nothing, fall through to the most severe tab that
    // does, so the user always lands on something actionable.
    private func selectSensibleTab() {
        if !items(tab).isEmpty { return }
        if !items(.error).isEmpty { tab = .error }
        else if !items(.warning).isEmpty { tab = .warning }
        else if !items(.info).isEmpty { tab = .info }
    }

    private func label(_ sev: Severity) -> String {
        switch sev {
        case .error:   return "errors"
        case .warning: return "warnings"
        case .info:    return "info"
        }
    }
}

struct DiagnosticRow: View {
    let diagnostic: Diagnostic
    var related: [RelatedRef] = []
    var onJump: ((Int, Int?) -> Void)? = nil
    var onApplyDefault: ((Diagnostic) -> Void)? = nil

    var body: some View {
        VStack(alignment: .leading, spacing: 3) {
            HStack(spacing: 6) {
                Image(systemName: diagnostic.severity.iconName)
                    .foregroundColor(diagnostic.severity.color)
                if let ln = diagnostic.lineNumber {
                    Button {
                        onJump?(ln, diagnostic.column)
                    } label: {
                        Text("L\(ln)").font(.caption.monospaced())
                    }
                    .buttonStyle(.link)
                    .help("Jump to line \(ln)")
                }
                if let code = diagnostic.code {
                    Text("[\(code)]")
                        .font(.caption.monospaced())
                        .foregroundColor(.secondary)
                }
            }
            Text(diagnostic.message)
                .font(.system(.body))
                .fixedSize(horizontal: false, vertical: true)
            if let note = diagnostic.note {
                Text(note)
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            if let fix = diagnostic.fix {
                Text("fix: \(fix)")
                    .font(.caption.monospaced())
                    .foregroundColor(.green)
                    .fixedSize(horizontal: false, vertical: true)
            }
            if let assignment = diagnostic.defaultAssignment, let apply = onApplyDefault {
                Button {
                    apply(diagnostic)
                } label: {
                    Label("Set explicitly: \(assignment.keyword) = \(assignment.value)",
                          systemImage: "plus.circle")
                        .font(.caption)
                }
                .buttonStyle(.borderless)
                .controlSize(.small)
                .help("Add '\(assignment.keyword) = \(assignment.value)' to the file to silence this warning")
                .padding(.top, 2)
            }
            if !related.isEmpty {
                relatedView
            }
        }
        .padding(.vertical, 2)
    }

    // The settings that provoked this diagnostic, each a click-to-jump link
    // into the source. Turns "error over here, cause over there" into a
    // navigable chain.
    private var relatedView: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text("caused by")
                .font(.caption2.weight(.semibold))
                .foregroundColor(.secondary)
                .textCase(.uppercase)
            ForEach(related) { ref in
                Button {
                    onJump?(ref.line, ref.column)
                } label: {
                    HStack(spacing: 4) {
                        Image(systemName: "arrow.turn.down.right")
                            .font(.caption2)
                            .foregroundColor(.secondary)
                        Text("L\(ref.line)")
                            .font(.caption2.monospaced())
                            .foregroundColor(.secondary)
                        Text(ref.snippet)
                            .font(.caption2.monospaced())
                            .lineLimit(1)
                            .truncationMode(.tail)
                            .foregroundColor(.primary)
                    }
                }
                .buttonStyle(.plain)
                .help("Jump to line \(ref.line)")
            }
        }
        .padding(.top, 3)
        .padding(.leading, 6)
    }
}

private let sampleControlFile = """
* A tiny BPP 4.x control file. Edit me; linter runs on each keystroke.

         seqfile = frogs.txt
        imapfile = frogs.imap.txt
         jobname = frogs

  speciesdelimitation = 0
          speciestree = 0
    speciesmodelprior = 1

       species&tree = 4  K  L  N  M
                          5  4  3  2
                       (K, (L, (N, M)));

         phase = 0 0 0 0

       usedata = 1
         nloci = 5
     cleandata = 0

    thetaprior = invgamma 3 0.002
      tauprior = invgamma 3 0.04

         print = 1 0 0 0 0
        burnin = 8000
      sampfreq = 2
       nsample = 100000
"""
