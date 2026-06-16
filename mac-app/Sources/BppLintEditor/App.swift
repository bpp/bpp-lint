import SwiftUI
import AppKit

extension Notification.Name {
    static let ctlEditorOpenFile = Notification.Name("BppLintEditor.OpenFile")
    static let ctlEditorSaveFile = Notification.Name("BppLintEditor.SaveFile")
    static let ctlEditorRelint   = Notification.Name("BppLintEditor.Relint")
}

@main
struct BppLintEditorApp: App {
    var body: some Scene {
        WindowGroup("BPP Lint Editor") {
            ContentView()
                .frame(minWidth: 900, minHeight: 600)
        }
        .commands {
            // File menu: replace the default "New" group with our Open/Save.
            CommandGroup(replacing: .newItem) {
                Button("Open…") {
                    NotificationCenter.default.post(name: .ctlEditorOpenFile, object: nil)
                }
                .keyboardShortcut("o", modifiers: .command)

                Button("Save…") {
                    NotificationCenter.default.post(name: .ctlEditorSaveFile, object: nil)
                }
                .keyboardShortcut("s", modifiers: .command)
            }
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
    @State private var status: String = "linting…"
    @StateObject private var runner = LinterRunner()
    @State private var selectedDiagnosticID: Diagnostic.ID? = nil

    var body: some View {
        VStack(spacing: 0) {
            toolbar
            Divider()
            HSplitView {
                CtlEditor(text: $text, highlightedLine: highlightedLine)
                    .frame(minWidth: 400)
                DiagnosticsList(
                    diagnostics: diagnostics,
                    selection: $selectedDiagnosticID
                )
                .frame(minWidth: 280)
            }
        }
        .onChange(of: text) { newValue in
            relint(newValue)
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

    private var toolbar: some View {
        HStack(spacing: 12) {
            Button("Open…") { openFile() }
                .keyboardShortcut("o", modifiers: .command)
            Button("Save…") { saveFile() }
                .keyboardShortcut("s", modifiers: .command)
            Spacer()
            Text(status)
                .font(.caption.monospaced())
                .foregroundColor(.secondary)
        }
        .padding(8)
    }

    private var highlightedLine: Int? {
        guard let id = selectedDiagnosticID,
              let diag = diagnostics.first(where: { $0.id == id })
        else { return nil }
        return diag.lineNumber
    }

    private func relint(_ s: String) {
        status = "linting…"
        runner.scheduleLint(of: s) { result in
            diagnostics = result.diagnostics
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

struct DiagnosticsList: View {
    let diagnostics: [Diagnostic]
    @Binding var selection: Diagnostic.ID?

    var body: some View {
        if diagnostics.isEmpty {
            VStack {
                Spacer()
                Text("No diagnostics")
                    .foregroundColor(.secondary)
                Spacer()
            }
            .frame(maxWidth: .infinity)
        } else {
            List(diagnostics, selection: $selection) { d in
                DiagnosticRow(diagnostic: d).tag(d.id)
            }
            .listStyle(.inset)
        }
    }
}

struct DiagnosticRow: View {
    let diagnostic: Diagnostic

    var body: some View {
        VStack(alignment: .leading, spacing: 3) {
            HStack(spacing: 6) {
                Image(systemName: diagnostic.severity.iconName)
                    .foregroundColor(diagnostic.severity.color)
                if let ln = diagnostic.lineNumber {
                    Text("L\(ln)")
                        .font(.caption.monospaced())
                        .foregroundColor(.secondary)
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
        }
        .padding(.vertical, 2)
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
