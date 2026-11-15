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
        .overlay(alignment: .top) { JITBanner() }
        .onAppear { ControllerNavigator.shared.start(session: session, settings: settings) }
        .onChange(of: scenePhase) { phase in
            if phase == .active, !session.isActive {
                JITLauncher.requestIfNeeded(settings: settings)
            }
        }
        .task {
            // The first activation happens before the phase changes: a short wait lets the
            // window come up before another app is opened over it.
            // The helper inside the app needs no window: it starts right away.
            if !settings.builtInJIT {
                try? await Task.sleep(nanoseconds: 800_000_000)
            }
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

/// The status of the built-in JIT helper as a toast over the app: preparing, enabled for a
/// moment, or what went wrong. It floats over whatever is showing and does not move or block it
/// (touches go through it, except on its OK button).
struct JITBanner: View {
    @ObservedObject private var status = JITStatus.shared

    var body: some View {
        Group {
            switch status.state {
            case .idle:
                EmptyView()
            case .preparing:
                banner {
                    ProgressView()
                    Text(status.waitingGame.map { "Preparing JIT… \($0) starts when it is ready" }
                         ?? "Preparing JIT…")
                }
            case .enabled:
                banner {
                    Image(systemName: "checkmark.circle.fill").foregroundStyle(.green)
                    Text("JIT is enabled")
                }
            case .failed(let report):
                banner {
                    Image(systemName: "exclamationmark.triangle.fill").foregroundStyle(.orange)
                    Text("JIT could not be enabled. Check Settings › General › CPU.")
                    Button("OK") { status.dismiss() }
                }
                .accessibilityHint(report)
            }
        }
        .animation(.easeInOut(duration: 0.25), value: status.state)
        .allowsHitTesting({ if case .failed = status.state { return true } else { return false } }())
    }

    private func banner<Content: View>(@ViewBuilder _ content: () -> Content) -> some View {
        HStack(spacing: 8) { content() }
            .font(.footnote.weight(.medium))
            .padding(.horizontal, 14)
            .padding(.vertical, 8)
            .background(.ultraThinMaterial, in: Capsule())
            .shadow(color: .black.opacity(0.15), radius: 8, y: 2)
            .padding(.top, 6)
            .transition(.move(edge: .top).combined(with: .opacity))
    }
}
