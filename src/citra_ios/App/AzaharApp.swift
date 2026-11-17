// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

import AVFoundation
import AzaharCore
import SwiftUI

@main
struct AzaharApp: App {
    @UIApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    @StateObject private var settings = AppSettings()
    @StateObject private var library = GameLibrary()
    @StateObject private var session = EmulationSession()
    @StateObject private var controllerLayout = ControllerLayoutStore()
    @StateObject private var multiplayer = MultiplayerSession()
    @StateObject private var controllerMapping = ControllerMappingStore()
    @StateObject private var compressor = GameCompressor()

    init() {
        FileLocations.createDirectories()
        if FrameGenerator.isSupported {
            AZEmulator.setFrameGenerator(FrameGenerator.shared)
        }
        AZEmulator.shared.setUp(userDirectory: FileLocations.userDirectory.path)

        let audioSession = AVAudioSession.sharedInstance()
        try? audioSession.setCategory(.playback, mode: .default, options: [.mixWithOthers])
        try? audioSession.setActive(true)
    }

    var body: some Scene {
        WindowGroup {
            ContentView()
                // Follows the system, or is always light or dark (Settings > App Colors).
                .preferredColorScheme(settings.appearance == 1 ? .light
                                      : (settings.appearance == 2 ? .dark : nil))
                .environmentObject(settings)
                .environmentObject(library)
                .environmentObject(session)
                .environmentObject(controllerLayout)
                .environmentObject(multiplayer)
                .environmentObject(controllerMapping)
                .environmentObject(compressor)
                .onOpenURL { url in
                    library.importFiles([url])
                }
        }
    }
}
