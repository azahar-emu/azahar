// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

import AzaharCore
import Foundation
import UIKit

/// Settings a game can override. A nil value means the game uses the global setting.
struct GameOverrides: Codable, Equatable {
    var resolutionFactor: Int?
    var textureFilter: Int?
    var upscaling: Int?
    var upscaleFactorTenths: Int?
    var sharpPixels: Bool?
    var sharpness: Int?
    var sharpenMethod: Int?
    var scaleFilter: Int?
    var gridStrength: Int?
    var saturation: Int?
    var contrast: Int?
    var brightness: Int?
    var anisotropy: Int?
    var fastShaderMath: Bool?
    var fastLighting: Bool?
    var fastLightingLookups: Bool?
    var fastColor: Bool?
    var fastMathFunctions: Bool?
    var halfInterpolants: Bool?
    var untrackedBuffers: Bool?
    var vertexReuse: Bool?
    var vertexFastFunctions: Bool?
    var vertexFastMath: Bool?
    var cpuJIT: Bool?
    var simulateGPUTimings: Bool?
    var gpuTimingPercent: Int?
    var gpuTimingMinMs: Int? // tenths of a ms; older saves held whole ms (under 100)
    var texturePool: Bool?
    var realtimeAudio: Bool?
    var fastDepthReadback: Bool?
    var asyncShaders: Bool?
    var deterministicAsync: Bool?
    var holdFramesForShaders: Bool?
    var frameGeneration: Int?
    var frameGenerationFast: Bool?
    var frameGenerationSize: Int?
    var frameGenerationTopOnly: Bool?
    var frameGenerationPause: Bool?
    var frameGenerationBuffer: Bool?
    var frameGenerationDisplayPaced: Bool?
    var frameGenerationMetalFX: Bool?
    var syncSpeedToDisplay: Bool?
    var pacingBufferMs: Int?
    var displaySync: Bool?
    /// RefreshRateOption raw value (0 Automatic, 60, 120).
    var refreshRate: Int?
    var frameCap30: Bool?
    var steadyThirty: Bool?
    var frameCapHoldsGame: Bool?
    var shaderWaitMs: Int?
    var disableRightEyeRender: Bool?
    var integerScaling: Bool?
    var secondaryDisplayLayout: Int?
    var skipDuplicateFrames: Bool?
    /// Id of the controller profile this game uses, nil for the mapping being edited.
    var controllerProfile: String?
    var customTextures: Bool?
    var preloadTextures: Bool?
    var hiddenSurfaceRemoval: Bool?
    var drawOptimizations: Bool?
    var fastWBufferDepth: Bool?
    var antiAliasing: Int?
    var waitForGPU: Bool?
    var textureCompression: Int?
    var hardwareShaders: Bool?
    var cpuClockPercentage: Int?
    var stereoMode: Int?
    var region: Int?
    var isNew3DS: Bool?
    var aspectRatio: Int?
    var textureSampling: Int?
    var monoEye: Int?
    var audioEmulation: Int?
    var lleApplets: Bool?
    var pluginLoader: Bool?
    var onlineModules: Bool?
    var regionFreePatch: Bool?
    var postEffect: Int?
    var fastForwardSpeed: Int?
    /// What the game does about each workaround of the core (key of GameHack): 1 off, 2 on. A
    /// workaround that is not here follows the built-in list.
    var hacks: [String: Int]?
    /// 0 runs without a speed limit, other values limit the speed to that percentage.
    var speedLimit: Int?

    var isEmpty: Bool { self == GameOverrides() }

    /// Replaces the fields of the configuration the game overrides.
    func apply(to config: AZConfiguration) {
        if let resolutionFactor { config.resolutionFactor = resolutionFactor }
        if let textureFilter { config.textureFilter = textureFilter }
        if let upscaling { config.upscaling = upscaling }
        if let upscaleFactorTenths { config.upscaleFactorTenths = upscaleFactorTenths }
        if let sharpPixels { config.sharpPixels = sharpPixels }
        if let sharpness { config.sharpness = sharpness }
        if let sharpenMethod { config.sharpenMethod = sharpenMethod }
        if let scaleFilter { config.scaleFilter = scaleFilter }
        if let gridStrength { config.gridStrength = gridStrength }
        if let saturation { config.saturation = saturation }
        if let contrast { config.contrast = contrast }
        if let brightness { config.brightness = brightness }
        if let anisotropy { config.anisotropy = anisotropy }
        if let fastShaderMath { config.fastShaderMath = fastShaderMath }
        if let fastLighting { config.fastLighting = fastLighting }
        if let fastLightingLookups { config.fastLightingLookups = fastLightingLookups }
        if let fastColor { config.fastColor = fastColor }
        if let fastMathFunctions { config.fastMathFunctions = fastMathFunctions }
        if let halfInterpolants { config.halfInterpolants = halfInterpolants }
        if let untrackedBuffers { config.untrackedBuffers = untrackedBuffers }
        if let vertexReuse { config.vertexReuse = vertexReuse }
        if let vertexFastFunctions { config.vertexFastFunctions = vertexFastFunctions }
        if let vertexFastMath { config.vertexFastMath = vertexFastMath }
        if let cpuJIT { config.useCPUJIT = cpuJIT }
        if let simulateGPUTimings { config.simulateGPUTimings = simulateGPUTimings }
        if let gpuTimingPercent { config.gpuTimingPercent = gpuTimingPercent }
        if let gpuTimingMinMs { config.gpuTimingMinMs = gpuTimingMinMs > 0 && gpuTimingMinMs < 100 ? gpuTimingMinMs * 10 : gpuTimingMinMs }
        if let texturePool { config.texturePool = texturePool }
        if let realtimeAudio { config.realtimeAudio = realtimeAudio }
        if let fastDepthReadback { config.fastDepthReadback = fastDepthReadback }
        if let asyncShaders { config.asyncShaderCompilation = asyncShaders }
        if let deterministicAsync { config.deterministicAsync = deterministicAsync }
        if let holdFramesForShaders { config.holdFramesForShaders = holdFramesForShaders }
        if let frameGeneration {
            config.frameGeneration = FrameGenerator.isSupported ? min(frameGeneration, 2) : 0
        }
        if let frameGenerationFast { config.frameGenerationSize = frameGenerationFast ? 2 : 1 }
        if let frameGenerationSize { config.frameGenerationSize = frameGenerationSize }
        if let frameGenerationTopOnly { config.frameGenerationTopOnly = frameGenerationTopOnly }
        if let frameGenerationPause { config.frameGenerationPause = frameGenerationPause }
        if let frameGenerationBuffer { config.frameGenerationBuffer = frameGenerationBuffer }
        if let frameGenerationMetalFX { config.frameGenerationMetalFX = frameGenerationMetalFX }
        if let syncSpeedToDisplay { config.syncSpeedToDisplay = syncSpeedToDisplay }
        if let pacingBufferMs { config.pacingBufferMs = pacingBufferMs }
        if let displaySync { config.displaySync = displaySync }
        if let refreshRate {
            let screenMax = UIScreen.main.maximumFramesPerSecond
            config.displayRefreshRate = refreshRate == 0 ? screenMax : min(refreshRate, screenMax)
        }
        if let frameCap30 { config.frameCap30 = frameCap30 }
        if let steadyThirty { config.steadyThirty = steadyThirty }
        if let frameCapHoldsGame { config.frameCapHoldsGame = frameCapHoldsGame }
        if let frameGenerationDisplayPaced {
            config.frameGenerationDisplayPaced = frameGenerationDisplayPaced
        }
        if let shaderWaitMs { config.shaderWaitMs = shaderWaitMs }
        if let disableRightEyeRender { config.disableRightEyeRender = disableRightEyeRender }
        if let integerScaling { config.integerScaling = integerScaling }
        if let secondaryDisplayLayout { config.secondaryDisplayLayout = secondaryDisplayLayout }
        if let skipDuplicateFrames { config.skipDuplicateFrames = skipDuplicateFrames }
        if let customTextures { config.customTextures = customTextures }
        if let preloadTextures { config.preloadTextures = preloadTextures }
        if let hiddenSurfaceRemoval { config.hiddenSurfaceRemoval = hiddenSurfaceRemoval }
        if let drawOptimizations { config.drawOptimizations = drawOptimizations }
        if let fastWBufferDepth { config.fastWBufferDepth = fastWBufferDepth }
        if let antiAliasing { config.antiAliasing = antiAliasing }
        if let waitForGPU { config.waitForGPU = waitForGPU }
        if let textureCompression { config.textureCompression = textureCompression }
        if let hardwareShaders { config.useHardwareShaders = hardwareShaders }
        if let cpuClockPercentage { config.cpuClockPercentage = cpuClockPercentage }
        if let stereoMode { config.stereoMode = stereoMode }
        if let region { config.region = region }
        if let isNew3DS { config.isNew3DS = isNew3DS }
        if let aspectRatio { config.aspectRatio = aspectRatio }
        if let textureSampling { config.textureSampling = textureSampling }
        if let monoEye { config.monoEye = monoEye }
        if let audioEmulation { config.audioEmulation = audioEmulation }
        if let lleApplets { config.lleApplets = lleApplets }
        if let pluginLoader { config.pluginLoader = pluginLoader }
        if let onlineModules { config.onlineModules = onlineModules }
        if let regionFreePatch { config.regionFreePatch = regionFreePatch }
        if let postEffect { config.postEffect = postEffect }
        if let fastForwardSpeed { config.fastForwardSpeed = fastForwardSpeed }
        if let hacks { config.gameHackOverrides = hacks.mapValues { NSNumber(value: $0) } }
        if let speedLimit {
            config.useFrameLimit = speedLimit != 0
            if speedLimit != 0 { config.frameLimit = speedLimit }
        }
    }
}

/// Keeps the overrides of every game, by title ID, in UserDefaults.
final class GameOverridesStore: ObservableObject {
    static let shared = GameOverridesStore()

    private let defaults = UserDefaults.standard
    private let key = "gameOverrides"
    private var all: [String: GameOverrides]

    private init() {
        if let data = defaults.data(forKey: key),
            let decoded = try? JSONDecoder().decode([String: GameOverrides].self, from: data)
        {
            all = decoded
        } else {
            all = [:]
        }
    }

    func overrides(for game: Game) -> GameOverrides {
        all[game.programIDString] ?? GameOverrides()
    }

    func set(_ overrides: GameOverrides, for game: Game) {
        objectWillChange.send()
        if overrides.isEmpty {
            all[game.programIDString] = nil
        } else {
            all[game.programIDString] = overrides
        }
        if let data = try? JSONEncoder().encode(all) {
            defaults.set(data, forKey: key)
        }
    }

    func hasOverrides(_ game: Game) -> Bool {
        all[game.programIDString] != nil
    }
}

/// The workarounds for single games that the core has built in.
enum GameHack: String, CaseIterable, Identifiable {
    case rightEye
    case accurateMul
    case shaderFixup
    case delayTextureCopy
    case regionFromSecure
    case spoofFriendCode
    case onlineLLE

    var id: String { rawValue }

    var title: String {
        switch self {
        case .rightEye: return "Skip Right Eye With 3D Off"
        case .accurateMul: return "Accurate Shader Multiplication"
        case .shaderFixup: return "Shader Fix-Up"
        case .delayTextureCopy: return "Delay Texture Copy Completion"
        case .regionFromSecure: return "Region From Secure Info"
        case .spoofFriendCode: return "Spoof Friend Code Seed"
        case .onlineLLE: return "Online System Modules"
        }
    }

    var details: String {
        switch self {
        case .rightEye:
            return "While 3D is off the right eye is not drawn, which is faster. A few games "
                + "(Luigi's Mansion games, Rayman Origins, Minecraft) draw wrongly that way and "
                + "have it off in the built-in list."
        case .accurateMul:
            return "Multiplies the way the console does for zero and infinite values. Slower, "
                + "needed by games with broken geometry (Ocarina of Time 3D, the Mario & Luigi "
                + "games)."
        case .shaderFixup:
            return "Corrects the shader programs of games that need it."
        case .delayTextureCopy:
            return "Reports a finished texture copy a little later, which fixes timing "
                + "problems in a few games."
        case .regionFromSecure:
            return "Reports the region of the console's secure info instead of the set one. For "
                + "system applets."
        case .spoofFriendCode:
            return "Uses the wireless address as the friend code seed, so local play works "
                + "without a set-up console."
        case .onlineLLE:
            return "Runs the console's own online system modules for this game."
        }
    }
}

/// What the external display shows (see AZConfiguration.secondaryDisplayLayout).
enum SecondaryLayoutOption {
    static func title(for value: Int) -> String {
        switch value {
        case 0: return "Top Screen (Bottom Stays on Device)"
        case 1: return "Top Screen Only"
        case 2: return "Bottom Screen Only"
        case 3: return "Side by Side"
        case 4: return "Opposite Screen Only"
        case 5: return "Original"
        case 6: return "Hybrid"
        case 7: return "Large Screen"
        default: return "\(value)"
        }
    }
}
