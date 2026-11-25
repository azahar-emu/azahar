// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

import AzaharCore
import Foundation
import UIKit

struct Game: Identifiable, Hashable {
    let url: URL
    let title: String
    let publisher: String
    let regions: String
    let fileType: String
    let programID: UInt64
    let isEncrypted: Bool
    let icon: UIImage?
    /// Installed from a CIA rather than imported as a file.
    var isInstalled = false
    /// What the emulator is started with when it is not the path of a file (Artic addresses).
    var launchPath: String?

    var id: String { launchPath ?? url.path }

    /// The path the emulator is started with.
    var startPath: String { launchPath ?? url.path }

    /// The extension of the compressed form of the game, nil when it is compressed already or
    /// cannot be compressed (installed titles, encrypted games, Artic).
    var compressedExtension: String? {
        guard !isInstalled, launchPath == nil, !isEncrypted else { return nil }
        switch url.pathExtension.lowercased() {
        case "cci", "3ds": return "zcci"
        case "cxi": return "zcxi"
        case "3dsx": return "z3dsx"
        default: return nil
        }
    }

    var programIDString: String {
        String(format: "%016llX", programID)
    }

    static func == (lhs: Game, rhs: Game) -> Bool { lhs.id == rhs.id }
    func hash(into hasher: inout Hasher) { hasher.combine(id) }
}

/// Scans the ROMs directory and keeps the list of games.
@MainActor
final class GameLibrary: ObservableObject {
    @Published private(set) var games: [Game] = []
    @Published private(set) var isScanning = false
    @Published private(set) var isImporting = false
    @Published var importError: String?
    /// File name and progress (0 to 1) of the CIA being installed.
    @Published private(set) var installName: String?
    @Published private(set) var installProgress: Double = 0
    /// What the last installation did, for a confirmation alert.
    @Published var installSummary: String?
    /// Set when a refresh was requested during a scan, so the files added meanwhile are not missed.
    private var needsRescan = false

    init() {
        refresh()
    }

    func refresh() {
        guard !isScanning else {
            needsRescan = true
            return
        }
        isScanning = true
        let directory = FileLocations.romsDirectory
        let installed = FileLocations.installedTitlesDirectory
        Task.detached(priority: .userInitiated) {
            let games = GameLibrary.scan(directory: directory, installedDirectory: installed)
            await MainActor.run {
                self.games = games
                self.isScanning = false
                if self.needsRescan {
                    self.needsRescan = false
                    self.refresh()
                }
            }
        }
    }

    /// Imports the provided files into the ROMs directory. Files that live in this app's temporary
    /// directory (the copies made by the document picker) are moved, everything else is copied.
    func importFiles(_ allURLs: [URL]) {
        // CIAs are installed into the emulated console instead of being copied.
        let cias = allURLs.filter {
            FileLocations.ciaExtensions.contains($0.pathExtension.lowercased())
        }
        if !cias.isEmpty {
            install(cias)
        }
        let urls = allURLs.filter {
            !FileLocations.ciaExtensions.contains($0.pathExtension.lowercased())
        }
        guard !urls.isEmpty else { return }
        isImporting = true
        let destination = FileLocations.romsDirectory
        Task.detached(priority: .userInitiated) {
            let failures = GameLibrary.importItems(urls, into: destination)
            await MainActor.run {
                self.isImporting = false
                if !failures.isEmpty {
                    self.importError = failures.joined(separator: "\n")
                }
                self.refresh()
            }
        }
    }

    /// Installs the CIAs one after the other, showing the progress.
    private func install(_ urls: [URL]) {
        guard installName == nil else {
            importError = "Another installation is still running."
            return
        }
        Task { @MainActor in
            var installed = 0
            var failures: [String] = []
            for url in urls {
                let name = url.lastPathComponent
                installName = name
                installProgress = 0
                let status: AZInstallStatus = await withCheckedContinuation { continuation in
                    AZEmulator.installCIA(
                        atPath: url.path,
                        progress: { fraction in
                            Task { @MainActor in self.installProgress = fraction }
                        },
                        completion: { continuation.resume(returning: $0) })
                }
                // The picker copied the file for this app, it is not needed any more.
                if GameLibrary.isInTemporaryDirectory(url) {
                    try? FileManager.default.removeItem(at: url)
                }
                if status == .success {
                    installed += 1
                } else {
                    failures.append("\(name): \(GameLibrary.describe(status))")
                }
            }
            installName = nil
            refresh()
            if !failures.isEmpty {
                importError = failures.joined(separator: "\n")
            }
            if installed > 0 {
                installSummary = installed == 1
                    ? "1 title was installed." : "\(installed) titles were installed."
            }
        }
    }

    nonisolated private static func describe(_ status: AZInstallStatus) -> String {
        switch status {
        case .success: return "Installed."
        case .failedToOpen: return "The file could not be opened."
        case .notFound: return "The file was not found."
        case .aborted: return "The installation was aborted. See the log for details."
        case .invalid: return "This is not a valid CIA."
        case .encrypted:
            return "This CIA is encrypted. Dump your titles from your own console in a decrypted "
                + "format."
        case .unknown: return "The installation failed."
        @unknown default: return "The installation failed."
        }
    }

    /// Returns a description of every file that could not be imported.
    nonisolated private static func importItems(_ urls: [URL], into destination: URL) -> [String] {
        let fileManager = FileManager.default
        do {
            try fileManager.createDirectory(at: destination, withIntermediateDirectories: true)
        } catch {
            return ["Could not create the games folder: \(error.localizedDescription)"]
        }

        var failures: [String] = []
        for url in urls {
            let name = url.lastPathComponent
            guard FileLocations.bootableExtensions.contains(url.pathExtension.lowercased()) else {
                failures.append("\(name): unsupported file type. Supported types: "
                    + FileLocations.bootableExtensions.sorted().joined(separator: ", "))
                continue
            }

            // Files provided by other apps may need security scoped access; the copies made by
            // the document picker do not, in which case this simply returns false.
            let accessing = url.startAccessingSecurityScopedResource()
            defer { if accessing { url.stopAccessingSecurityScopedResource() } }

            let target = destination.appendingPathComponent(name)
            guard target.standardizedFileURL != url.standardizedFileURL else { continue }
            do {
                if fileManager.fileExists(atPath: target.path) {
                    try fileManager.removeItem(at: target)
                }
                if isInTemporaryDirectory(url) {
                    do {
                        try fileManager.moveItem(at: url, to: target)
                        continue
                    } catch {
                        // Fall through to a plain copy.
                    }
                }
                try fileManager.copyItem(at: url, to: target)
            } catch {
                failures.append("\(name): \(error.localizedDescription)")
            }
        }
        return failures
    }

    nonisolated static func isInTemporaryDirectory(_ url: URL) -> Bool {
        let temporary = FileManager.default.temporaryDirectory.standardizedFileURL.resolvingSymlinksInPath()
        let file = url.standardizedFileURL.resolvingSymlinksInPath()
        return file.path.hasPrefix(temporary.path + "/")
    }

    func delete(_ game: Game) {
        if game.isInstalled {
            // .../title/00040000/<title id>/content/<id>.app: remove the whole title.
            let titleDirectory = game.url.deletingLastPathComponent().deletingLastPathComponent()
            try? FileManager.default.removeItem(at: titleDirectory)
        } else {
            try? FileManager.default.removeItem(at: game.url)
        }
        refresh()
    }

    /// Title types shown in the library: applications, demos and DSiWare. Updates and DLC are
    /// installed titles too, but are used by their game rather than booted.
    nonisolated private static let bootableTitleTypes: Set<UInt64> = [
        0x0004_0000, 0x0004_0002, 0x0004_8004,
    ]

    nonisolated private static func scan(directory: URL, installedDirectory: URL) -> [Game] {
        func games(in directory: URL, installed: Bool) -> [Game] {
            guard let enumerator = FileManager.default.enumerator(
                at: directory, includingPropertiesForKeys: [.isRegularFileKey],
                options: [.skipsHiddenFiles])
            else { return [] }

            var found: [Game] = []
            for case let url as URL in enumerator {
                guard FileLocations.bootableExtensions.contains(url.pathExtension.lowercased()),
                    let info = AZGameInfo(path: url.path)
                else { continue }
                if installed {
                    // The other contents of a title (manual, download play...) have no icon.
                    guard bootableTitleTypes.contains(info.programID >> 32), info.iconRGBA != nil
                    else { continue }
                }
                found.append(
                    Game(
                        url: url, title: info.title, publisher: info.publisher,
                        regions: info.regions, fileType: info.fileType,
                        programID: info.programID, isEncrypted: info.isEncrypted,
                        icon: makeIcon(info.iconRGBA), isInstalled: installed))
            }
            return found
        }

        var all = games(in: directory, installed: false)
        // A game kept both as the original and as its compressed copy is listed once, as the
        // original.
        var chosen: [UInt64: Game] = [:]
        for game in all {
            if let other = chosen[game.programID] {
                let otherCompressed = other.url.pathExtension.lowercased().hasPrefix("z")
                let gameCompressed = game.url.pathExtension.lowercased().hasPrefix("z")
                if otherCompressed && !gameCompressed {
                    chosen[game.programID] = game
                }
            } else {
                chosen[game.programID] = game
            }
        }
        all = all.filter { chosen[$0.programID]?.url == $0.url }
        // A title that is both imported and installed is listed once.
        let imported = Set(all.map(\.programID))
        all += games(in: installedDirectory, installed: true).filter {
            !imported.contains($0.programID)
        }
        return all.sorted { $0.title.localizedCaseInsensitiveCompare($1.title) == .orderedAscending }
    }

    nonisolated private static func makeIcon(_ data: Data?) -> UIImage? {
        guard let data, data.count == 48 * 48 * 4,
            let provider = CGDataProvider(data: data as CFData),
            let image = CGImage(
                width: 48, height: 48, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: 48 * 4,
                space: CGColorSpaceCreateDeviceRGB(),
                bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipLast.rawValue),
                provider: provider, decode: nil, shouldInterpolate: true, intent: .defaultIntent)
        else { return nil }
        return UIImage(cgImage: image)
    }
}
