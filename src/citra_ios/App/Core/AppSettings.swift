// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

import AzaharCore
import Combine
import Foundation
import UIKit

enum LibraryStyle: String, CaseIterable, Identifiable {
    case list
    case grid

    var id: String { rawValue }
    var title: String { self == .list ? "List" : "Grid" }
}

/// The refresh rate the display is asked to run at while a game is shown.
enum RefreshRateOption: Int, CaseIterable, Identifiable {
    case automatic = 0
    case sixty = 60
    case oneTwenty = 120

    var id: Int { rawValue }
    var title: String {
        switch self {
        case .automatic: return "Automatic"
        case .sixty: return "60 Hz"
        case .oneTwenty: return "120 Hz"
        }
    }
}

enum ScreenLayout: Int, CaseIterable, Identifiable {
    case standard = 0
    case singleScreen = 1
    case largeScreen = 2
    case sideBySide = 3
    case hybrid = 5
    case custom = 6

    var id: Int { rawValue }
    var title: String {
        switch self {
        case .standard: return "Default"
        case .singleScreen: return "Single Screen"
        case .largeScreen: return "Large Screen"
        case .sideBySide: return "Side by Side"
        case .hybrid: return "Hybrid"
        case .custom: return "Custom"
        }
    }
}

enum PortraitScreenLayout: Int, CaseIterable, Identifiable {
    case topFullWidth = 0
    case custom = 1
    case original = 2

    var id: Int { rawValue }
    var title: String {
        switch self {
        case .topFullWidth: return "Top Full Width"
        case .custom: return "Custom"
        case .original: return "Original"
        }
    }
}

/// Where the small screen sits next to the large one. The values are Settings::SmallScreenPosition.
enum SmallScreenPlacement: Int, CaseIterable, Identifiable {
    case topRight = 0
    case middleRight = 1
    case bottomRight = 2
    case topLeft = 3
    case middleLeft = 4
    case bottomLeft = 5
    case aboveLarge = 6
    case belowLarge = 7

    var id: Int { rawValue }
    var title: String {
        switch self {
        case .topRight: return "Top Right"
        case .middleRight: return "Middle Right"
        case .bottomRight: return "Bottom Right"
        case .topLeft: return "Top Left"
        case .middleLeft: return "Middle Left"
        case .bottomLeft: return "Bottom Left"
        case .aboveLarge: return "Above"
        case .belowLarge: return "Below"
        }
    }
}

enum ConsoleRegion: Int, CaseIterable, Identifiable {
    case auto = -1
    case japan = 0
    case usa = 1
    case europe = 2
    case australia = 3
    case china = 4
    case korea = 5
    case taiwan = 6

    var id: Int { rawValue }
    var title: String {
        switch self {
        case .auto: return "Auto-select"
        case .japan: return "Japan"
        case .usa: return "USA"
        case .europe: return "Europe"
        case .australia: return "Australia"
        case .china: return "China"
        case .korea: return "Korea"
        case .taiwan: return "Taiwan"
        }
    }
}

/// GPU memory compression of textures. The values are AZConfiguration.textureCompression.
enum TextureCompressionOption: Int, CaseIterable, Identifiable {
    case off = 0
    case lossless = 1
    case lossy = 2

    var id: Int { rawValue }
    var title: String {
        switch self {
        case .off: return "Off"
        case .lossless: return "Lossless"
        case .lossy: return "Lossy"
        }
    }
}

enum UpscalingOption: Int, CaseIterable, Identifiable {
    case off = 0
    case spatial = 1

    var id: Int { rawValue }
    var title: String { self == .off ? "Off" : "MetalFX Spatial" }
}

/// Which camera of the device a camera of the 3DS shows. The values are the ones of AZConfiguration.
enum CameraSource: Int, CaseIterable, Identifiable {
    case off = 0
    case back = 1
    case front = 2

    var id: Int { rawValue }
    var title: String {
        switch self {
        case .off: return "Off"
        case .back: return "Back Camera"
        case .front: return "Front Camera"
        }
    }
}

/// How the two screens' 3D picture is shown. The values are Settings::StereoRenderOption.
enum StereoModeOption: Int, CaseIterable, Identifiable {
    case off = 0
    case sideBySide = 1
    case sideBySideFull = 2
    case anaglyph = 3
    case interlaced = 4
    case reverseInterlaced = 5
    case cardboard = 6

    var id: Int { rawValue }
    var title: String {
        switch self {
        case .off: return "Off"
        case .sideBySide: return "Side by Side"
        case .sideBySideFull: return "Side by Side (Full Width)"
        case .anaglyph: return "Anaglyph (Red/Cyan)"
        case .interlaced: return "Interlaced"
        case .reverseInterlaced: return "Reverse Interlaced"
        case .cardboard: return "Cardboard VR"
        }
    }
}

enum TextureFilterOption: Int, CaseIterable, Identifiable {
    case none = 0
    case anime4k = 1
    case bicubic = 2
    case scaleForce = 3
    case xbrz = 4
    case mmpx = 5

    var id: Int { rawValue }
    var title: String {
        switch self {
        case .none: return "None"
        case .anime4k: return "Anime4K"
        case .bicubic: return "Bicubic"
        case .scaleForce: return "ScaleForce"
        case .xbrz: return "xBRZ"
        case .mmpx: return "MMPX"
        }
    }
}

/// Persisted user settings. Values are stored in UserDefaults and converted to an AZConfiguration
/// when a game starts.
final class AppSettings: ObservableObject {
    private let defaults = UserDefaults.standard

    // App
    @Published var libraryStyle: LibraryStyle { didSet { save(libraryStyle.rawValue, "libraryStyle") } }
    @Published var showPerformanceOverlay: Bool { didSet { save(showPerformanceOverlay, "showPerformanceOverlay") } }
    /// Fade out the menu and swap buttons after a few seconds without a touch while a physical
    /// controller is connected.
    @Published var autoHideMenuButtons: Bool { didSet { save(autoHideMenuButtons, "autoHideMenuButtons") } }
    /// The Home button of the on-screen Select / Home / Start control.
    @Published var showHomeButton: Bool { didSet { save(showHomeButton, "showHomeButton") } }
    /// Enable JIT with the helper inside the app (see JITHelper) instead of a debugger app. When
    /// on it replaces opening a debugger app.
    @Published var builtInJIT: Bool { didSet { save(builtInJIT, "builtInJIT") } }
    /// Open the JIT debugger app at launch when JIT is not enabled yet.
    @Published var autoEnableJIT: Bool { didSet { save(autoEnableJIT, "autoEnableJIT") } }
    @Published var jitDebuggerName: String { didSet { save(jitDebuggerName, "jitDebugger") } }
    @Published var jitCustomURL: String { didSet { save(jitCustomURL, "jitCustomURL") } }
    var jitDebugger: JITDebugger {
        get { JITDebugger(rawValue: jitDebuggerName) ?? .stikDebug }
        set { jitDebuggerName = newValue.rawValue }
    }
    @Published var hideControllerWithHardware: Bool { didSet { save(hideControllerWithHardware, "hideControllerWithHardware") } }
    @Published var showController: Bool { didSet { save(showController, "showController") } }
    @Published var showSwapButton: Bool { didSet { save(showSwapButton, "showSwapButton") } }
    @Published var controllerOpacity: Double { didSet { save(controllerOpacity, "controllerOpacity") } }
    @Published var hapticFeedback: Bool { didSet { save(hapticFeedback, "hapticFeedback") } }
    /// The on-screen controller as Liquid Glass (iOS 26), and how tinted the glass is (0 clear).
    @Published var glassControls: Bool { didSet { save(glassControls, "glassControls") } }
    @Published var glassControlTint: Double { didSet { save(glassControlTint, "glassControlTint") } }
    @Published var useMotion: Bool { didSet { save(useMotion, "useMotion") } }

    // CPU
    @Published var cpuJIT: Bool { didSet { save(cpuJIT, "cpuJIT") } }
    @Published var cpuClockPercentage: Int { didSet { save(cpuClockPercentage, "cpuClockPercentage") } }

    // Graphics
    @Published var shaderJIT: Bool { didSet { save(shaderJIT, "shaderJIT") } }
    @Published var hardwareShaders: Bool { didSet { save(hardwareShaders, "hardwareShaders") } }
    @Published var accurateMultiplication: Bool { didSet { save(accurateMultiplication, "accurateMultiplication") } }
    @Published var resolutionFactor: Int { didSet { save(resolutionFactor, "resolutionFactor") } }
    @Published var linearFiltering: Bool { didSet { save(linearFiltering, "linearFiltering") } }
    @Published var textureFilter: TextureFilterOption { didSet { save(textureFilter.rawValue, "textureFilter") } }
    @Published var skipDuplicateFrames: Bool { didSet { save(skipDuplicateFrames, "skipDuplicateFrames") } }
    @Published var outerCamera: CameraSource { didSet { save(outerCamera.rawValue, "outerCamera") } }
    @Published var innerCamera: CameraSource { didSet { save(innerCamera.rawValue, "innerCamera") } }
    @Published var stereoMode: StereoModeOption { didSet { save(stereoMode.rawValue, "stereoMode") } }
    @Published var stereoDepth: Int { didSet { save(stereoDepth, "stereoDepth") } }
    @Published var swapEyes3D: Bool { didSet { save(swapEyes3D, "swapEyes3D") } }
    @Published var refreshRate: RefreshRateOption { didSet { save(refreshRate.rawValue, "refreshRate") } }
    @Published var framePacing: Bool { didSet { save(framePacing, "framePacing") } }
    /// Frames shown by a display link and made in step with the display's refreshes.
    @Published var displaySync: Bool { didSet { save(displaySync, "displaySync") } }
    /// Each new game frame is shown for two 3DS refreshes (steady 30 FPS), with display sync.
    @Published var frameCap30: Bool { didSet { save(frameCap30, "frameCap30") } }
    /// With the cap, the game itself also runs at 30 (its 3D work never finishes within a refresh).
    @Published var frameCapHoldsGame: Bool { didSet { save(frameCapHoldsGame, "frameCapHoldsGame") } }
    /// Delay in milliseconds that the pacing adds so a slow frame still meets its slot.
    @Published var pacingBufferMs: Int { didSet { save(pacingBufferMs, "pacingBufferMs") } }
    @Published var asyncShaders: Bool { didSet { save(asyncShaders, "asyncShaders") } }
    @Published var shaderWaitMs: Int { didSet { save(shaderWaitMs, "shaderWaitMs") } }
    @Published var frameGeneration: Int { didSet { save(frameGeneration, "frameGeneration") } }
    /// Size the in-between frames are made at: 0 full, 1 half, 2 three eighths, 3 a quarter.
    @Published var frameGenerationSize: Int {
        didSet { save(frameGenerationSize, "frameGenerationSize") }
    }
    /// Frame generation pauses while it falls behind and comes back when the scene is lighter.
    @Published var frameGenerationPause: Bool {
        didSet { save(frameGenerationPause, "frameGenerationPause") }
    }
    /// One more frame kept waiting before frames are shown (steadier, a refresh more latency).
    @Published var frameGenerationBuffer: Bool {
        didSet { save(frameGenerationBuffer, "frameGenerationBuffer") }
    }
    @Published var frameGenerationTopOnly: Bool {
        didSet { save(frameGenerationTopOnly, "frameGenerationTopOnly") }
    }
    @Published var frameGenerationAllGames: Bool {
        didSet { save(frameGenerationAllGames, "frameGenerationAllGames") }
    }
    /// Make the frames with MetalFX's interpolator instead of VideoToolbox's.
    @Published var frameGenerationMetalFX: Bool {
        didSet { save(frameGenerationMetalFX, "frameGenerationMetalFX") }
    }
    @Published var frameGenerationDisplayPaced: Bool {
        didSet { save(frameGenerationDisplayPaced, "frameGenerationDisplayPaced") }
    }
    @Published var holdFramesForShaders: Bool { didSet { save(holdFramesForShaders, "holdFramesForShaders") } }
    @Published var fastLighting: Bool { didSet { save(fastLighting, "fastLighting") } }
    @Published var fastLightingLookups: Bool {
        didSet { save(fastLightingLookups, "fastLightingLookups") }
    }
    @Published var fastColor: Bool { didSet { save(fastColor, "fastColor") } }
    @Published var loggingEnabled: Bool {
        didSet {
            save(loggingEnabled, "loggingEnabled")
            AZEmulator.setLoggingEnabled(loggingEnabled)
        }
    }
    @Published var fastMathFunctions: Bool { didSet { save(fastMathFunctions, "fastMathFunctions") } }
    @Published var halfInterpolants: Bool { didSet { save(halfInterpolants, "halfInterpolants") } }
    @Published var untrackedBuffers: Bool { didSet { save(untrackedBuffers, "untrackedBuffers") } }
    @Published var vertexReuse: Bool { didSet { save(vertexReuse, "vertexReuse") } }
    @Published var vertexFastFunctions: Bool { didSet { save(vertexFastFunctions, "vertexFastFunctions") } }
    @Published var vertexFastMath: Bool { didSet { save(vertexFastMath, "vertexFastMath") } }
    @Published var texturePool: Bool { didSet { save(texturePool, "texturePool") } }
    @Published var fastDepthReadback: Bool { didSet { save(fastDepthReadback, "fastDepthReadback") } }
    /// Draw-time optimizations (no fragment function for depth-only draws, hardware scissor,
    /// no-op blending dropped).
    @Published var drawOptimizations: Bool { didSet { save(drawOptimizations, "drawOptimizations") } }
    @Published var fastWBufferDepth: Bool { didSet { save(fastWBufferDepth, "fastWBufferDepth") } }
    /// MSAA: 0 off, 1 2x, 2 4x, 3 8x.
    @Published var antiAliasing: Int { didSet { save(antiAliasing, "antiAliasing") } }
    @Published var waitForGPU: Bool { didSet { save(waitForGPU, "waitForGPU") } }
    /// GPU timestamps per render pass in the log (diagnostic).
    @Published var gpuPassTiming: Bool { didSet { save(gpuPassTiming, "gpuPassTiming") } }
    /// Writes the most used shaders to Neroli/shader_dump (diagnostic).
    @Published var dumpShaders: Bool { didSet { save(dumpShaders, "dumpShaders") } }
    @Published var hiddenSurfaceRemoval: Bool {
        didSet { save(hiddenSurfaceRemoval, "hiddenSurfaceRemoval") }
    }
    @Published var textureCompression: TextureCompressionOption {
        didSet { save(textureCompression.rawValue, "textureCompression") }
    }
    @Published var fastShaderMath: Bool { didSet { save(fastShaderMath, "fastShaderMath") } }
    @Published var anisotropy: Int { didSet { save(anisotropy, "anisotropy") } }
    @Published var diskShaderCache: Bool { didSet { save(diskShaderCache, "diskShaderCache") } }
    @Published var upscaling: UpscalingOption { didSet { save(upscaling.rawValue, "upscaling") } }
    /// How far MetalFX scales, in tenths (15 is 1.5x); 0 scales straight to the display size.
    @Published var upscaleFactorTenths: Int { didSet { save(upscaleFactorTenths, "upscaleFactorTenths") } }
    @Published var sharpPixels: Bool { didSet { save(sharpPixels, "sharpPixels") } }
    /// Sharpening of the screens in percent, 0 is off.
    @Published var sharpness: Int { didSet { save(sharpness, "sharpness") } }
    /// 0 unsharp mask, 1 contrast adaptive sharpening.
    @Published var sharpenMethod: Int { didSet { save(sharpenMethod, "sharpenMethod") } }
    /// How a screen is enlarged: 0 bilinear, 1 bicubic, 2 Lanczos.
    @Published var scaleFilter: Int { didSet { save(scaleFilter, "scaleFilter") } }
    /// Darkness of the LCD grid gaps in percent, 0 is the default depth.
    @Published var gridStrength: Int { didSet { save(gridStrength, "gridStrength") } }
    /// Color adjustment of the screens, -100 to 100, 0 for none.
    @Published var saturation: Int { didSet { save(saturation, "saturation") } }
    @Published var contrast: Int { didSet { save(contrast, "contrast") } }
    @Published var brightness: Int { didSet { save(brightness, "brightness") } }

    // Layout
    @Published var layout: ScreenLayout { didSet { save(layout.rawValue, "layout") } }
    @Published var portraitLayout: PortraitScreenLayout { didSet { save(portraitLayout.rawValue, "portraitLayout") } }
    @Published var uprightScreens: Bool { didSet { save(uprightScreens, "uprightScreens") } }
    @Published var screenGap: Int { didSet { save(screenGap, "screenGap") } }
    @Published var largeScreenProportion: Double {
        didSet { save(largeScreenProportion, "largeScreenProportion") }
    }
    @Published var smallScreenPlacement: SmallScreenPlacement {
        didSet { save(smallScreenPlacement.rawValue, "smallScreenPlacement") }
    }
    /// Rectangles of the custom layout as fractions of the screen (top x, y, width, height, then
    /// the bottom screen). Empty until the layout was edited.
    @Published var customLayoutLandscape: [Double] {
        didSet { save(customLayoutLandscape, "customLayoutLandscape") }
    }
    @Published var customLayoutPortrait: [Double] {
        didSet { save(customLayoutPortrait, "customLayoutPortrait") }
    }
    @Published var useExternalDisplay: Bool { didSet { save(useExternalDisplay, "useExternalDisplay") } }
    @Published var secondaryDisplayLayout: Int { didSet { save(secondaryDisplayLayout, "secondaryDisplayLayout") } }
    @Published var disableRightEyeRender: Bool { didSet { save(disableRightEyeRender, "disableRightEyeRender") } }
    @Published var integerScaling: Bool { didSet { save(integerScaling, "integerScaling") } }
    @Published var asyncCustomTextures: Bool { didSet { save(asyncCustomTextures, "asyncCustomTextures") } }
    @Published var deterministicAsync: Bool { didSet { save(deterministicAsync, "deterministicAsync") } }
    @Published var simulateHeadphones: Bool { didSet { save(simulateHeadphones, "simulateHeadphones") } }
    @Published var simulateGPUTimings: Bool { didSet { save(simulateGPUTimings, "simulateGPUTimings") } }
    @Published var gpuTimingPercent: Int { didSet { save(gpuTimingPercent, "gpuTimingPercent") } }
    /// In tenths of a millisecond (180 = 18.0 ms).
    @Published var gpuTimingMinMs: Int { didSet { save(gpuTimingMinMs, "gpuTimingMinTenths") } }
    @Published var customTextures: Bool { didSet { save(customTextures, "customTextures") } }
    @Published var preloadTextures: Bool { didSet { save(preloadTextures, "preloadTextures") } }
    @Published var dumpTextures: Bool { didSet { save(dumpTextures, "dumpTextures") } }
    @Published var swapScreens: Bool { didSet { save(swapScreens, "swapScreens") } }
    @Published var respectNotch: Bool { didSet { save(respectNotch, "respectNotch") } }

    // Audio
    @Published var keepOriginalsWhenCompressing: Bool {
        didSet { save(keepOriginalsWhenCompressing, "keepOriginalsWhenCompressing") }
    }
    @Published var fixedClock: Bool { didSet { save(fixedClock, "fixedClock") } }
    @Published var fixedTime: Double { didSet { save(fixedTime, "fixedTime") } }
    @Published var fixedBattery: Bool { didSet { save(fixedBattery, "fixedBattery") } }
    @Published var batteryLevel: Int { didSet { save(batteryLevel, "batteryLevel") } }
    @Published var batteryCharging: Bool { didSet { save(batteryCharging, "batteryCharging") } }
    @Published var stepsPerHour: Int { didSet { save(stepsPerHour, "stepsPerHour") } }
    @Published var regionFreePatch: Bool { didSet { save(regionFreePatch, "regionFreePatch") } }
    @Published var pluginLoader: Bool { didSet { save(pluginLoader, "pluginLoader") } }
    @Published var onlineModules: Bool { didSet { save(onlineModules, "onlineModules") } }
    @Published var aspectRatio: Int { didSet { save(aspectRatio, "aspectRatio") } }
    @Published var topStretch: Bool { didSet { save(topStretch, "topStretch") } }
    @Published var bottomStretch: Bool { didSet { save(bottomStretch, "bottomStretch") } }
    @Published var topPaddingH: Int { didSet { save(topPaddingH, "topPaddingH") } }
    @Published var topPaddingV: Int { didSet { save(topPaddingV, "topPaddingV") } }
    @Published var bottomPaddingH: Int { didSet { save(bottomPaddingH, "bottomPaddingH") } }
    @Published var bottomPaddingV: Int { didSet { save(bottomPaddingV, "bottomPaddingV") } }
    @Published var cardboardSize: Int { didSet { save(cardboardSize, "cardboardSize") } }
    @Published var cardboardXShift: Int { didSet { save(cardboardXShift, "cardboardXShift") } }
    @Published var cardboardYShift: Int { didSet { save(cardboardYShift, "cardboardYShift") } }
    @Published var fastForwardSpeed: Int { didSet { save(fastForwardSpeed, "fastForwardSpeed") } }
    @Published var fastForwardHold: Bool { didSet { save(fastForwardHold, "fastForwardHold") } }
    @Published var postEffect: Int { didSet { save(postEffect, "postEffect") } }
    @Published var anaglyphMode: Int { didSet { save(anaglyphMode, "anaglyphMode") } }
    @Published var postShader: String { didSet { save(postShader, "postShader") } }
    @Published var useVirtualSD: Bool { didSet { save(useVirtualSD, "useVirtualSD") } }
    @Published var compressCIAInstalls: Bool {
        didSet { save(compressCIAInstalls, "compressCIAInstalls") }
    }
    @Published var realtimeAudio: Bool { didSet { save(realtimeAudio, "realtimeAudio") } }
    @Published var audioEmulation: Int { didSet { save(audioEmulation, "audioEmulation") } }
    @Published var lleApplets: Bool { didSet { save(lleApplets, "lleApplets") } }
    @Published var textureSampling: Int { didSet { save(textureSampling, "textureSampling") } }
    @Published var monoEye: Int { didSet { save(monoEye, "monoEye") } }
    /// The layouts the user saved, the ones the layout shortcut cycles through.
    @Published var layoutPresets: [LayoutPreset] {
        didSet {
            if let data = try? JSONEncoder().encode(layoutPresets) {
                defaults.set(data, forKey: "layoutPresets")
            }
        }
    }
    var lastLayoutPresetIndex: Int {
        get { defaults.integer(forKey: "lastLayoutPresetIndex") }
        set { defaults.set(newValue, forKey: "lastLayoutPresetIndex") }
    }
    /// 0 title, 1 recently played, 2 most played, 3 compatibility.
    @Published var librarySort: Int { didSet { save(librarySort, "librarySort") } }
    @Published var favoritesFirst: Bool { didSet { save(favoritesFirst, "favoritesFirst") } }
    /// The stick that moves a cursor over the touch screen: 0 none, 1 left, 2 right.
    @Published var touchStick: Int { didSet { save(touchStick, "touchStick") } }
    @Published var touchStickSpeed: Double { didSet { save(touchStickSpeed, "touchStickSpeed") } }
    @Published var controllerNavigation: Bool { didSet { save(controllerNavigation, "controllerNavigation") } }
    @Published var gameHacksEnabled: Bool { didSet { save(gameHacksEnabled, "gameHacksEnabled") } }
    @Published var useMicrophone: Bool { didSet { save(useMicrophone, "useMicrophone") } }
    @Published var audioStretching: Bool { didSet { save(audioStretching, "audioStretching") } }
    @Published var volume: Double { didSet { save(volume, "volume") } }

    // System
    @Published var isNew3DS: Bool { didSet { save(isNew3DS, "isNew3DS") } }
    @Published var region: ConsoleRegion { didSet { save(region.rawValue, "region") } }
    @Published var useFrameLimit: Bool { didSet { save(useFrameLimit, "useFrameLimit") } }
    @Published var frameLimit: Int { didSet { save(frameLimit, "frameLimit") } }
    /// The three colors blended in the soft background of the app (hex, RRGGBB) and how strong
    /// the background is (percent of the default).
    @Published var backdropColor1: String { didSet { save(backdropColor1, "backdropColor1") } }
    @Published var backdropColor2: String { didSet { save(backdropColor2, "backdropColor2") } }
    @Published var backdropColor3: String { didSet { save(backdropColor3, "backdropColor3") } }
    /// The same three colors for dark mode (the three above are for light mode).
    @Published var backdropDarkColor1: String { didSet { save(backdropDarkColor1, "backdropDarkColor1") } }
    @Published var backdropDarkColor2: String { didSet { save(backdropDarkColor2, "backdropDarkColor2") } }
    @Published var backdropDarkColor3: String { didSet { save(backdropDarkColor3, "backdropDarkColor3") } }
    @Published var backdropIntensity: Int { didSet { save(backdropIntensity, "backdropIntensity") } }
    /// The app's appearance: 0 follows the system, 1 always light, 2 always dark.
    @Published var appearance: Int { didSet { save(appearance, "appearance") } }
    /// Whether the colored background is shown behind the game grid and the game list.
    @Published var gridBackdropEnabled: Bool { didSet { save(gridBackdropEnabled, "gridBackdropEnabled") } }
    @Published var listBackdropEnabled: Bool { didSet { save(listBackdropEnabled, "listBackdropEnabled") } }
    /// Run at 60 Hz instead of 59.83 Hz at a limit of 100%, so the frames match the display.
    @Published var syncSpeedToDisplay: Bool { didSet { save(syncSpeedToDisplay, "syncSpeedToDisplay") } }

    init() {
        let d = UserDefaults.standard
        func value<T>(_ key: String, _ fallback: T) -> T { d.object(forKey: key) as? T ?? fallback }

        libraryStyle = LibraryStyle(rawValue: value("libraryStyle", LibraryStyle.grid.rawValue)) ?? .grid
        showPerformanceOverlay = value("showPerformanceOverlay", true)
        hideControllerWithHardware = value("hideControllerWithHardware", true)
        builtInJIT = value("builtInJIT", false)
        autoEnableJIT = value("autoEnableJIT", false)
        jitDebuggerName = value("jitDebugger", JITDebugger.stikDebug.rawValue)
        jitCustomURL = value("jitCustomURL", "")
        showHomeButton = value("showHomeButton", true)
        autoHideMenuButtons = value("autoHideMenuButtons", false)
        showController = value("showController", true)
        showSwapButton = value("showSwapButton", true)
        controllerOpacity = value("controllerOpacity", 0.7)
        hapticFeedback = value("hapticFeedback", true)
        glassControls = value("glassControls", false)
        glassControlTint = value("glassControlTint", 0.3)
        useMotion = value("useMotion", true)

        cpuJIT = value("cpuJIT", true)
        cpuClockPercentage = value("cpuClockPercentage", 100)

        shaderJIT = value("shaderJIT", true)
        hardwareShaders = value("hardwareShaders", true)
        accurateMultiplication = value("accurateMultiplication", true)
        resolutionFactor = value("resolutionFactor", 2)
        linearFiltering = value("linearFiltering", true)
        textureFilter = TextureFilterOption(rawValue: value("textureFilter", 0)) ?? .none
        skipDuplicateFrames = value("skipDuplicateFrames", false)
        outerCamera = CameraSource(rawValue: value("outerCamera", 0)) ?? .off
        innerCamera = CameraSource(rawValue: value("innerCamera", 0)) ?? .off
        stereoMode = StereoModeOption(rawValue: value("stereoMode", 0)) ?? .off
        stereoDepth = value("stereoDepth", 50)
        swapEyes3D = value("swapEyes3D", false)
        refreshRate = RefreshRateOption(rawValue: value("refreshRate", RefreshRateOption.automatic.rawValue)) ?? .automatic
        framePacing = value("framePacing", true)
        displaySync = value("displaySync", true)
        frameCap30 = value("frameCap30", false)
        frameCapHoldsGame = value("frameCapHoldsGame", true)
        pacingBufferMs = value("pacingBufferMs", 0)
        asyncShaders = value("asyncShaders", true)
        holdFramesForShaders = value("holdFramesForShaders", true)
        frameGeneration = min(value("frameGeneration", 0), 1)
        // Faster Frame Generation (3/8) from before became a size.
        frameGenerationSize = value("frameGenerationSize", value("frameGenerationFast", false) ? 2 : 1)
        frameGenerationTopOnly = value("frameGenerationTopOnly", false)
        frameGenerationPause = value("frameGenerationPause", false)
        frameGenerationBuffer = value("frameGenerationBuffer", false)
        frameGenerationDisplayPaced = value("frameGenerationDisplayPaced", false)
        frameGenerationMetalFX = value("frameGenerationMetalFX", false)
        frameGenerationAllGames = value("frameGenerationAllGames", false)
        shaderWaitMs = value("shaderWaitMs", 2)
        fastShaderMath = value("fastShaderMath", false)
        fastLighting = value("fastLighting", false)
        fastLightingLookups = value("fastLightingLookups", false)
        fastColor = value("fastColor", false)
        loggingEnabled = value("loggingEnabled", false)
        fastMathFunctions = value("fastMathFunctions", false)
        halfInterpolants = value("halfInterpolants", false)
        untrackedBuffers = value("untrackedBuffers", false)
        vertexReuse = value("vertexReuse", false)
        vertexFastFunctions = value("vertexFastFunctions", false)
        vertexFastMath = value("vertexFastMath", false)
        texturePool = value("texturePool", false)
        fastDepthReadback = value("fastDepthReadback", false)
        hiddenSurfaceRemoval = value("hiddenSurfaceRemoval", false)
        drawOptimizations = value("drawOptimizations", true)
        fastWBufferDepth = value("fastWBufferDepth", false)
        antiAliasing = value("antiAliasing", 0)
        waitForGPU = value("waitForGPU", true)
        gpuPassTiming = value("gpuPassTiming", false)
        dumpShaders = value("dumpShaders", false)
        textureCompression = TextureCompressionOption(rawValue: value("textureCompression", 0)) ?? .off
        anisotropy = value("anisotropy", 4)
        diskShaderCache = value("diskShaderCache", true)
        upscaling = UpscalingOption(rawValue: value("upscaling", 0)) ?? .off
        upscaleFactorTenths = value("upscaleFactorTenths", 0)
        sharpPixels = value("sharpPixels", false)
        sharpness = value("sharpness", 0)
        sharpenMethod = value("sharpenMethod", 0)
        scaleFilter = value("scaleFilter", 0)
        gridStrength = value("gridStrength", 0)
        saturation = value("saturation", 0)
        contrast = value("contrast", 0)
        brightness = value("brightness", 0)

        layout = ScreenLayout(rawValue: value("layout", ScreenLayout.standard.rawValue)) ?? .standard
        portraitLayout = PortraitScreenLayout(rawValue: value("portraitLayout", 0)) ?? .topFullWidth
        uprightScreens = value("uprightScreens", false)
        screenGap = value("screenGap", 0)
        largeScreenProportion = value("largeScreenProportion", 4.0)
        smallScreenPlacement =
            SmallScreenPlacement(rawValue: value("smallScreenPlacement", 2)) ?? .bottomRight
        customLayoutLandscape = value("customLayoutLandscape", [Double]())
        customLayoutPortrait = value("customLayoutPortrait", [Double]())
        useExternalDisplay = value("useExternalDisplay", true)
        secondaryDisplayLayout = value("secondaryDisplayLayout", 0)
        disableRightEyeRender = value("disableRightEyeRender", false)
        integerScaling = value("integerScaling", false)
        asyncCustomTextures = value("asyncCustomTextures", true)
        deterministicAsync = value("deterministicAsync", false)
        simulateHeadphones = value("simulateHeadphones", false)
        simulateGPUTimings = value("simulateGPUTimings", false)
        gpuTimingPercent = value("gpuTimingPercent", 200)
        gpuTimingMinMs = value("gpuTimingMinTenths", value("gpuTimingMinMs", 0) * 10)
        customTextures = value("customTextures", false)
        preloadTextures = value("preloadTextures", false)
        dumpTextures = value("dumpTextures", false)
        swapScreens = value("swapScreens", false)
        respectNotch = value("respectNotch", true)

        keepOriginalsWhenCompressing = value("keepOriginalsWhenCompressing", true)
        fixedClock = value("fixedClock", false)
        fixedTime = value("fixedTime", 946681277)
        fixedBattery = value("fixedBattery", false)
        batteryLevel = value("batteryLevel", 4)
        batteryCharging = value("batteryCharging", true)
        stepsPerHour = value("stepsPerHour", 0)
        regionFreePatch = value("regionFreePatch", true)
        pluginLoader = value("pluginLoader", false)
        onlineModules = value("onlineModules", false)
        aspectRatio = value("aspectRatio", 0)
        topStretch = value("topStretch", false)
        bottomStretch = value("bottomStretch", false)
        topPaddingH = value("topPaddingH", 0)
        topPaddingV = value("topPaddingV", 0)
        bottomPaddingH = value("bottomPaddingH", 0)
        bottomPaddingV = value("bottomPaddingV", 0)
        cardboardSize = value("cardboardSize", 85)
        cardboardXShift = value("cardboardXShift", 0)
        cardboardYShift = value("cardboardYShift", 0)
        fastForwardSpeed = value("fastForwardSpeed", 300)
        fastForwardHold = value("fastForwardHold", false)
        postEffect = value("postEffect", 0)
        anaglyphMode = value("anaglyphMode", 0)
        postShader = value("postShader", "")
        useVirtualSD = value("useVirtualSD", true)
        compressCIAInstalls = value("compressCIAInstalls", false)
        realtimeAudio = value("realtimeAudio", false)
        audioEmulation = value("audioEmulation", 0)
        lleApplets = value("lleApplets", false)
        textureSampling = value("textureSampling", 0)
        monoEye = value("monoEye", 0)
        if let data = d.data(forKey: "layoutPresets"),
           let presets = try? JSONDecoder().decode([LayoutPreset].self, from: data) {
            layoutPresets = presets
        } else {
            layoutPresets = []
        }
        librarySort = value("librarySort", 0)
        favoritesFirst = value("favoritesFirst", true)
        touchStick = value("touchStick", 0)
        touchStickSpeed = value("touchStickSpeed", 1.0)
        gameHacksEnabled = value("gameHacksEnabled", true)
        controllerNavigation = value("controllerNavigation", true)
        useMicrophone = value("useMicrophone", false)
        audioStretching = value("audioStretching", true)
        volume = value("volume", 1.0)

        isNew3DS = value("isNew3DS", true)
        region = ConsoleRegion(rawValue: value("region", -1)) ?? .auto
        useFrameLimit = value("useFrameLimit", true)
        frameLimit = value("frameLimit", 100)
        syncSpeedToDisplay = value("syncSpeedToDisplay", true)
        backdropColor1 = value("backdropColor1", BackdropPalette.defaultColors[0])
        backdropColor2 = value("backdropColor2", BackdropPalette.defaultColors[1])
        backdropColor3 = value("backdropColor3", BackdropPalette.defaultColors[2])
        backdropDarkColor1 = value("backdropDarkColor1", value("backdropColor1", BackdropPalette.defaultColors[0]))
        backdropDarkColor2 = value("backdropDarkColor2", value("backdropColor2", BackdropPalette.defaultColors[1]))
        backdropDarkColor3 = value("backdropDarkColor3", value("backdropColor3", BackdropPalette.defaultColors[2]))
        backdropIntensity = value("backdropIntensity", 100)
        appearance = value("appearance", 0)
        gridBackdropEnabled = value("gridBackdropEnabled", true)
        listBackdropEnabled = value("listBackdropEnabled", true)
        // The saved choice applies from the start (property observers do not run in init).
        AZEmulator.setLoggingEnabled(loggingEnabled)
    }

    private func save(_ value: Any, _ key: String) {
        defaults.set(value, forKey: key)
    }

    /// Builds the core configuration for a game: the current settings with the game's own
    /// overrides applied.
    func makeConfiguration(for game: Game?) -> AZConfiguration {
        let config = makeConfiguration()
        if let game {
            GameOverridesStore.shared.overrides(for: game).apply(to: config)
        }
        return config
    }

    /// Builds the core configuration from the current settings.
    func makeConfiguration() -> AZConfiguration {
        let config = AZConfiguration.makeDefault()
        config.outerCamera = outerCamera.rawValue
        config.innerCamera = innerCamera.rawValue
        config.stereoMode = stereoMode.rawValue
        config.stereoDepth = stereoDepth
        config.swapEyes3D = swapEyes3D
        config.useCPUJIT = cpuJIT
        config.cpuClockPercentage = cpuClockPercentage
        config.useShaderJIT = shaderJIT
        config.useHardwareShaders = hardwareShaders
        config.accurateMultiplication = accurateMultiplication
        config.resolutionFactor = resolutionFactor
        config.linearFiltering = linearFiltering
        config.textureFilter = textureFilter.rawValue
        config.skipDuplicateFrames = skipDuplicateFrames
        config.asyncShaderCompilation = asyncShaders
        config.holdFramesForShaders = holdFramesForShaders
        config.frameGeneration =
            FrameGenerator.isSupported && frameGeneration > 0 ? (frameGenerationAllGames ? 2 : 1) : 0
        config.frameGenerationSize = frameGenerationSize
        config.frameGenerationTopOnly = frameGenerationTopOnly
        config.frameGenerationPause = frameGenerationPause
        config.frameGenerationBuffer = frameGenerationBuffer
        config.frameGenerationDisplayPaced = frameGenerationDisplayPaced
        config.frameGenerationMetalFX = frameGenerationMetalFX
        // The rate the display really runs at: the one chosen in the settings (never above what the
        // screen can do), or the fastest the screen can do with Automatic. The renderer paces
        // frames to its refreshes and decides from it whether 60 FPS games can be interpolated.
        let screenMax = UIScreen.main.maximumFramesPerSecond
        config.displayRefreshRate = refreshRate == .automatic ? screenMax : min(refreshRate.rawValue, screenMax)
        config.shaderWaitMs = shaderWaitMs
        config.framePacing = framePacing
        config.displaySync = displaySync
        config.frameCap30 = frameCap30
        config.frameCapHoldsGame = frameCapHoldsGame
        config.pacingBufferMs = pacingBufferMs
        config.fastShaderMath = fastShaderMath
        config.fastLighting = fastLighting
        config.fastLightingLookups = fastLightingLookups
        config.fastColor = fastColor
        config.loggingEnabled = loggingEnabled
        config.fastMathFunctions = fastMathFunctions
        config.halfInterpolants = halfInterpolants
        config.untrackedBuffers = untrackedBuffers
        config.vertexReuse = vertexReuse
        config.vertexFastFunctions = vertexFastFunctions
        config.vertexFastMath = vertexFastMath
        config.texturePool = texturePool
        config.fastDepthReadback = fastDepthReadback
        config.hiddenSurfaceRemoval = hiddenSurfaceRemoval
        config.drawOptimizations = drawOptimizations
        config.fastWBufferDepth = fastWBufferDepth
        config.antiAliasing = antiAliasing
        config.waitForGPU = waitForGPU
        config.gpuPassTiming = gpuPassTiming
        config.dumpShaders = dumpShaders
        config.textureCompression = textureCompression.rawValue
        config.anisotropy = anisotropy
        config.diskShaderCache = diskShaderCache
        config.upscaling = upscaling.rawValue
        config.upscaleFactorTenths = upscaleFactorTenths
        config.sharpPixels = sharpPixels
        config.sharpness = sharpness
        config.sharpenMethod = sharpenMethod
        config.scaleFilter = scaleFilter
        config.gridStrength = gridStrength
        config.saturation = saturation
        config.contrast = contrast
        config.brightness = brightness
        config.layoutOption = AZLayoutOption(rawValue: layout.rawValue) ?? .default
        config.portraitLayoutOption =
            AZPortraitLayoutOption(rawValue: portraitLayout.rawValue) ?? .topFullWidth
        config.swapScreens = swapScreens
        config.uprightScreens = uprightScreens
        config.screenGap = screenGap
        config.largeScreenProportion = Float(largeScreenProportion)
        config.smallScreenPosition = smallScreenPlacement.rawValue
        config.customLayoutLandscape = customLayoutLandscape.map { NSNumber(value: $0) }
        config.customLayoutPortrait = customLayoutPortrait.map { NSNumber(value: $0) }
        config.useExternalDisplay = useExternalDisplay
        config.secondaryDisplayLayout = secondaryDisplayLayout
        config.disableRightEyeRender = disableRightEyeRender
        config.integerScaling = integerScaling
        config.asyncCustomTextures = asyncCustomTextures
        config.deterministicAsync = deterministicAsync
        config.simulateHeadphones = simulateHeadphones
        config.simulateGPUTimings = simulateGPUTimings
        config.gpuTimingPercent = gpuTimingPercent
        config.gpuTimingMinMs = gpuTimingMinMs
        config.customTextures = customTextures
        config.preloadTextures = preloadTextures
        config.dumpTextures = dumpTextures
        config.audioStretching = audioStretching
        config.fixedClock = fixedClock
        config.fixedTime = Int(fixedTime)
        config.fixedBattery = fixedBattery
        config.batteryLevel = batteryLevel
        config.batteryCharging = batteryCharging
        config.stepsPerHour = stepsPerHour
        config.regionFreePatch = regionFreePatch
        config.pluginLoader = pluginLoader
        config.onlineModules = onlineModules
        config.aspectRatio = aspectRatio
        config.topStretch = topStretch
        config.bottomStretch = bottomStretch
        config.topPaddingH = topPaddingH
        config.topPaddingV = topPaddingV
        config.bottomPaddingH = bottomPaddingH
        config.bottomPaddingV = bottomPaddingV
        config.cardboardSize = cardboardSize
        config.cardboardXShift = cardboardXShift
        config.cardboardYShift = cardboardYShift
        config.fastForwardSpeed = fastForwardSpeed
        config.fastForwardHold = fastForwardHold
        config.postEffect = postEffect
        config.anaglyphMode = anaglyphMode
        config.postShader = postShader
        config.useVirtualSD = useVirtualSD
        config.compressCIAInstalls = compressCIAInstalls
        config.customStoragePath = CustomStorage.activate()
        config.realtimeAudio = realtimeAudio
        config.audioEmulation = audioEmulation
        config.lleApplets = lleApplets
        config.textureSampling = textureSampling
        config.monoEye = monoEye
        config.gameHacksEnabled = gameHacksEnabled
        config.useMicrophone = useMicrophone
        config.volume = Float(volume)
        config.isNew3DS = isNew3DS
        config.region = region.rawValue
        config.useFrameLimit = useFrameLimit
        config.frameLimit = frameLimit
        config.syncSpeedToDisplay = syncSpeedToDisplay
        return config
    }
}
