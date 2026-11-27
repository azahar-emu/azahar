// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

import AzaharCore
import SwiftUI

/// Full screen emulation: the rendered 3DS screens, the on-screen controller and a menu.
struct EmulationView: View {
    @EnvironmentObject private var session: EmulationSession
    @EnvironmentObject private var settings: AppSettings
    @EnvironmentObject private var controllerLayout: ControllerLayoutStore
    @Environment(\.scenePhase) private var scenePhase

    @State private var showingMenu = false
    @State private var controllerVisible = true
    /// Keeps the on-screen controller up although a physical one is connected (until it changes).
    @State private var showDespiteHardware = false
    @State private var pausedForBackground = false
    @State private var editingLayout = false
    @State private var showingSaveStates = false
    @State private var showingLoadStates = false
    @State private var showingCheats = false
    @State private var showingAmiibo = false
    @State private var showingMultiplayer = false
    @State private var showingMovies = false
    @State private var showingGameSettings = false
    @State private var savingLayout = false
    @State private var layoutName = ""
    @State private var editingScreens = false
    @State private var resumeAfterEditing = false
    /// The game was paused because the menu came up, and runs again once it is gone.
    @State private var pausedForMenu = false
    /// The last touch on the screen, for fading the menu buttons out while a controller is used.
    @State private var lastTouch = Date()
    @State private var buttonsFaded = false
    @State private var resumeTask: Task<Void, Never>?

    /// Seconds without a touch before the menu buttons fade out (controller connected).
    private static let fadeDelay: TimeInterval = 8

    private var observed: some View {
        stage
        .onChange(of: session.hardwareControllerConnected) { _ in showDespiteHardware = false }
        .onReceive(NotificationCenter.default.publisher(for: .azaharScreenTouched)) { _ in
            lastTouch = Date()
            buttonsFaded = false
        }
        // A loop of its own: a timer publisher made here would be made again with every redraw
        // (the speed numbers redraw this view twice a second), and a one second timer that is
        // restarted twice a second never fires.
        .task {
            while !Task.isCancelled {
                try? await Task.sleep(nanoseconds: 1_000_000_000)
                updateButtonFade()
            }
        }
        .onChange(of: settings.refreshRate) { _ in
            RefreshRateController.shared.apply(effectiveRefreshRate)
        }
        .statusBarHidden()
        .persistentSystemOverlays(.hidden)
        // Without this iOS holds back touches near the screen edges until it knows they are not
        // a system swipe (Home, Control Center, Notification Center), then delivers the press
        // and the release together, so the game never sees a quick tap on the controller.
        .defersSystemGestures(on: .all)
    }

    private var presented: some View {
        observed
        .sheet(isPresented: $showingSaveStates) {
            SaveStatesView(mode: .save)
        }
        .sheet(isPresented: $showingLoadStates) {
            SaveStatesView(mode: .load)
        }
        .sheet(isPresented: $showingCheats) {
            CheatsView()
        }
        .sheet(isPresented: $showingAmiibo) {
            AmiiboView()
        }
        .sheet(isPresented: $showingMultiplayer) {
            MultiplayerView()
        }
        .sheet(isPresented: $showingMovies) {
            if let game = session.currentGame {
                MoviesView(game: game)
            }
        }
        .sheet(isPresented: $showingGameSettings) {
            if let game = session.currentGame {
                NavigationStack {
                    GameSettingsView(game: game) { session.apply(settings: settings) }
                        .toolbar {
                            ToolbarItem(placement: .confirmationAction) {
                                Button("Done") { showingGameSettings = false }
                            }
                        }
                }
            }
        }
        .sheet(item: $session.miiRequest) { request in
            MiiSelectorView(request: request) { session.miiRequest = nil }
        }
        .sheet(item: $session.keyboardRequest) { request in
            KeyboardPromptView(request: request) { session.keyboardRequest = nil }
        }
        .alert(session.notice?.title ?? "", isPresented: Binding(
            get: { session.notice != nil },
            set: { if !$0 { session.notice = nil } })
        ) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(session.notice?.message ?? "")
        }
    }

    /// The game's own Display Refresh Rate if it has one, otherwise the global setting.
    private var effectiveRefreshRate: RefreshRateOption {
        if let game = session.currentGame,
           let raw = GameOverridesStore.shared.overrides(for: game).refreshRate,
           let option = RefreshRateOption(rawValue: raw) {
            return option
        }
        return settings.refreshRate
    }

    var body: some View {
        presented
        .onChange(of: menuVisible) { visible in
            menuVisibilityChanged(visible)
        }
        .onChange(of: editingLayout) { editing in
            if !editing && resumeAfterEditing {
                resumeAfterEditing = false
                if session.isPaused {
                    session.togglePause()
                }
            }
        }
        .onChange(of: scenePhase) { phase in
            switch phase {
            case .background, .inactive:
                if !session.isPaused {
                    session.togglePause()
                    pausedForBackground = true
                }
            case .active:
                if pausedForBackground && session.isPaused {
                    session.togglePause()
                }
                pausedForBackground = false
            @unknown default:
                break
            }
        }
    }

    @ViewBuilder private var hudLayer: some View {
        // Placed with the controller layout, so it can be moved in the layout editor (which
        // draws it itself while editing).
        if !editingLayout && !editingScreens {
            GeometryReader { geometry in
                let metrics = ControllerMetrics(size: geometry.size)
                let placement = controllerLayout.placement(for: .menuButton,
                                                           portrait: metrics.portrait)
                let frame = metrics.frame(of: .menuButton, placement: placement)
                let swapPlacement = controllerLayout.placement(for: .swapButton,
                                                               portrait: metrics.portrait)
                GameHUD(isOpen: $showingMenu,
                        title: session.currentGame?.title ?? "Emulation",
                        actions: hudActions,
                        menuFrame: frame,
                        menuScale: CGFloat(placement.scale),
                        swapFrame: settings.showSwapButton && !swapPlacement.hidden
                            ? metrics.frame(of: .swapButton, placement: swapPlacement) : nil,
                        swapScale: CGFloat(swapPlacement.scale),
                        glass: ControlGlassStyle(enabled: settings.glassControls,
                                                 tint: settings.glassControlTint),
                        concealed: buttonsFaded,
                        onSwap: {
                            settings.swapScreens.toggle()
                            session.apply(settings: settings)
                        })
            }
        }
    }

    @ViewBuilder private var statusLayers: some View {
        // The cursor of the touch stick, over the bottom screen.
        if let cursor = session.touchCursor, !cursor.frame.isEmpty {
            GeometryReader { _ in
                let scale = UIScreen.main.scale
                Image(systemName: "circle.circle")
                    .font(.system(size: 22, weight: .bold))
                    .foregroundStyle(.white)
                    .shadow(color: .black, radius: 2)
                    .position(
                        x: (cursor.frame.minX + cursor.position.x * cursor.frame.width) / scale,
                        y: (cursor.frame.minY + cursor.position.y * cursor.frame.height) / scale)
            }
            .ignoresSafeArea(settings.respectNotch ? [] : .all)
            .allowsHitTesting(false)
        }

        if let progress = session.gamePreloadProgress ?? session.shaderBuildProgress {
            VStack(spacing: 8) {
                Text(session.gamePreloadProgress != nil ? "Loading Game into Memory" : "Building Shaders")
                    .font(.headline)
                ProgressView(value: progress)
                    .frame(width: 180)
                Text("\(Int((progress * 100).rounded()))%")
                    .font(.caption.monospacedDigit())
                    .foregroundStyle(.secondary)
            }
            .padding(16)
            .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 14))
            .allowsHitTesting(false)
        }

        if let toast = session.toast {
            VStack {
                Spacer()
                Text(toast)
                    .font(.subheadline.weight(.semibold))
                    .multilineTextAlignment(.center)
                    .frame(maxWidth: 440)
                    .fixedSize(horizontal: false, vertical: true)
                    .padding(.horizontal, 16)
                    .padding(.vertical, 10)
                    // A capsule for a line, rounded corners for a longer message that wraps.
                    .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 20, style: .continuous))
                    .padding(.horizontal, 24)
                    .padding(.bottom, 24)
            }
            .allowsHitTesting(false)
            .transition(.opacity)
        }

        // Not over the menu, which pauses the game itself: only when it was left paused.
        if session.isPaused && !editingLayout && !menuVisible {
            Text("Paused")
                .font(.headline)
                .padding(.horizontal, 16)
                .padding(.vertical, 8)
                .background(.ultraThinMaterial, in: Capsule())
        }
    }

    private var stage: some View {
        ZStack {
            Color.black.ignoresSafeArea()

            // By default stays inside the safe area so the screens do not go under the notch or
            // the Dynamic Island. The black background behind it still fills the whole display.
            MetalView {
                session.renderSurfaceReady()
            }
            .ignoresSafeArea(settings.respectNotch ? [] : .all)

            if editingScreens {
                ScreenLayoutEditor(isEditing: $editingScreens)
                    .ignoresSafeArea(settings.respectNotch ? [] : .all)
            }

            if controllerShown && !editingScreens {
                ControllerOverlay(opacity: settings.controllerOpacity,
                                  haptics: settings.hapticFeedback,
                                  glass: ControlGlassStyle(enabled: settings.glassControls,
                                                           tint: settings.glassControlTint),
                                  showHomeButton: settings.showHomeButton,
                                  isEditing: $editingLayout)
            }

            VStack {
                HStack(alignment: .top) {
                    if settings.showPerformanceOverlay {
                        PerformanceOverlay(fps: session.fps, displayFps: session.displayFps,
                                           speed: session.speed, gpuMs: session.gpuMs,
                                           stallMs: session.stallMs, draws: session.draws,
                                           paceJitterMs: session.paceJitterMs)
                    }
                    Spacer()
                }
                Spacer()
            }
            .padding(.horizontal, 8)

            hudLayer

            statusLayers
        }
        .onAppear {
            controllerLayout.activate(gameID: session.currentGame?.programIDString)
            RefreshRateController.shared.apply(effectiveRefreshRate)
        }
        .onDisappear {
            controllerLayout.activate(gameID: nil)
            RefreshRateController.shared.apply(.automatic)
        }
        .onReceive(NotificationCenter.default.publisher(for: .openEmulationMenu)) { _ in
            showingMenu = true
        }
        .onReceive(NotificationCenter.default.publisher(for: .nextScreenLayout)) { _ in
            cycleLayout()
        }
        .alert("Save Screen Layout", isPresented: $savingLayout) {
            TextField("Name", text: $layoutName)
            Button("Save") {
                settings.saveLayoutPreset(named: layoutName)
                session.showToast("Layout saved")
            }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("Saves the screens as they are now. A layout with the same name is replaced.")
        }
    }
}

extension EmulationView {
    /// The entries of the control center, the most used first.
    fileprivate var hudActions: [HUDAction] {
        var actions: [HUDAction] = []
        func add(_ id: String, _ title: String, _ image: String, isOn: Bool = false,
                 isDestructive: Bool = false, _ action: @escaping () -> Void) {
            actions.append(HUDAction(id: id, title: title, systemImage: image, isOn: isOn,
                                     isDestructive: isDestructive, action: action))
        }
        if pausedForMenu {
            // The game is paused while the menu is up. Leaving it paused afterwards is a choice.
            add("keepPaused", "Keep Paused", "pause.circle") { pausedForMenu = false }
        } else {
            add("pause", session.isPaused ? "Resume" : "Pause",
                session.isPaused ? "play.fill" : "pause.fill") {
                pausedForMenu = false
                session.togglePause()
            }
        }
        add("save", "Save State", "square.and.arrow.down") { showingSaveStates = true }
        add("load", "Load State", "square.and.arrow.up") { showingLoadStates = true }
        add("fastForward", session.isFastForwarding ? "Normal Speed" : "Fast Forward",
            "forward.fill", isOn: session.isFastForwarding) { session.toggleFastForward() }
        add("swap", "Swap Screens", "rectangle.2.swap", isOn: settings.swapScreens) {
            settings.swapScreens.toggle()
            session.apply(settings: settings)
        }
        add("nextLayout", "Next Layout", "rectangle.3.group") { cycleLayout() }
        add("cheats", "Cheats", "wand.and.stars") { showingCheats = true }
        add("screenshot", "Screenshot", "camera") { session.takeScreenshot() }
        add("gameSettings", "Game Settings", "slider.horizontal.3") { showingGameSettings = true }
        add("controller", controllerShown ? "Hide Controller" : "Show Controller",
            "gamecontroller") {
            if controllerShown {
                controllerVisible = false
            } else {
                controllerVisible = true
                showDespiteHardware = true
            }
        }
        if settings.showController {
            add("editController", "Edit Controller", "hand.draw") { beginEditingLayout() }
        }
        add("editScreens", "Edit Screens", "rectangle.dashed") { beginEditingScreens() }
        add("saveLayout", "Save Layout", "plus.rectangle.on.rectangle") {
            layoutName = ""
            savingLayout = true
        }
        add("amiibo", "Amiibo", "figure.stand") { showingAmiibo = true }
        add("multiplayer", "Multiplayer", "person.2") { showingMultiplayer = true }
        add("movies", "Movies", "film") { showingMovies = true }
        add("stop", "Stop", "stop.circle", isDestructive: true) { session.stop() }
        return actions
    }

    /// Switches to the next saved screen layout.
    fileprivate func cycleLayout() {
        guard let preset = settings.cycleLayout() else {
            session.showToast("No saved layouts: use Save Screen Layout")
            return
        }
        session.apply(settings: settings)
        AZEmulator.shared.refreshLayout()
        session.showToast(preset.name)
    }

    /// Whether the menu, or something it opened that the game should wait for, is on screen. The
    /// multiplayer room is not part of it: the others in the room keep playing.
    fileprivate var menuVisible: Bool {
        showingMenu || showingSaveStates || showingLoadStates || showingCheats || showingAmiibo
            || showingMovies || showingGameSettings || editingLayout
    }

    /// Pauses the game while the menu is up and runs it again when it is gone.
    /// Fades the menu and swap buttons out after a few seconds without a touch while a controller
    /// is used, and back in when it is no longer true.
    fileprivate func updateButtonFade() {
        let idle = Date().timeIntervalSince(lastTouch) > Self.fadeDelay
        let fade = settings.autoHideMenuButtons && session.hardwareControllerConnected
            && !showingMenu && idle
        if fade != buttonsFaded { buttonsFaded = fade }
    }

    fileprivate func menuVisibilityChanged(_ visible: Bool) {
        resumeTask?.cancel()
        resumeTask = nil
        if visible {
            // A game that was paused already stays paused afterwards.
            if !session.isPaused {
                session.togglePause()
                pausedForMenu = true
            }
            return
        }
        guard pausedForMenu else { return }
        // The menu hands over to the screen of the choice (save states, cheats) in a moment where
        // neither shows, wait a little so the game does not run in between.
        resumeTask = Task { @MainActor in
            try? await Task.sleep(nanoseconds: 300_000_000)
            guard !Task.isCancelled, !menuVisible, pausedForMenu else { return }
            pausedForMenu = false
            if session.isPaused {
                session.togglePause()
            }
        }
    }

    /// Whether the on-screen controller is drawn: it is hidden by the menu, and while a physical
    /// controller is connected (when the setting says so).
    fileprivate var controllerShown: Bool {
        guard settings.showController && controllerVisible else { return false }
        let hiddenByHardware = settings.hideControllerWithHardware
            && session.hardwareControllerConnected && !showDespiteHardware
        return !hiddenByHardware
    }

    /// Switches the current orientation to the custom layout and lets the screens be moved.
    fileprivate func beginEditingScreens() {
        let portrait = UIScreen.main.bounds.height > UIScreen.main.bounds.width
        // Coming from another layout (Fold, Top Full Width, ...), the editor starts from where the
        // screens are now instead of an older custom layout, so they can be nudged from there.
        let wasCustom = portrait ? settings.portraitLayout == .custom : settings.layout == .custom
        if !wasCustom, let current = AZEmulator.shared.currentScreenFractions {
            if portrait {
                settings.customLayoutPortrait = current.map(\.doubleValue)
            } else {
                settings.customLayoutLandscape = current.map(\.doubleValue)
            }
        }
        if portrait {
            settings.portraitLayout = .custom
        } else {
            settings.layout = .custom
        }
        session.apply(settings: settings)
        AZEmulator.shared.refreshLayout()
        editingScreens = true
    }

    /// Pauses the game and lets the controller be moved and resized.
    fileprivate func beginEditingLayout() {
        controllerVisible = true
        showDespiteHardware = true
        if !session.isPaused {
            session.togglePause()
            resumeAfterEditing = true
        }
        editingLayout = true
    }
}

private struct PerformanceOverlay: View {
    let fps: Double
    let displayFps: Double
    let speed: Double
    let gpuMs: Double
    let stallMs: Double
    let draws: Double
    let paceJitterMs: Double

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            // What reaches the screen, like the Metal HUD counts it. The frames the game draws
            // itself are shown next to it when frame generation makes the two differ.
            if displayFps > fps + 2 {
                Text(String(format: "FPS %.0f (game %.0f)", displayFps, fps))
            } else {
                Text(String(format: "FPS %.0f", max(displayFps, fps)))
            }
            Text(String(format: "Speed %.0f%%", speed))
            Text(String(format: "GPU %.1f ms", gpuMs))
            Text(String(format: "Wait %.1f ms", stallMs))
            Text(String(format: "Pace ±%.1f ms", paceJitterMs))
            Text(String(format: "Draws %.0f", draws))
        }
        .font(.system(size: 11, weight: .semibold, design: .monospaced))
        .foregroundStyle(.white)
        .padding(6)
        .background(Color.black.opacity(0.45), in: RoundedRectangle(cornerRadius: 6))
        .padding(.top, 8)
        .allowsHitTesting(false)
    }
}
