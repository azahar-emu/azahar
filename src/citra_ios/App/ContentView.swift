// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

import SwiftUI
import UIKit

/// Root view: the library and settings tabs, swapped for the emulation view while a game runs.
struct ContentView: View {
    @EnvironmentObject private var session: EmulationSession
    @EnvironmentObject private var compressor: GameCompressor
    @EnvironmentObject private var settings: AppSettings
    @Environment(\.scenePhase) private var scenePhase

    var body: some View {
        Group {
            if session.isActive {
                EmulationView()
                    .transition(.opacity)
            } else {
                TabView {
                    GamesListView()
                        .tabItem { Label("Games", systemImage: "gamecontroller.fill") }
                    SettingsView()
                        .tabItem { Label("Settings", systemImage: "gearshape.fill") }
                }
                .lgTabBarMinimizes()
                .alert("Compression", isPresented: Binding(
                    get: { compressor.summary != nil },
                    set: { if !$0 { compressor.summary = nil } })
                ) {
                    Button("OK", role: .cancel) {}
                } message: {
                    Text(compressor.summary ?? "")
                }
                .transition(.opacity)
            }
        }
        .animation(.easeInOut(duration: 0.2), value: session.isActive)
        .onAppear { ControllerNavigator.shared.start(session: session, settings: settings) }
        .onChange(of: scenePhase) { phase in
            if phase == .active, !session.isActive {
                JITLauncher.requestIfNeeded(settings: settings)
            }
        }
        .task {
            // The first activation happens before the phase changes: a short wait lets the
            // window come up before another app is opened over it.
            try? await Task.sleep(nanoseconds: 800_000_000)
            JITLauncher.requestIfNeeded(settings: settings)
        }
        .alert("Emulation Error", isPresented: $session.showingError) {
            if let report = session.errorReport {
                Button("Copy Details") { UIPasteboard.general.string = report }
            }
            Button("OK", role: .cancel) {}
        } message: {
            Text(session.errorMessage ?? "Unknown error")
        }
    }
}
