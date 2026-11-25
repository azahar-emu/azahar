// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

import AzaharCore
import Foundation

/// Makes ROM bundles (.bcci, .bcxi, .bcia) like the desktop app's Create ROM Bundle: a game and its
/// update and DLC CIAs in one file, which the emulator plays without installing the update and DLC.
/// The files are chosen one by one (the picker copies them into the temporary directory), and the
/// bundle is written to the games folder.
@MainActor
final class BundleMaker: ObservableObject {
    enum Slot {
        case game, update, dlc
    }

    @Published private(set) var game: URL?
    @Published private(set) var update: URL?
    @Published private(set) var dlc: [URL] = []
    @Published private(set) var isRunning = false
    @Published private(set) var progress: Double = 0
    /// What happened, for an alert.
    @Published var message: String?

    var canCreate: Bool { game != nil && !isRunning }

    /// Shows the file picker for a slot. A file that cannot go into a bundle is refused with the
    /// reason. DLC can be several files.
    func choose(_ slot: Slot) {
        guard !isRunning else { return }
        DocumentImporter.present { [weak self] urls in
            guard let self else { return }
            var accepted: [URL] = []
            var problems: [String] = []
            for url in urls {
                if let problem = AZEmulator.bundleProblem(forFileAtPath: url.path,
                                                          asDependency: slot != .game) {
                    problems.append("\(url.lastPathComponent): \(problem)")
                    Self.removeIfTemporary(url)
                } else if slot != .dlc, !accepted.isEmpty {
                    // One game and one update.
                    Self.removeIfTemporary(url)
                } else {
                    accepted.append(url)
                }
            }
            switch slot {
            case .game:
                if let url = accepted.first {
                    if let old = game { Self.removeIfTemporary(old) }
                    game = url
                }
            case .update:
                if let url = accepted.first {
                    if let old = update { Self.removeIfTemporary(old) }
                    update = url
                }
            case .dlc:
                dlc += accepted
            }
            if !problems.isEmpty {
                message = "Not added:\n" + problems.joined(separator: "\n")
            }
        }
    }

    func clear(_ slot: Slot) {
        guard !isRunning else { return }
        switch slot {
        case .game:
            if let old = game { Self.removeIfTemporary(old) }
            game = nil
        case .update:
            if let old = update { Self.removeIfTemporary(old) }
            update = nil
        case .dlc:
            dlc.forEach(Self.removeIfTemporary)
            dlc = []
        }
    }

    /// Writes the bundle to the games folder. A .bcia (the game was a CIA) is installed right
    /// after, the library only lists games it can start directly.
    func create(library: GameLibrary) {
        guard let game, !isRunning else { return }
        isRunning = true
        progress = 0
        let dependencies = ([update].compactMap { $0 } + dlc).map(\.path)
        let destination = FileLocations.romsDirectory
        try? FileManager.default.createDirectory(at: destination, withIntermediateDirectories: true)

        AZEmulator.createBundle(
            withGameAtPath: game.path, dependencies: dependencies, toDirectory: destination.path,
            // Both are called on the main thread, Swift does not know that.
            progress: { [weak self] (fraction: Double) in
                Task { @MainActor in self?.progress = fraction }
            },
            completion: { [weak self] (success: Bool, result: String) in
                Task { @MainActor in self?.finish(success: success, result: result, library: library) }
            })
    }

    private func finish(success: Bool, result: String, library: GameLibrary) {
        isRunning = false
        guard success else {
            message = "The bundle was not created: \(result)"
            return
        }
        let output = URL(fileURLWithPath: result)
        // The copies the picker made are not needed any more.
        clear(.game)
        clear(.update)
        clear(.dlc)
        if output.pathExtension.lowercased() == "bcia" {
            message = "Created \(output.lastPathComponent) in Files › Neroli › ROMs. "
                + "It is a bundle of CIAs, so it is installed now (see the library)."
            library.importFiles([output])
        } else {
            message = "Created \(output.lastPathComponent) in Files › Neroli › ROMs. "
                + "It is in the library and plays with its update and DLC."
            library.refresh()
        }
    }

    private static func removeIfTemporary(_ url: URL) {
        if GameLibrary.isInTemporaryDirectory(url) {
            try? FileManager.default.removeItem(at: url)
        }
    }
}
