// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

import SwiftUI

/// Loads amiibo files onto the virtual NFC reader of the running game.
struct AmiiboView: View {
    @EnvironmentObject private var session: EmulationSession
    @Environment(\.dismiss) private var dismiss

    @State private var files: [URL] = []
    @State private var error: String?

    var body: some View {
        NavigationStack {
            List {
                Section {
                    Button("Import Amiibo File…") { importFile() }
                    Button("Remove Amiibo", role: .destructive) {
                        session.removeAmiibo()
                        dismiss()
                    }
                } footer: {
                    Text("The game has to ask for an amiibo (its amiibo screen) before one can be "
                         + "loaded. Imported files are kept in Files › Neroli › amiibo, a game "
                         + "saves what it writes to an amiibo into that file.")
                }

                if !files.isEmpty {
                    Section("Imported") {
                        ForEach(files, id: \.self) { url in
                            Button(url.deletingPathExtension().lastPathComponent) {
                                session.loadAmiibo(at: url)
                                dismiss()
                            }
                        }
                        .onDelete { offsets in
                            for index in offsets {
                                try? FileManager.default.removeItem(at: files[index])
                            }
                            reload()
                        }
                    }
                }
            }
            .navigationTitle("Amiibo")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
            .onAppear(perform: reload)
            .alert("Import Failed", isPresented: Binding(
                get: { error != nil }, set: { if !$0 { error = nil } })
            ) {
                Button("OK", role: .cancel) {}
            } message: {
                Text(error ?? "")
            }
        }
    }

    private func reload() {
        let urls = (try? FileManager.default.contentsOfDirectory(
            at: FileLocations.amiiboDirectory, includingPropertiesForKeys: nil)) ?? []
        files = urls
            .filter { $0.pathExtension.lowercased() == "bin" }
            .sorted { $0.lastPathComponent.localizedCaseInsensitiveCompare($1.lastPathComponent)
                == .orderedAscending }
    }

    private func importFile() {
        let presented = DocumentImporter.present { urls in
            let directory = FileLocations.amiiboDirectory
            try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            for url in urls {
                let target = directory.appendingPathComponent(url.lastPathComponent)
                // An amiibo that is already here keeps the progress a game saved into it.
                if !FileManager.default.fileExists(atPath: target.path) {
                    do {
                        try FileManager.default.copyItem(at: url, to: target)
                    } catch {
                        self.error = "\(url.lastPathComponent): \(error.localizedDescription)"
                    }
                }
                try? FileManager.default.removeItem(at: url)
            }
            reload()
        }
        if !presented {
            error = "The file picker could not be opened."
        }
    }
}
