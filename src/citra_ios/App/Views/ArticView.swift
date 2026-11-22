// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

import AzaharCore
import SwiftUI

/// Streams a game from a real 3DS running Artic Base, or sets up the system files of a console
/// with the Artic Setup Tool.
struct ArticView: View {
    private enum Mode: String, CaseIterable, Identifiable {
        case play = "Play a Game"
        case setup = "Set Up System Files"

        var id: String { rawValue }
    }

    @EnvironmentObject private var session: EmulationSession
    @EnvironmentObject private var settings: AppSettings
    @Environment(\.dismiss) private var dismiss

    @AppStorage("articAddress") private var address = ""
    @State private var mode = Mode.play
    @State private var setupOld3DS = true
    @State private var confirmingReinstall = false

    private let old3DSInstalled = AZEmulator.areOld3DSSystemFilesInstalled()
    private let new3DSInstalled = AZEmulator.areNew3DSSystemFilesInstalled()

    private var trimmedAddress: String {
        address.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    Picker("Mode", selection: $mode) {
                        ForEach(Mode.allCases) { Text($0.rawValue).tag($0) }
                    }
                    .pickerStyle(.segmented)
                }

                Section {
                    TextField("192.168.0.10", text: $address)
                        .keyboardType(.numbersAndPunctuation)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                } header: {
                    Text(mode == .play ? "Artic Base Address" : "Artic Setup Tool Address")
                } footer: {
                    if mode == .play {
                        Text("Run Artic Base on your 3DS, start the game you want to play there "
                             + "and enter the address it shows. The game is streamed over your "
                             + "network: both devices have to be on the same Wi-Fi and the app "
                             + "needs local network access (iOS Settings).")
                    } else {
                        Text("Run the Artic Setup Tool on your 3DS and enter the address it "
                             + "shows.")
                    }
                }

                if mode == .setup {
                    Section {
                        Picker("Setup", selection: $setupOld3DS) {
                            Text(old3DSInstalled ? "Old 3DS Setup ✓" : "Old 3DS Setup").tag(true)
                            Text(new3DSInstalled ? "New 3DS Setup ✓" : "New 3DS Setup").tag(false)
                        }
                        .pickerStyle(.inline)
                        .labelsHidden()
                    } footer: {
                        Text("This installs console unique data from your console. Do not share "
                             + "your user or nand folders afterwards. Don't go online with both "
                             + "Neroli and your 3DS at the same time after the setup. The Old 3DS "
                             + "setup is needed for the New 3DS setup, doing both is recommended; "
                             + "both work whatever the model of your console is.")
                    }
                }

                Section {
                    Button(mode == .play ? "Connect" : "Start Setup") { connect() }
                        .disabled(trimmedAddress.isEmpty
                                  || (mode == .setup && !setupOld3DS && !old3DSInstalled))
                }
            }
            .navigationTitle("Artic")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancel") { dismiss() }
                }
            }
            .confirmationDialog(
                "The system files of this setup are already installed. Install them again?",
                isPresented: $confirmingReinstall, titleVisibility: .visible
            ) {
                Button("Reinstall", role: .destructive) { startSetup() }
            }
        }
    }

    private func connect() {
        switch mode {
        case .play:
            launch(path: "articbase://\(trimmedAddress)", title: "Artic Base")
        case .setup:
            let installed = setupOld3DS ? old3DSInstalled : new3DSInstalled
            if installed {
                confirmingReinstall = true
            } else {
                startSetup()
            }
        }
    }

    private func startSetup() {
        AZEmulator.uninstallSystemFiles(forOld3DS: setupOld3DS)
        let scheme = setupOld3DS ? "articinio" : "articinin"
        launch(path: "\(scheme)://\(trimmedAddress)", title: "Artic Setup Tool")
    }

    private func launch(path: String, title: String) {
        dismiss()
        let game = Game(
            url: URL(fileURLWithPath: "/artic"), title: title, publisher: trimmedAddress,
            regions: "", fileType: "Artic", programID: 0, isEncrypted: false, icon: nil,
            launchPath: path)
        session.launch(game, settings: settings)
    }
}
