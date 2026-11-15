// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

import AzaharCore
import Darwin
import UIKit

/// The debugger apps that can switch JIT on for this app. They run on their own, so all this app
/// can do is open them through their URL scheme and ask them to attach to this process.
enum JITDebugger: String, CaseIterable, Identifiable {
    case stikDebug
    case stikJIT
    case stosDebug
    case custom

    var id: String { rawValue }

    var title: String {
        switch self {
        case .stikDebug: return "StikDebug"
        case .stikJIT: return "StikJIT (older)"
        case .stosDebug: return "StosDebug"
        case .custom: return "Custom URL"
        }
    }

    /// The link that is opened. {pid} is this process, {bundle} the bundle identifier. These are
    /// best guesses of what the apps take: a different app or a changed format is what the custom
    /// choice is for.
    var template: String {
        switch self {
        case .stikDebug: return "stikdebug://enable-jit?pid={pid}"
        case .stikJIT: return "stikjit://enable-jit?pid={pid}"
        case .stosDebug: return "stosdebug://enable-jit?pid={pid}"
        case .custom: return ""
        }
    }
}

/// What the built-in JIT helper is doing, for the status message in the game list. A game that is
/// started while it is still preparing waits for it.
@MainActor
final class JITStatus: ObservableObject {
    static let shared = JITStatus()

    enum State: Equatable {
        case idle
        case preparing
        case enabled
        case failed(String)
    }

    @Published private(set) var state = State.idle
    @Published private(set) var waitingGame: String?
    private var waiting: [() -> Void] = []

    var isPreparing: Bool { state == .preparing }

    func begin() {
        state = .preparing
    }

    func finish(enabled: Bool, report: String) {
        state = enabled ? .enabled : .failed(report)
        let actions = waiting
        waiting = []
        waitingGame = nil
        actions.forEach { $0() }
        if enabled {
            // The message goes away by itself.
            Task { @MainActor in
                try? await Task.sleep(nanoseconds: 3_000_000_000)
                if self.state == .enabled { self.state = .idle }
            }
        }
    }

    func dismiss() {
        if case .failed = state { state = .idle }
    }

    /// Runs the action once the helper is done (a game that was started in the meantime).
    func whenDone(game: String, _ action: @escaping () -> Void) {
        waitingGame = game
        waiting.append(action)
    }
}

@MainActor
enum JITLauncher {
    private static var triedThisLaunch = false

    /// The link for a debugger, with this process filled in. nil when the template is empty or
    /// not a valid URL.
    static func url(for debugger: JITDebugger, custom: String) -> URL? {
        let template = debugger == .custom ? custom : debugger.template
        let bundle = Bundle.main.bundleIdentifier ?? ""
        let text = template
            .replacingOccurrences(of: "{pid}", with: String(getpid()))
            .replacingOccurrences(of: "{bundle}", with: bundle)
            .trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else { return nil }
        return URL(string: text)
    }

    /// Opens the debugger app, once per launch, when the setting is on and JIT is not there yet.
    static func requestIfNeeded(settings: AppSettings) {
        guard !triedThisLaunch, !AZEmulator.isJITAvailable else { return }
        if settings.builtInJIT {
            // The helper inside the app takes the place of a debugger app.
            triedThisLaunch = true
            enableWithHelper()
        } else if settings.autoEnableJIT {
            triedThisLaunch = true
            open(settings: settings)
        }
    }

    /// Runs the built-in helper and reports to JITStatus (the report is nil-safe to ignore).
    static func enableWithHelper(completion: ((String) -> Void)? = nil) {
        guard !JITStatus.shared.isPreparing else { return }
        JITStatus.shared.begin()
        AZJITHelper.enableJIT { enabled, report in
            JITStatus.shared.finish(enabled: enabled, report: report)
            completion?(report)
        }
    }

    /// Opens the debugger app now (the button in the settings).
    static func open(settings: AppSettings) {
        guard let url = url(for: settings.jitDebugger, custom: settings.jitCustomURL) else {
            return
        }
        UIApplication.shared.open(url)
    }
}
