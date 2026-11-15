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
        guard settings.autoEnableJIT, !triedThisLaunch, !AZEmulator.isJITAvailable else { return }
        triedThisLaunch = true
        open(settings: settings)
    }

    /// Opens the debugger app now (the button in the settings).
    static func open(settings: AppSettings) {
        guard let url = url(for: settings.jitDebugger, custom: settings.jitCustomURL) else {
            return
        }
        UIApplication.shared.open(url)
    }
}
