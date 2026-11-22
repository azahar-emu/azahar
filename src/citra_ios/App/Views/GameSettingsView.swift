// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

import SwiftUI

/// The settings of one game. Every row can use the global setting or replace it for this game.
struct GameSettingsView: View {
    let game: Game
    /// Called after a row changed, which the game menu uses to apply the change to the running
    /// game right away.
    var onChange: (() -> Void)?
    @ObservedObject private var store = GameOverridesStore.shared
    @EnvironmentObject private var settings: AppSettings
    @State private var overrides = GameOverrides()
    private var profiles: [ControllerProfile] { ControllerMappingStore.loadProfiles() }
    /// Whether this game runs its vertex shaders on the GPU: its own setting, else the global one.
    private var usesHardwareShaders: Bool {
        overrides.hardwareShaders ?? settings.hardwareShaders
    }

    private static let sizeNames = ["Full", "Half", "3/8", "Quarter", "3/4", "Adaptive"]

    /// The size of the in-between frames; the older Faster Frame Generation override is 3/8.
    private var frameGenerationSize: Binding<Int?> {
        Binding(
            get: {
                if let size = overrides.frameGenerationSize { return size }
                if let fast = overrides.frameGenerationFast { return fast ? 2 : 1 }
                return nil
            },
            set: { value in
                overrides.frameGenerationSize = value
                overrides.frameGenerationFast = nil
            })
    }

    var body: some View {
        Form {
            Section {
                OverridePicker("Internal Resolution", value: $overrides.resolutionFactor,
                               options: Array(1...6)) { $0 == 1 ? "Native (1x)" : "\($0)x" }
                OverridePicker("Texture Filter", value: $overrides.textureFilter,
                               options: TextureFilterOption.allCases.map(\.rawValue)) {
                    TextureFilterOption(rawValue: $0)?.title ?? "\($0)"
                }
                OverridePicker("Anti-Aliasing (MSAA)", value: $overrides.antiAliasing,
                               options: Array(0...3)) { GraphicsSettingsView.antiAliasingName($0) }
                OverridePicker("MetalFX Upscaling", value: $overrides.upscaling,
                               options: UpscalingOption.allCases.map(\.rawValue)) {
                    UpscalingOption(rawValue: $0)?.title ?? "\($0)"
                }
                OverridePicker("MetalFX Scale", value: $overrides.upscaleFactorTenths,
                               options: [0, 15, 20, 25, 30, 35, 40]) {
                    $0 == 0 ? "Fit to screen" : String(format: "%.1fx", Double($0) / 10)
                }
                OverrideToggle("Sharp Pixels", value: $overrides.sharpPixels)
                OverridePicker("Sharpening", value: $overrides.sharpness,
                               options: [0, 25, 50, 75, 100]) { $0 == 0 ? "Off" : "\($0)%" }
                OverridePicker("Sharpening Method", value: $overrides.sharpenMethod,
                               options: [0, 1]) { ["Unsharp Mask", "Contrast Adaptive"][$0] }
                OverridePicker("Scaling Filter", value: $overrides.scaleFilter,
                               options: [0, 1, 2]) { ["Bilinear", "Bicubic", "Lanczos"][$0] }
                OverridePicker("LCD Grid Darkness", value: $overrides.gridStrength,
                               options: [0, 25, 50, 75, 100]) { $0 == 0 ? "Default" : "\($0)%" }
                OverridePicker("Saturation", value: $overrides.saturation,
                               options: [-50, -25, 0, 25, 50]) { $0 == 0 ? "Normal" : "\($0)%" }
                OverridePicker("Contrast", value: $overrides.contrast,
                               options: [-50, -25, 0, 25, 50]) { $0 == 0 ? "Normal" : "\($0)%" }
                OverridePicker("Brightness", value: $overrides.brightness,
                               options: [-50, -25, 0, 25, 50]) { $0 == 0 ? "Normal" : "\($0)%" }
                OverridePicker("Anisotropic Filtering", value: $overrides.anisotropy,
                               options: [1, 2, 4, 8, 16]) { $0 == 1 ? "Off" : "\($0)x" }
                OverrideToggle("Fast Shader Math", value: $overrides.fastShaderMath)
                OverrideToggle("Fast Lighting Math", value: $overrides.fastLighting)
                OverrideToggle("Fast Lighting Lookups", value: $overrides.fastLightingLookups)
                OverrideToggle("Fast Color Math", value: $overrides.fastColor)
                OverrideToggle("Fast Math Functions", value: $overrides.fastMathFunctions)
                OverrideToggle("Half Precision Colors Between Stages", value: $overrides.halfInterpolants)
                OverrideToggle("Untracked Stream Buffers", value: $overrides.untrackedBuffers)
                OverrideToggle("Reuse Shaded Vertices", value: $overrides.vertexReuse)
                    .disabled(!usesHardwareShaders)
                OverrideToggle("Fast Math Functions in Vertex Shaders",
                               value: $overrides.vertexFastFunctions)
                    .disabled(!usesHardwareShaders)
                OverrideToggle("Fast Math in Vertex Shaders (no Accurate Multiplication)",
                               value: $overrides.vertexFastMath)
                    .disabled(!usesHardwareShaders)
                OverrideToggle("Texture Pool", value: $overrides.texturePool)
                OverrideToggle("Realtime Audio", value: $overrides.realtimeAudio)
                OverrideToggle("Fast Depth Readback", value: $overrides.fastDepthReadback)
                OverrideToggle("Wait for GPU Each Frame", value: $overrides.waitForGPU)
                OverrideToggle("Async Shader Compilation", value: $overrides.asyncShaders)
                OverrideToggle("Deterministic Async Operations", value: $overrides.deterministicAsync)
                OverrideToggle("Hold Frame While Shaders Build", value: $overrides.holdFramesForShaders)
                OverridePicker("Wait for Shaders", value: $overrides.shaderWaitMs,
                               options: [0, 1, 2, 4, 8]) { $0 == 0 ? "Never" : "\($0) ms" }
                OverrideToggle("Disable Right Eye Render", value: $overrides.disableRightEyeRender)
                OverrideToggle("Integer Scaling", value: $overrides.integerScaling)
                OverridePicker("External Display", value: $overrides.secondaryDisplayLayout,
                               options: Array(0...7)) { SecondaryLayoutOption.title(for: $0) }
                OverrideToggle("Frame Presentation Skip", value: $overrides.skipDuplicateFrames)
                OverridePicker("Frame Generation", value: $overrides.frameGeneration,
                               options: [0, 1, 2]) {
                    ["Off", "30 FPS Games", "All Games (120 Hz)"][$0]
                }
                OverridePicker("Frame Generation Size", value: frameGenerationSize,
                               options: [5, 0, 4, 1, 2, 3]) { Self.sizeNames[$0] }
                OverrideToggle("Frame Generation: Pause When Behind",
                               value: $overrides.frameGenerationPause)
                OverrideToggle("Frame Generation: Buffer",
                               value: $overrides.frameGenerationBuffer)
                OverrideToggle("Frame Generation: Top Screen Only",
                               value: $overrides.frameGenerationTopOnly)
                OverrideToggle("Frame Generation: Display-Driven Pacing",
                               value: $overrides.frameGenerationDisplayPaced)
                OverrideToggle("Frame Generation With MetalFX (Experimental)",
                               value: $overrides.frameGenerationMetalFX)
                OverrideToggle("Sync Speed to Display", value: $overrides.syncSpeedToDisplay)
                OverrideToggle("Sync to Display Refresh", value: $overrides.displaySync)
                OverridePicker("Display Refresh Rate", value: $overrides.refreshRate,
                               options: RefreshRateOption.allCases.map(\.rawValue)) {
                    RefreshRateOption(rawValue: $0)?.title ?? "\($0) Hz"
                }
                OverrideToggle("30 FPS Frame Cap", value: $overrides.frameCap30)
                OverrideToggle("Frame Cap: Run Game at 30", value: $overrides.frameCapHoldsGame)
                OverridePicker("Frame Pacing Buffer", value: $overrides.pacingBufferMs,
                               options: [0, 8, 17, 25]) { $0 == 0 ? "Off" : "\($0) ms" }
                OverrideToggle("Custom Textures", value: $overrides.customTextures)
                OverrideToggle("Preload Custom Textures", value: $overrides.preloadTextures)
                OverrideToggle("Hidden Surface Removal", value: $overrides.hiddenSurfaceRemoval)
                OverrideToggle("Draw Optimizations", value: $overrides.drawOptimizations)
                OverridePicker("Texture Compression", value: $overrides.textureCompression,
                               options: TextureCompressionOption.allCases.map(\.rawValue)) {
                    TextureCompressionOption(rawValue: $0)?.title ?? "\($0)"
                }
                OverrideToggle("Hardware Shaders", value: $overrides.hardwareShaders)
                OverridePicker("Aspect Ratio", value: $overrides.aspectRatio,
                               options: Array(0...5)) {
                    ["Default", "16:9", "4:3", "21:9", "16:10", "Stretch to Fit"][$0]
                }
                OverridePicker("Texture Sampling", value: $overrides.textureSampling,
                               options: [0, 1, 2]) {
                    ["Game Controlled", "Nearest Neighbor", "Linear"][$0]
                }
                OverridePicker("Post-Processing Effect", value: $overrides.postEffect,
                               options: [0, 1, 2, 3]) {
                    ["None", "Sharpen", "LCD Grid", "Scanlines"][$0]
                }
                OverridePicker("Eye Shown With 3D Off", value: $overrides.monoEye,
                               options: [0, 1]) { $0 == 0 ? "Left" : "Right" }
                OverridePicker("3D Mode", value: $overrides.stereoMode,
                               options: StereoModeOption.allCases.map(\.rawValue)) {
                    StereoModeOption(rawValue: $0)?.title ?? "\($0)"
                }
            } header: {
                Text("Graphics")
            }
            .listRowBackground(LGRowBackground())

            Section {
                OverrideToggle("CPU JIT", value: $overrides.cpuJIT)
                OverrideToggle("Simulate 3DS GPU Timing", value: $overrides.simulateGPUTimings)
                OverridePicker("GPU Timing Strength", value: $overrides.gpuTimingPercent,
                               options: [100, 150, 200, 300, 400, 600]) { "\($0)%" }
                MinimumGPUTimeField(title: "Minimum GPU Time", value: $overrides.gpuTimingMinMs)
                OverridePicker("CPU Clock Speed", value: $overrides.cpuClockPercentage,
                               options: [50, 75, 100, 150, 200, 300, 400]) { "\($0)%" }
                OverridePicker("Fast Forward Speed", value: $overrides.fastForwardSpeed,
                               options: [150, 200, 300, 400, 500, 800, 0]) {
                    $0 == 0 ? "Unlimited" : "\($0)%"
                }
                OverridePicker("Audio Emulation", value: $overrides.audioEmulation,
                               options: [0, 1, 2]) {
                    ["HLE (Fast)", "LLE (Accurate)", "LLE Multithreaded"][$0]
                }
                OverridePicker("Speed Limit", value: $overrides.speedLimit,
                               options: [0, 50, 75, 100, 150, 200, 300]) {
                    $0 == 0 ? "No limit" : "\($0)%"
                }
            } header: {
                Text("Speed")
            }

            .listRowBackground(LGRowBackground())

            Section {
                OverrideToggle("New 3DS Mode", value: $overrides.isNew3DS)
                OverrideToggle("LLE Applets", value: $overrides.lleApplets)
                OverrideToggle("Plugin Loader (3GX)", value: $overrides.pluginLoader)
                OverrideToggle("Nintendo Network Support", value: $overrides.onlineModules)
                OverrideToggle("Region-Free Patch", value: $overrides.regionFreePatch)
                OverridePicker("Region", value: $overrides.region,
                               options: ConsoleRegion.allCases.map(\.rawValue)) {
                    ConsoleRegion(rawValue: $0)?.title ?? "\($0)"
                }
            } header: {
                InfoHeader("System") {
                    Text("Rows set to Default follow the global settings. Opened from the game menu, "
                         + "changes apply to the running game right away where that is possible "
                         + "(layout, speed, graphics options). Settings that change how the game "
                         + "is set up (game hacks, shader options, the region, plugins and the "
                         + "like) apply the next time the game starts.")
                }
            }

            .listRowBackground(LGRowBackground())

            Section {
                ForEach(GameHack.allCases) { hack in
                    Picker(hack.title, selection: Binding(
                        get: { overrides.hacks?[hack.rawValue] ?? 0 },
                        set: { choice in
                            var hacks = overrides.hacks ?? [:]
                            if choice == 0 {
                                hacks[hack.rawValue] = nil
                            } else {
                                hacks[hack.rawValue] = choice
                            }
                            overrides.hacks = hacks.isEmpty ? nil : hacks
                        })) {
                        Text("Built-in List").tag(0)
                        Text("Off").tag(1)
                        Text("On").tag(2)
                    }
                }
            } header: {
                InfoHeader("Game Hacks") {
                    Text(GameHack.allCases.map { "\($0.title): \($0.details)" }
                            .joined(separator: "\n\n"))
                }
            }

            .listRowBackground(LGRowBackground())

            Section {
                Picker("Controller Profile", selection: $overrides.controllerProfile) {
                    Text("Current Mapping").tag(String?.none)
                    ForEach(profiles) { Text($0.name).tag(String?.some($0.id)) }
                }
            } header: {
                InfoHeader("Controller") {
                    Text("Uses a saved controller profile for this game. Create profiles in "
                         + "Settings › Controller Mapping.")
                }
            }

            .listRowBackground(LGRowBackground())

            Section {
                Button("Reset to Global Settings", role: .destructive) {
                    overrides = GameOverrides()
                }
                .disabled(overrides.isEmpty)
            }

            .listRowBackground(LGRowBackground())
        }
        .navigationTitle("Game Settings")
        .lgSettingsStyle()
        .navigationBarTitleDisplayMode(.inline)
        .onAppear { overrides = store.overrides(for: game) }
        .onChange(of: overrides) {
            store.set($0, for: game)
            onChange?()
        }
    }
}

/// A picker that can also fall back to the global setting (nil).
private struct OverridePicker: View {
    let title: String
    @Binding var value: Int?
    let options: [Int]
    let label: (Int) -> String

    init(_ title: String, value: Binding<Int?>, options: [Int], label: @escaping (Int) -> String) {
        self.title = title
        _value = value
        self.options = options
        self.label = label
    }

    var body: some View {
        Picker(title, selection: $value) {
            Text("Default").tag(Int?.none)
            ForEach(options, id: \.self) { option in
                Text(label(option)).tag(Int?.some(option))
            }
        }
    }
}

/// A switch with three states: the global setting, on and off.
private struct OverrideToggle: View {
    let title: String
    @Binding var value: Bool?

    init(_ title: String, value: Binding<Bool?>) {
        self.title = title
        _value = value
    }

    var body: some View {
        Picker(title, selection: $value) {
            Text("Default").tag(Bool?.none)
            Text("On").tag(Bool?.some(true))
            Text("Off").tag(Bool?.some(false))
        }
    }
}

/// A minimum time in milliseconds typed in (to a tenth), for the setting of a game. Empty is the
/// global setting; 0 turns the minimum off. The value is kept in tenths of a millisecond.
struct MinimumGPUTimeField: View {
    let title: String
    @Binding var value: Int?
    @State private var text = ""
    @FocusState private var focused: Bool

    var body: some View {
        HStack {
            Text(title)
            Spacer()
            TextField("Default", text: $text)
                .keyboardType(.decimalPad)
                .multilineTextAlignment(.trailing)
                .frame(maxWidth: 90)
                .focused($focused)
                .onChange(of: focused) { isFocused in
                    if !isFocused { commit() }
                }
                .onSubmit { commit() }
            Text("ms").foregroundStyle(.secondary)
        }
        .onAppear { text = Self.format(value) }
        .toolbar {
            ToolbarItemGroup(placement: .keyboard) {
                Spacer()
                Button("Done") { focused = false }
            }
        }
    }

    private func commit() {
        let cleaned = text.replacingOccurrences(of: ",", with: ".")
            .trimmingCharacters(in: .whitespaces)
        if cleaned.isEmpty {
            value = nil
        } else if let ms = Double(cleaned) {
            value = min(max(Int((ms * 10).rounded()), 0), 600)
        }
        text = Self.format(value)
    }

    private static func format(_ tenths: Int?) -> String {
        guard let tenths else { return "" }
        return tenths % 10 == 0 ? "\(tenths / 10)" : String(format: "%.1f", Double(tenths) / 10)
    }
}
