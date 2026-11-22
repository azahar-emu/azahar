// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

import AVFoundation
import AzaharCore
import Foundation
import UIKit

/// Drives the emulator from the UI: launching, pausing, stopping and statistics.
@MainActor
final class EmulationSession: ObservableObject {
    @Published private(set) var currentGame: Game?
    @Published private(set) var isPaused = false
    @Published private(set) var fps: Double = 0
    @Published private(set) var displayFps: Double = 0
    @Published private(set) var speed: Double = 0
    /// Progress of building the cached shaders while the game boots, nil when not building.
    @Published private(set) var shaderBuildProgress: Double?
    /// Average GPU time and time the emulation thread waited for the GPU, per frame, in ms.
    @Published private(set) var gpuMs: Double = 0
    @Published private(set) var stallMs: Double = 0
    @Published private(set) var paceJitterMs: Double = 0
    @Published private(set) var draws: Double = 0
    /// The software keyboard of the running game, while it waits for text.
    @Published var keyboardRequest: AZKeyboardRequest?
    /// The Mii selector of the running game, while it waits for a choice.
    @Published var miiRequest: AZMiiRequest?
    /// A message for the user that does not end the game.
    @Published var notice: SessionNotice?
    /// A short confirmation shown over the game for a moment.
    @Published private(set) var toast: String?
    @Published var showingError = false
    @Published private(set) var errorMessage: String?
    /// Complete report of an exception in the emulated program, when there is one.
    @Published private(set) var errorReport: String?

    /// Whether a physical game controller is connected while a game runs.
    @Published private(set) var hardwareControllerConnected = false
    /// The cursor of the touch stick, nil when there is none.
    @Published private(set) var touchCursor: TouchCursorState?

    let controllers = GameControllerInput()
    let motion = MotionInput()

    private var playStart: Date?
    private var pausedSince: Date?
    private var pausedTotal: TimeInterval = 0
    private var pendingGame: Game?
    private var pendingSettings: AppSettings?
    private var statsTimer: Timer?

    var isActive: Bool { currentGame != nil }

    init() {
        controllers.onConnectionChange = { [weak self] connected in
            self?.hardwareControllerConnected = connected
        }
        ControllerTouchHandler.shared.onTurbo = { [weak self] held in
            Task { @MainActor in
                self?.setFastForward(held)
            }
        }
        controllers.onTouchCursor = { [weak self] position, frame in
            Task { @MainActor in
                if let position {
                    let state = TouchCursorState(position: position, frame: frame)
                    if self?.touchCursor != state { self?.touchCursor = state }
                } else {
                    self?.touchCursor = nil
                }
            }
        }
        AZEmulator.shared.onMovieFinished = { [weak self] in
            self?.showToast("Movie finished")
        }
        controllers.onHotkey = { [weak self] hotkey, pressed in
            Task { @MainActor in
                self?.perform(hotkey, pressed: pressed)
            }
        }
        AZEmulator.shared.onStopped = { [weak self] error in
            Task { @MainActor in
                self?.handleStopped(error: error)
            }
        }
        AZEmulator.shared.onKeyboardRequest = { [weak self] request in
            Task { @MainActor in
                self?.keyboardRequest = request
            }
        }
        AZEmulator.shared.onMiiRequest = { [weak self] request in
            Task { @MainActor in
                self?.miiRequest = request
            }
        }
        AZEmulator.shared.onNotice = { [weak self] title, message in
            Task { @MainActor in
                self?.notice = SessionNotice(title: title, message: message)
            }
        }
    }

    // MARK: Save states and cheats

    func saveState(slot: Int) {
        if AZEmulator.shared.saveState(toSlot: slot) {
            showToast(slot == 0 ? "Saved to the quick slot" : "Saved to slot \(slot)")
        } else {
            notice = SessionNotice(title: "Save State", message: "The state could not be saved right now.")
        }
    }

    func loadState(slot: Int) {
        if AZEmulator.shared.loadState(fromSlot: slot) {
            showToast(slot == 0 ? "Loaded the quick slot" : "Loaded slot \(slot)")
        } else {
            notice = SessionNotice(title: "Load State", message: "The state could not be loaded right now.")
        }
    }

    // MARK: Amiibo, screenshots and speed

    @Published private(set) var isFastForwarding = false

    /// Fast forwards while held (the turbo button): on or off, without the toast.
    func setFastForward(_ on: Bool) {
        guard on != isFastForwarding else { return }
        isFastForwarding = on
        AZEmulator.shared.setFastForward(on)
    }

    func toggleFastForward() {
        isFastForwarding.toggle()
        AZEmulator.shared.setFastForward(isFastForwarding)
        showToast(isFastForwarding ? "Fast forward on" : "Fast forward off")
    }

    /// What a controller shortcut does.
    private func perform(_ hotkey: Hotkey, pressed: Bool) {
        guard isActive else { return }
        switch hotkey {
        case .menu:
            if pressed { NotificationCenter.default.post(name: .openEmulationMenu, object: nil) }
        case .fastForward:
            if UserDefaults.standard.bool(forKey: "fastForwardHold") {
                // Fast forward only while the button is held.
                if pressed != isFastForwarding { toggleFastForward() }
            } else if pressed {
                toggleFastForward()
            }
        case .screenshot:
            if pressed { takeScreenshot() }
        case .nextLayout:
            if pressed { NotificationCenter.default.post(name: .nextScreenLayout, object: nil) }
        case .touchTap:
            controllers.setTouchTap(pressed)
        }
    }

    func takeScreenshot() {
        AZEmulator.shared.takeScreenshot(toDirectory: FileLocations.screenshotsDirectory.path) {
            [weak self] path in
            Task { @MainActor in
                self?.showToast(path != nil
                    ? "Screenshot saved to Files › Neroli › screenshots"
                    : "The screenshot failed")
            }
        }
    }

    func loadAmiibo(at url: URL) {
        switch AZEmulator.shared.loadAmiibo(atPath: url.path) {
        case .loaded:
            showToast("Amiibo loaded")
        case .notSearching:
            notice = SessionNotice(
                title: "Amiibo",
                message: "The game is not asking for an amiibo right now. Open the amiibo screen in "
                    + "the game first, then load it.")
        case .tagInUse:
            notice = SessionNotice(
                title: "Amiibo", message: "An amiibo is already on the reader. Remove it first.")
        case .notRunning, .failed:
            notice = SessionNotice(title: "Amiibo", message: "The amiibo could not be loaded.")
        @unknown default:
            break
        }
    }

    func removeAmiibo() {
        AZEmulator.shared.removeAmiibo()
        showToast("Amiibo removed")
    }

    private var toastTask: Task<Void, Never>?

    func showToast(_ text: String) {
        toast = text
        toastTask?.cancel()
        toastTask = Task { @MainActor in
            try? await Task.sleep(nanoseconds: 2_000_000_000)
            if !Task.isCancelled {
                toast = nil
            }
        }
    }

    /// Shows the emulation view. The emulator itself starts once the Metal view is ready.
    func launch(_ game: Game, settings: AppSettings) {
        guard currentGame == nil else { return }
        if JITStatus.shared.isPreparing {
            // JIT is being prepared: the game starts when it is ready (with the interpreters it
            // would be far too slow, and the memory is being set up).
            JITStatus.shared.whenDone(game: game.title) { [weak self] in
                self?.launch(game, settings: settings)
            }
            return
        }
        if game.isEncrypted {
            present(error: "\(game.title) is encrypted. Please dump your titles from your own "
                + "console in a decrypted format.")
            return
        }
        pendingGame = game
        pendingSettings = settings
        currentGame = game
        isPaused = false
    }

    /// Called by the Metal view once its layer has a valid size.
    func renderSurfaceReady() {
        guard let game = pendingGame, let settings = pendingSettings else { return }
        pendingGame = nil
        pendingSettings = nil

        if !AZEmulator.isJITAvailable && (settings.cpuJIT || settings.shaderJIT) {
            NSLog("Neroli: JIT is not available, falling back to the interpreters")
        }
        AZEmulator.shared.apply(configuration: settings.makeConfiguration(for: game))
        // A game can use its own controller profile for as long as it runs.
        if let id = GameOverridesStore.shared.overrides(for: game).controllerProfile,
           let mapping = ControllerMappingStore.profileMapping(id: id) {
            ControllerMapping.current = mapping
        }
        do {
            try AZEmulator.shared.start(path: game.startPath)
        } catch {
            currentGame = nil
            present(error: error.localizedDescription)
            return
        }

        playStart = Date()
        pausedTotal = 0
        pausedSince = nil
        UIApplication.shared.isIdleTimerDisabled = true
        prepareAudioSession(recording: settings.useMicrophone)
        controllers.start()
        if settings.useMotion {
            motion.start()
        }
        statsTimer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in
            Task { @MainActor in
                guard let self else { return }
                self.fps = AZEmulator.shared.framesPerSecond
                self.displayFps = AZEmulator.shared.displayFramesPerSecond
                self.speed = AZEmulator.shared.emulationSpeed
                self.gpuMs = AZEmulator.shared.gpuMilliseconds
                self.stallMs = AZEmulator.shared.stallMilliseconds
                self.paceJitterMs = AZEmulator.shared.paceJitterMilliseconds
                self.draws = AZEmulator.shared.drawsPerFrame
                let progress = AZEmulator.shared.shaderBuildProgress
                self.shaderBuildProgress = progress >= 0 ? progress : nil
            }
        }
    }

    func togglePause() {
        // The time paused does not count as play time.
        if isPaused {
            if let since = pausedSince { pausedTotal += Date().timeIntervalSince(since) }
            pausedSince = nil
        } else {
            pausedSince = Date()
        }
        if isPaused {
            AZEmulator.shared.resume()
        } else {
            AZEmulator.shared.pause()
        }
        isPaused.toggle()
    }

    /// Starts the current game over (a movie that was set starts with it).
    func restartCurrentGame(settings: AppSettings) {
        guard let game = currentGame else { return }
        stop()
        Task { @MainActor in
            // The game view has to go away and come back for the new run to get its surface.
            try? await Task.sleep(nanoseconds: 700_000_000)
            launch(game, settings: settings)
        }
    }

    func stop() {
        pendingGame = nil
        AZEmulator.shared.stop()
        cleanUp()
    }

    /// Applies changed settings to the running game where possible.
    func apply(settings: AppSettings) {
        guard let game = currentGame else { return }
        AZEmulator.shared.apply(configuration: settings.makeConfiguration(for: game))
    }

    private func handleStopped(error: Error?) {
        cleanUp()
        if let error {
            let report = (error as NSError).userInfo["AZEmulatorErrorReport"] as? String
            present(error: error.localizedDescription, report: report)
        }
    }

    /// Playback only, or playback and recording when the game may use the microphone. Recording
    /// asks for permission the first time.
    private func prepareAudioSession(recording: Bool) {
        let audio = AVAudioSession.sharedInstance()
        guard recording else {
            try? audio.setCategory(.playback, mode: .default, options: [.mixWithOthers])
            try? audio.setActive(true)
            return
        }
        audio.requestRecordPermission { granted in
            Task { @MainActor in
                let category: AVAudioSession.Category = granted ? .playAndRecord : .playback
                var options: AVAudioSession.CategoryOptions = [.mixWithOthers]
                if granted { options.insert(.defaultToSpeaker) }
                try? audio.setCategory(category, mode: .default, options: options)
                try? audio.setActive(true)
            }
        }
    }

    private func cleanUp() {
        if let game = currentGame, let start = playStart {
            let now = Date()
            let pausedNow = pausedSince.map { now.timeIntervalSince($0) } ?? 0
            let played = now.timeIntervalSince(start) - pausedTotal - pausedNow
            GameStatsStore.shared.recordPlay(of: game.programID, seconds: played, at: now)
        }
        playStart = nil
        pausedSince = nil
        // Back to the mapping from the settings after a game with its own profile.
        ControllerMapping.current = ControllerMappingStore.load()
        prepareAudioSession(recording: false)
        statsTimer?.invalidate()
        statsTimer = nil
        controllers.stop()
        motion.stop()
        UIApplication.shared.isIdleTimerDisabled = false
        currentGame = nil
        isFastForwarding = false
        keyboardRequest = nil
        miiRequest = nil
        isPaused = false
        fps = 0
        displayFps = 0
        speed = 0
    }

    private func present(error: String, report: String? = nil) {
        errorMessage = error
        errorReport = report
        showingError = true
    }
}

/// A message shown to the user in an alert.
struct SessionNotice: Identifiable {
    let id = UUID()
    let title: String
    let message: String
}

extension AZMiiRequest: Identifiable {
    public var id: ObjectIdentifier { ObjectIdentifier(self) }
}

extension AZKeyboardRequest: Identifiable {
    public var id: ObjectIdentifier { ObjectIdentifier(self) }
}

extension Notification.Name {
    /// Asks the game view to show its menu (a controller shortcut).
    static let openEmulationMenu = Notification.Name("azahar.openEmulationMenu")
    /// Asks the game view to switch to the next saved screen layout (a controller shortcut).
    static let nextScreenLayout = Notification.Name("azahar.nextScreenLayout")
}

/// Where the cursor of the touch stick is: inside the bottom screen (0 to 1 in both directions) and
/// the frame of that screen in drawable pixels.
struct TouchCursorState: Equatable {
    var position: CGPoint
    var frame: CGRect
}
