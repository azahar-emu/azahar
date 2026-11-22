// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

import Foundation
import UIKit
import UniformTypeIdentifiers

/// Backs up and restores the save data of a game as a folder in Files › Neroli › saves.
enum SaveManager {
    enum SaveError: LocalizedError {
        case noSave
        case notASave
        case failed(String)

        var errorDescription: String? {
            switch self {
            case .noSave: return "This game has no save data yet."
            case .notASave: return "That folder does not hold save data of a game."
            case .failed(let reason): return reason
            }
        }
    }

    /// Where the backups are written.
    static var backupsDirectory: URL {
        FileLocations.userDirectory.appendingPathComponent("saves", isDirectory: true)
    }

    /// The save data of a game: the data folder of its title on the console's SD card.
    static func saveDirectory(of game: Game) -> URL {
        // Saves belong to the application (0004 0000), whatever kind of file the game came from.
        let low = String(format: "%08x", UInt32(truncatingIfNeeded: game.programID))
        let zeros = String(repeating: "0", count: 32)
        return FileLocations.sdmcDirectory
            .appendingPathComponent("Nintendo 3DS/\(zeros)/\(zeros)/title/00040000/\(low)/data",
                                    isDirectory: true)
    }

    static func hasSave(of game: Game) -> Bool {
        FileManager.default.fileExists(atPath: saveDirectory(of: game).path)
    }

    private static func tag(of game: Game) -> String {
        "[\(game.programIDString)]"
    }

    private static func safeName(_ title: String) -> String {
        let bad = CharacterSet(charactersIn: "/:\\?%*|\"<>\n")
        return title.components(separatedBy: bad).joined(separator: " ")
            .trimmingCharacters(in: .whitespaces)
    }

    private static func stamp() -> String {
        let formatter = DateFormatter()
        formatter.dateFormat = "yyyy-MM-dd HH-mm-ss"
        return formatter.string(from: Date())
    }

    /// Copies the save data into a new folder of the backups folder and returns it.
    @discardableResult
    static func export(_ game: Game, suffix: String = "") throws -> URL {
        let source = saveDirectory(of: game)
        guard FileManager.default.fileExists(atPath: source.path) else { throw SaveError.noSave }
        let name = "\(safeName(game.title)) \(tag(of: game)) \(stamp())\(suffix)"
        let destination = backupsDirectory.appendingPathComponent(name, isDirectory: true)
        do {
            try FileManager.default.createDirectory(at: backupsDirectory,
                                                    withIntermediateDirectories: true)
            try FileManager.default.copyItem(at: source, to: destination)
        } catch {
            throw SaveError.failed(error.localizedDescription)
        }
        return destination
    }

    /// The backups of a game, newest first.
    static func backups(of game: Game) -> [URL] {
        let files = (try? FileManager.default.contentsOfDirectory(
            at: backupsDirectory, includingPropertiesForKeys: [.isDirectoryKey])) ?? []
        return files.filter { $0.lastPathComponent.contains(tag(of: game)) }
            .sorted { $0.lastPathComponent > $1.lastPathComponent }
    }

    /// Replaces the save data of the game with the content of a folder. The save it had is kept
    /// as a backup first.
    static func restore(_ game: Game, from folder: URL) throws {
        let fileManager = FileManager.default
        let accessing = folder.startAccessingSecurityScopedResource()
        defer { if accessing { folder.stopAccessingSecurityScopedResource() } }

        // A save folder holds the folder 00000001, the folder chosen may also be the one above.
        var source = folder
        if !fileManager.fileExists(atPath: source.appendingPathComponent("00000001").path) {
            let inner = folder.appendingPathComponent("data")
            if fileManager.fileExists(atPath: inner.appendingPathComponent("00000001").path) {
                source = inner
            } else {
                throw SaveError.notASave
            }
        }

        let target = saveDirectory(of: game)
        if fileManager.fileExists(atPath: target.path) {
            try export(game, suffix: " (before restore)")
            try? fileManager.removeItem(at: target)
        }
        do {
            try fileManager.createDirectory(at: target.deletingLastPathComponent(),
                                            withIntermediateDirectories: true)
            try fileManager.copyItem(at: source, to: target)
        } catch {
            throw SaveError.failed(error.localizedDescription)
        }
    }
}

/// The system's folder picker for a folder to read (not copied into the app).
@MainActor
final class FolderPicker: NSObject, UIDocumentPickerDelegate {
    private static var active: FolderPicker?
    private let completion: @MainActor (URL?) -> Void

    private init(completion: @escaping @MainActor (URL?) -> Void) {
        self.completion = completion
    }

    @discardableResult
    static func present(completion: @escaping @MainActor (URL?) -> Void) -> Bool {
        guard active == nil else { return false }
        let scenes = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }
        let scene = scenes.first { $0.activationState == .foregroundActive } ?? scenes.first
        let window = scene?.windows.first { $0.isKeyWindow } ?? scene?.windows.first
        var presenter = window?.rootViewController
        while let presented = presenter?.presentedViewController {
            presenter = presented
        }
        guard let presenter else { return false }
        let picker = FolderPicker(completion: completion)
        let controller = UIDocumentPickerViewController(forOpeningContentTypes: [.folder])
        controller.delegate = picker
        controller.allowsMultipleSelection = false
        active = picker
        presenter.present(controller, animated: true)
        return true
    }

    nonisolated func documentPicker(
        _ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]
    ) {
        Task { @MainActor in
            let completion = self.completion
            FolderPicker.active = nil
            completion(urls.first)
        }
    }

    nonisolated func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) {
        Task { @MainActor in
            FolderPicker.active = nil
        }
    }
}
