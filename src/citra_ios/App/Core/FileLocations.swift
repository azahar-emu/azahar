// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

import Foundation
import UIKit

/// Locations of the app data. Everything lives in Documents so it is reachable from the Files app.
enum FileLocations {
    static var documents: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
    }

    /// Emulator user directory (sdmc, nand, sysdata, config, shaders, logs...).
    static let userDirectory: URL = resolveUserDirectory()

    /// The folder had the name of the upstream project, Azahar, before the app was called Neroli.
    /// It is moved to the new name the first time, so saves, system files and everything else
    /// come along. Should the move fail, the old folder stays in use rather than starting empty.
    private static func resolveUserDirectory() -> URL {
        let fileManager = FileManager.default
        let current = documents.appendingPathComponent("Neroli", isDirectory: true)
        let legacy = documents.appendingPathComponent("Azahar", isDirectory: true)
        var isDirectory: ObjCBool = false
        guard fileManager.fileExists(atPath: legacy.path, isDirectory: &isDirectory),
              isDirectory.boolValue
        else { return current }

        if !fileManager.fileExists(atPath: current.path) {
            do {
                try fileManager.moveItem(at: legacy, to: current)
                NSLog("Neroli: moved the Azahar folder to Neroli")
                return current
            } catch {
                NSLog("Neroli: could not move the Azahar folder (%@), still using it",
                      error.localizedDescription)
                return legacy
            }
        }
        // Both exist (a Neroli folder made by hand, or an earlier move cut short): what is not in
        // the Neroli folder yet is moved into it, and the Azahar folder goes once it is empty.
        let items = (try? fileManager.contentsOfDirectory(atPath: legacy.path)) ?? []
        for name in items {
            let target = current.appendingPathComponent(name)
            if !fileManager.fileExists(atPath: target.path) {
                try? fileManager.moveItem(at: legacy.appendingPathComponent(name), to: target)
            }
        }
        if ((try? fileManager.contentsOfDirectory(atPath: legacy.path)) ?? []).isEmpty {
            try? fileManager.removeItem(at: legacy)
        }
        return current
    }

    /// Shows a folder in the Files app (inside this app's Documents, which is where the Files app
    /// lists the app's data). Falls back to opening the Files app.
    @MainActor
    static func showInFiles(_ url: URL) {
        let path = url.path.addingPercentEncoding(withAllowedCharacters: .urlPathAllowed) ?? url.path
        if let target = URL(string: "shareddocuments://" + path) {
            UIApplication.shared.open(target) { opened in
                if !opened, let files = URL(string: "shareddocuments://") {
                    UIApplication.shared.open(files)
                }
            }
        }
    }

    /// 3GX plugins of the plugin loader (the virtual SD card of the console).
    static var pluginsDirectory: URL {
        sdmcDirectory.appendingPathComponent("luma/plugins", isDirectory: true)
    }

    /// Metal post-processing shaders written by the user.
    static var postShadersDirectory: URL {
        userDirectory.appendingPathComponent("shaders/post", isDirectory: true)
    }

    /// Recorded movies (.ctm).
    static var moviesDirectory: URL {
        userDirectory.appendingPathComponent("movies", isDirectory: true)
    }

    /// Imported games.
    static var romsDirectory: URL {
        documents.appendingPathComponent("ROMs", isDirectory: true)
    }

    /// File extensions the emulator can boot.
    static let bootableExtensions: Set<String> = [
        "3ds", "cci", "cxi", "3dsx", "app", "elf", "axf", "zcci", "zcxi", "z3dsx",
        // Bundles: the game with its updates and DLC in one file, used without installing them.
        "bcci", "bcxi",
    ]

    /// Metal shader caches of every game (see the Metal PipelineCache).
    static var shaderCacheDirectory: URL {
        userDirectory.appendingPathComponent("shaders/metal", isDirectory: true)
    }

    /// Deletes the shader caches. Returns true so it can drive a confirmation alert.
    @discardableResult
    static func clearShaderCache() -> Bool {
        try? FileManager.default.removeItem(at: shaderCacheDirectory)
        return true
    }

    /// The amiibo files the app keeps. A game saves what it writes to an amiibo into its file.
    static var amiiboDirectory: URL {
        userDirectory.appendingPathComponent("amiibo", isDirectory: true)
    }

    /// Screenshots taken in a game.
    static var screenshotsDirectory: URL {
        userDirectory.appendingPathComponent("screenshots", isDirectory: true)
    }

    /// Installable titles (games, updates and DLC) the emulator installs instead of importing.
    /// A .bcia bundle holds several CIAs (a game with its updates and DLC), all installed.
    static let ciaExtensions: Set<String> = ["cia", "zcia", "bcia"]

    /// Where the installed titles live.
    static var installedTitlesDirectory: URL {
        sdmcDirectory.appendingPathComponent("Nintendo 3DS", isDirectory: true)
    }

    /// The virtual SD card of the console: in the storage folder when one is chosen.
    static var sdmcDirectory: URL {
        if let custom = CustomStorage.currentURL,
           FileManager.default.isWritableFile(atPath: custom.path) {
            return custom.appendingPathComponent("sdmc", isDirectory: true)
        }
        return userDirectory.appendingPathComponent("sdmc", isDirectory: true)
    }

    static func createDirectories() {
        for url in [userDirectory, romsDirectory] {
            try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        }
    }
}

/// The post-processing shaders the user can pick.
enum PostShaders {
    /// A shader to start from, written to the folder the first time it is looked at.
    private static let example = """
        // A post-processing shader for Neroli. Define post_effect: it gets the position in the
        // screen (0 to 1), the picture of the screen, a sampler and the size of the window
        // (width, height, 1/width, 1/height), and returns the color of that point.
        float4 post_effect(float2 uv, texture2d<float> screen, sampler s, float4 resolution) {
            float3 color = screen.sample(s, uv).rgb;
            float gray = dot(color, float3(0.299f, 0.587f, 0.114f));
            // A little more color than the original.
            return float4(mix(float3(gray), color, 1.25f), 1.0f);
        }

        """

    /// The names (without .metal) of the shader files, after making the folder with the example.
    static func availableNames() -> [String] {
        let directory = FileLocations.postShadersDirectory
        let fileManager = FileManager.default
        if !fileManager.fileExists(atPath: directory.path) {
            try? fileManager.createDirectory(at: directory, withIntermediateDirectories: true)
            try? example.write(to: directory.appendingPathComponent("Example.metal"),
                               atomically: true, encoding: .utf8)
        }
        let files = (try? fileManager.contentsOfDirectory(at: directory,
                                                          includingPropertiesForKeys: nil)) ?? []
        return files.filter { $0.pathExtension.lowercased() == "metal" }
            .map { $0.deletingPathExtension().lastPathComponent }
            .sorted()
    }
}
