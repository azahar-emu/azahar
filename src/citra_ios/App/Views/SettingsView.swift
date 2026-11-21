// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

import AzaharCore
import SwiftUI
import UIKit
import UniformTypeIdentifiers

/// Settings, grouped in categories.
struct SettingsView: View {
    var body: some View {
        NavigationStack {
            List {
                Section {
                    NavigationLink { GeneralSettingsView() } label: {
                        Label("General", systemImage: "gearshape")
                    }
                    NavigationLink { GraphicsSettingsView() } label: {
                        Label("Graphics", systemImage: "display")
                    }
                    NavigationLink { LayoutSettingsView() } label: {
                        Label("Layout", systemImage: "rectangle.split.1x2")
                    }
                    NavigationLink { AudioSettingsView() } label: {
                        Label("Audio", systemImage: "speaker.wave.2")
                    }
                    NavigationLink { SystemSettingsView() } label: {
                        Label("System", systemImage: "cpu")
                    }
                    NavigationLink { ControlsSettingsView() } label: {
                        Label("Controls", systemImage: "gamecontroller")
                    }
                    NavigationLink { AppColorsSettingsView() } label: {
                        Label("App Colors", systemImage: "paintpalette")
                    }
                }
                .listRowBackground(LGRowBackground())
                Section {
                    NavigationLink { AboutView() } label: {
                        Label("About", systemImage: "info.circle")
                    }
                }
                .listRowBackground(LGRowBackground())
            }
            .navigationTitle("Settings")
            .lgSettingsStyle()
        }
    }
}

/// What this device's MetalFX offers for frame interpolation (read from the system), for trying
/// MetalFX's own frame interpolation next to the VideoToolbox one.
private struct MetalFXProbeRow: View {
    @State private var report: String?

    var body: some View {
        Button("MetalFX Interpolation Diagnostics") {
            report = AZMetalFXProbe.report()
        }
        if let report {
            Text(report)
                .font(.caption2.monospaced())
                .textSelection(.enabled)
            Button("Copy Report") { UIPasteboard.general.string = report }
        }
    }
}

private struct JITStatusRow: View {
    var body: some View {
        HStack {
            Text("JIT")
            Spacer()
            if AZEmulator.isJITAvailable {
                Label("Enabled", systemImage: "checkmark.circle.fill")
                    .foregroundStyle(.green)
            } else {
                Label("Unavailable", systemImage: "xmark.circle.fill")
                    .foregroundStyle(.orange)
            }
        }
    }
}

struct GeneralSettingsView: View {
    @EnvironmentObject private var settings: AppSettings
    @EnvironmentObject private var library: GameLibrary
    @EnvironmentObject private var compressor: GameCompressor
    @State private var helperReport: String?
    @State private var testingHelper = false
    @State private var importingPairing = false
    @State private var pairingStatus = AZJITHelper.pairingStatus()

    var body: some View {
        Form {
            Section {
                JITStatusRow()
                Toggle("Built-in JIT Helper", isOn: $settings.builtInJIT)
                if settings.builtInJIT {
                    Button(testingHelper ? "Enabling JIT…" : "Enable JIT Now") {
                        testingHelper = true
                        helperReport = nil
                        JITLauncher.enableWithHelper { report in
                            helperReport = report
                            testingHelper = false
                        }
                    }
                    .disabled(testingHelper)
                }
                Toggle("Open Debugger to Enable JIT", isOn: $settings.autoEnableJIT)
                    .disabled(settings.builtInJIT)
                if settings.autoEnableJIT && !settings.builtInJIT {
                    Picker("Debugger App", selection: $settings.jitDebuggerName) {
                        ForEach(JITDebugger.allCases) { Text($0.title).tag($0.rawValue) }
                    }
                    if settings.jitDebugger == .custom {
                        TextField("Link, with {pid} for this app", text: $settings.jitCustomURL)
                            .textInputAutocapitalization(.never)
                            .autocorrectionDisabled()
                            .keyboardType(.URL)
                    }
                    Button("Open Debugger Now") { JITLauncher.open(settings: settings) }
                }
                Button(testingHelper ? "Testing the JIT Helper…" : "Test the JIT Helper") {
                    testingHelper = true
                    helperReport = nil
                    AZJITHelper.test { report in
                        helperReport = report
                        testingHelper = false
                    }
                }
                .disabled(testingHelper)
                LabeledContent("Pairing File", value: pairingStatus)
                Button("Import Pairing File…") { importingPairing = true }
                if pairingStatus == "Imported" {
                    Button("Remove Pairing File", role: .destructive) {
                        AZJITHelper.removePairingFile()
                        pairingStatus = AZJITHelper.pairingStatus()
                    }
                }
                Button("Check the Connection to the Debug Service") {
                    testingHelper = true
                    helperReport = nil
                    AZJITHelper.checkConnection { report in
                        helperReport = report
                        testingHelper = false
                    }
                }
                .disabled(testingHelper)
                if let helperReport {
                    Text(helperReport)
                        .font(.footnote.monospaced())
                        .textSelection(.enabled)
                }
                Toggle("CPU JIT", isOn: $settings.cpuJIT)
                    .disabled(!AZEmulator.isJITAvailable)
                Stepper(value: $settings.cpuClockPercentage, in: 25...400, step: 25) {
                    LabeledContent("CPU Clock", value: "\(settings.cpuClockPercentage)%")
                }
            } header: {
                InfoHeader("CPU") {
                    Text("JIT recompilation requires the app to be launched with JIT enabled, for "
                         + "example through StikDebug or a debugger. Without JIT the much slower "
                         + "interpreter is used. Open Debugger to Enable JIT opens the chosen "
                         + "debugger app when this app starts without JIT, and asks it to attach. "
                         + "The debugger has to be set up already (its pairing file, VPN). The "
                         + "link formats are guesses: if the app doesn't react, use Custom URL "
                         + "with the link from that app's documentation.")
                }
            }
            .listRowBackground(LGRowBackground())

            Section {
                Toggle("Sync Speed to Display", isOn: $settings.syncSpeedToDisplay)
                Toggle("Limit Speed", isOn: $settings.useFrameLimit)
                if settings.useFrameLimit {
                    Stepper(value: $settings.frameLimit, in: 25...400, step: 25) {
                        LabeledContent("Speed Limit", value: "\(settings.frameLimit)%")
                    }
                }
                Picker("Fast Forward Speed", selection: $settings.fastForwardSpeed) {
                    ForEach([150, 200, 300, 400, 500, 800], id: \.self) { Text("\($0)%").tag($0) }
                    Text("Unlimited").tag(0)
                }
                Toggle("Hold to Fast Forward (Controller)", isOn: $settings.fastForwardHold)
                Toggle("Simulate 3DS GPU Timing", isOn: $settings.simulateGPUTimings)
                Stepper(value: $settings.gpuTimingPercent, in: 50...800, step: 25) {
                    LabeledContent("GPU Timing Strength", value: "\(settings.gpuTimingPercent)%")
                }
                .disabled(!settings.simulateGPUTimings)
                MinimumGPUTimeField(title: "Minimum GPU Time",
                                    value: Binding(get: { settings.gpuTimingMinMs },
                                                   set: { settings.gpuTimingMinMs = $0 ?? 0 }))
                    .disabled(!settings.simulateGPUTimings)
            } header: {
                InfoHeader("Speed") {
                    Text("Simulate 3DS GPU Timing makes games see the graphics chip take as long as "
                         + "the real one. Games that adapt their frame rate to it (some run at 60 "
                         + "FPS where there is little to draw and drop to 30 in heavy scenes) then "
                         + "run at the frame rate they have on a console; GPU Timing Strength sets "
                         + "how slow that GPU is (the estimate is twice as fast as a console, so "
                         + "about 200% is the console; raise it if a game still runs at 60 FPS "
                         + "where the console would run 30). Minimum GPU Time is steadier than the "
                         + "estimate: the game never sees a frame's 3D work finish sooner than that "
                         + "after the vertical blank (17 ms and up is more than one 60 FPS refresh), so "
                         + "a game that switches between 30 and 60 holds 30 (try 20 ms; raise it if "
                         + "it goes below 30, lower it if it never reaches 30); without it the "
                         + "emulated GPU is instant and they can run at 60 FPS everywhere. Fast Forward is in the game menu, and on a controller button in Controls › "
                         + "Controller Mapping. With Hold on, the controller button only fast-forwards "
                         + "while it is held.")
                }
            }

            .listRowBackground(LGRowBackground())

            Section("Library") {
                Picker("Library Style", selection: $settings.libraryStyle) {
                    ForEach(LibraryStyle.allCases) { Text($0.title).tag($0) }
                }
            }

            .listRowBackground(LGRowBackground())

            Section {
                Toggle("Keep Original Files", isOn: $settings.keepOriginalsWhenCompressing)
                Button("Compress Games…") {
                    DocumentImporter.present { urls in
                        compressor.compress(urls, deleteSources: false, library: library)
                    }
                }
                .disabled(compressor.isRunning)
                Button("Compress the Games in the Library") {
                    compressor.compress(GameCompressor.compressibleFilesInLibrary(),
                                        deleteSources: !settings.keepOriginalsWhenCompressing,
                                        library: library)
                }
                .disabled(compressor.isRunning)
                if compressor.isRunning, let name = compressor.currentName {
                    VStack(alignment: .leading, spacing: 6) {
                        Text("\(compressor.position) of \(compressor.total): \(name)")
                            .font(.caption)
                            .lineLimit(1)
                            .truncationMode(.middle)
                        ProgressView(value: compressor.progress)
                    }
                }
                Button("Show Games Folder in Files") {
                    FileLocations.showInFiles(FileLocations.romsDirectory)
                }
            } header: {
                InfoHeader("Compression") {
                    Text("Compressed games (.zcci, .zcxi, .z3dsx and .zcia) are smaller and play the "
                         + "same. They are written to the games folder (Files › ROMs). Compress the "
                         + "Games… picks files to compress, the originals you pick are left where "
                         + "they are; the library button compresses the games in the games folder "
                         + "and removes their originals unless Keep Original Files is on. A .zcia "
                         + "installs like a .cia. Encrypted and bundled games cannot be compressed.")
                }
            }

            .listRowBackground(LGRowBackground())
        }
        .fileImporter(isPresented: $importingPairing,
                      allowedContentTypes: [.propertyList, .data, .item]) { result in
            guard case .success(let url) = result else { return }
            if let problem = AZJITHelper.importPairingFile(at: url) {
                helperReport = problem
            } else {
                helperReport = "Pairing file imported."
            }
            pairingStatus = AZJITHelper.pairingStatus()
        }
        .navigationTitle("General")
        .lgSettingsStyle()
    }
}

struct GraphicsSettingsView: View {
    @EnvironmentObject private var settings: AppSettings
    @State private var postShaders = PostShaders.availableNames()
    @State private var clearedShaderCache = false

    /// Built in steps: one long concatenation is too much for the type checker.
    private static var enhancementsFooter: String {
        var text = ""
        text += "Texture filters smooth the textures of games when Internal Resolution is above "
        text += "1x (xBRZ and MMPX suit pixel art, Bicubic and ScaleForce are gentler). "
        text += "Lower anisotropic filtering and Fast Shader Math make games run faster. "
        text += "Fast Shader Math can cause small graphical differences in some games. "
        text += "Fast Lighting Math computes lighting in half precision, which helps games "
        text += "with a lot of lighting (Luigi's Mansion) and can shift highlights slightly. "
        text += "Fast Lighting Lookups reads the lighting tables with the GPU's texture "
        text += "filtering, one step per lookup instead of several; the ends of the tables are "
        text += "held instead of extended, which can change the brightest highlights a little. "
        text += "Fast Math Functions uses faster versions of functions like normalize and "
        text += "pow (iOS 18), without the NaN assumptions of Fast Shader Math; it can be on "
        text += "together with it. Half Precision Colors Between "
        text += "Stages passes the vertex color to the pixel shader as half precision, "
        text += "which saves a little bandwidth. Untracked Stream Buffers stops Metal "
        text += "from tracking the buffers the emulator already keeps in order itself, "
        text += "which saves CPU time in games with many draws (turn it off if a game "
        text += "flickers). Reuse Shaded Vertices draws the models the GPU shades without "
        text += "lighting natively, so a vertex shared by several triangles is shaded once "
        text += "and not once for every triangle (it can make GPU-shaded vertices several "
        text += "times cheaper; models with lighting are not affected; turn it off if a game "
        text += "shows broken models). Fast Math Functions in Vertex Shaders uses the faster, "
        text += "less exact versions of functions like reciprocal and square root in the "
        text += "vertex programs (iOS 18); it can make vertices a little cheaper but may "
        text += "leave tiny cracks between models (turn it off if you see them). Fast Math in "
        text += "Vertex Shaders goes further (the compiler may assume values are never NaN or "
        text += "infinite) and turns Accurate Multiplication off, which it cannot work with; "
        text += "it can break models in games that depend on that (exploding or missing "
        text += "geometry). Texture Pool keeps the textures of surfaces a game stopped using "
        text += "(up to 192 MB) and gives them to the next surface of the same size and "
        text += "format, instead of creating new ones, which helps games that stream a lot of "
        text += "assets (turn it off if a game shows leftover pictures). They apply when a "
        text += "game starts. "
        text += "Fast Color Math does the color combiners in half precision, the same "
        text += "8-bit result in practice. Hidden Surface Removal lets the GPU skip "
        text += "pixels hidden behind others (not for games using W-buffering). Texture "
        text += "Compression saves GPU memory bandwidth: Lossless looks identical, Lossy "
        text += "(A15 and later) saves more with a tiny loss of quality. "
        text += "Fast Depth Readback answers the small depth checks some games do every "
        text += "frame (Mario Kart 7's sun glare) with the previous frame's values instead "
        text += "of waiting for the GPU. "
        text += "Draw Optimizations draws depth-only geometry (like shadow volumes) without a "
        text += "pixel shader, does the scissor test with the GPU's own scissor, drops "
        text += "blending that changes nothing and draws clears of part of the screen inside the "
        text += "game's own render passes; turn it off if a game shows glitches it did not "
        text += "have before. "
        text += "All of these apply the next time a game starts."
        return text
    }

    @ViewBuilder private var rendererSections: some View {
            Section {
                LabeledContent("Renderer", value: "Metal")
                Toggle("Hardware Shaders", isOn: $settings.hardwareShaders)
                Toggle("Accurate Multiplication", isOn: $settings.accurateMultiplication)
                    .disabled(settings.vertexFastMath)
                    .disabled(!settings.hardwareShaders)
                Toggle("Shader JIT", isOn: $settings.shaderJIT)
                    .disabled(!AZEmulator.isJITAvailable)
            } header: {
                InfoHeader("Renderer") {
                    Text("Azahar renders natively with Metal. Hardware Shaders run the game's vertex "
                         + "shaders on the GPU. Draws that can't be translated, and games that use "
                         + "geometry shaders, still run on the CPU, where the shader JIT speeds them "
                         + "up. Accurate Multiplication fixes graphical glitches in some games at a "
                         + "small GPU cost.")
                }
            }
            .listRowBackground(LGRowBackground())

            Section {
                Picker("Internal Resolution", selection: $settings.resolutionFactor) {
                    ForEach(1...6, id: \.self) { factor in
                        Text(factor == 1 ? "Native (1x)" : "\(factor)x").tag(factor)
                    }
                }
                Toggle("Linear Screen Filtering", isOn: $settings.linearFiltering)
                Picker("Texture Sampling", selection: $settings.textureSampling) {
                    Text("Game Controlled").tag(0)
                    Text("Nearest Neighbor").tag(1)
                    Text("Linear").tag(2)
                }
                Picker("Texture Filter", selection: $settings.textureFilter) {
                    ForEach(TextureFilterOption.allCases) { Text($0.title).tag($0) }
                }
                Picker("Anisotropic Filtering", selection: $settings.anisotropy) {
                    Text("Off").tag(1)
                    ForEach([2, 4, 8, 16], id: \.self) { Text("\($0)x").tag($0) }
                }
                Toggle("Fast Shader Math", isOn: $settings.fastShaderMath)
                Toggle("Fast Lighting Math", isOn: $settings.fastLighting)
                Toggle("Fast Lighting Lookups", isOn: $settings.fastLightingLookups)
                Toggle("Fast Color Math", isOn: $settings.fastColor)
                Toggle("Fast Math Functions", isOn: $settings.fastMathFunctions)
                Toggle("Half Precision Colors Between Stages", isOn: $settings.halfInterpolants)
                Toggle("Untracked Stream Buffers", isOn: $settings.untrackedBuffers)
                Toggle("Reuse Shaded Vertices", isOn: $settings.vertexReuse)
                    .disabled(!settings.hardwareShaders)
                Toggle("Fast Math Functions in Vertex Shaders", isOn: $settings.vertexFastFunctions)
                    .disabled(!settings.hardwareShaders)
                Toggle("Fast Math in Vertex Shaders", isOn: $settings.vertexFastMath)
                    .disabled(!settings.hardwareShaders)
                Toggle("Texture Pool", isOn: $settings.texturePool)
                Toggle("Fast Depth Readback", isOn: $settings.fastDepthReadback)
                Toggle("Disable Right Eye Render", isOn: $settings.disableRightEyeRender)
                Toggle("Hidden Surface Removal", isOn: $settings.hiddenSurfaceRemoval)
                Toggle("Draw Optimizations", isOn: $settings.drawOptimizations)
                Picker("Texture Compression", selection: $settings.textureCompression) {
                    ForEach(TextureCompressionOption.allCases) { Text($0.title).tag($0) }
                }
            } header: {
                InfoHeader("Enhancements") {
                    Text(verbatim: Self.enhancementsFooter)
                }
            }

            .listRowBackground(LGRowBackground())

            Section {
                Toggle("Use External Display", isOn: $settings.useExternalDisplay)
                Picker("External Display Shows", selection: $settings.secondaryDisplayLayout) {
                    ForEach(0...7, id: \.self) {
                        Text(SecondaryLayoutOption.title(for: $0)).tag($0)
                    }
                }
                .disabled(!settings.useExternalDisplay)
            } header: {
                InfoHeader("External Display") {
                    Text("With a TV or AirPlay display connected, the default shows the top screen on "
                         + "it and this device the bottom screen with the controls. The other choices "
                         + "give the display the screen layout you pick while this device keeps its "
                         + "own layout. Takes effect when a game starts. "
                         + "Running inside another app (such as LiveContainer) may stop iOS from "
                         + "offering the display to Azahar.")
                }
            }

            .listRowBackground(LGRowBackground())
    }

    @ViewBuilder private var enhancementSections: some View {
            Section {
                Toggle("Custom Textures", isOn: $settings.customTextures)
                Toggle("Preload Custom Textures", isOn: $settings.preloadTextures)
                    .disabled(!settings.customTextures)
                Toggle("Load Custom Textures in the Background", isOn: $settings.asyncCustomTextures)
                    .disabled(!settings.customTextures)
                Toggle("Dump Textures", isOn: $settings.dumpTextures)
            } header: {
                InfoHeader("Texture Packs") {
                    Text("Custom Textures replaces the textures of a game with the images in Files › "
                         + "Azahar › load › textures › the game's title ID. Preloading loads them all "
                         + "when the game starts (more memory, no stutter). Dump Textures saves the "
                         + "textures a game uses to Files › Azahar › dump › textures, to make a pack. "
                         + "Applies the next time a game starts.")
                }
            }
            .listRowBackground(LGRowBackground())

            Section {
                Picker("Outer Cameras", selection: $settings.outerCamera) {
                    ForEach(CameraSource.allCases) { Text($0.title).tag($0) }
                }
                Picker("Inner Camera", selection: $settings.innerCamera) {
                    ForEach(CameraSource.allCases) { Text($0.title).tag($0) }
                }
            } header: {
                InfoHeader("Cameras") {
                    Text("Choose which camera of this device a camera of the 3DS shows. Off shows a "
                         + "black picture. The first time a game uses a camera, iOS asks for "
                         + "permission. Applies the next time a game starts.")
                }
            }

            .listRowBackground(LGRowBackground())

            Section {
                Picker("3D Mode", selection: $settings.stereoMode) {
                    ForEach(StereoModeOption.allCases) { Text($0.title).tag($0) }
                }
                if settings.stereoMode != .off {
                    VStack(alignment: .leading) {
                        Text("Depth: \(settings.stereoDepth)%")
                        Slider(value: Binding(
                            get: { Double(settings.stereoDepth) },
                            set: { settings.stereoDepth = Int($0.rounded()) }), in: 0...100)
                    }
                    Toggle("Swap Eyes", isOn: $settings.swapEyes3D)
                }
                if settings.stereoMode == .off {
                    Picker("Eye Shown", selection: $settings.monoEye) {
                        Text("Left").tag(0)
                        Text("Right").tag(1)
                    }
                }
                if settings.stereoMode == .anaglyph {
                    Picker("Anaglyph Colors", selection: $settings.anaglyphMode) {
                        Text("Dubois").tag(0)
                        Text("Gray").tag(1)
                        Text("Half Color").tag(2)
                        Text("Color").tag(3)
                    }
                }
                if settings.stereoMode == .cardboard {
                    Stepper("Cardboard Screen Size: \(settings.cardboardSize)%",
                            value: $settings.cardboardSize, in: 30...100, step: 5)
                    Stepper("Cardboard Horizontal Shift: \(settings.cardboardXShift)",
                            value: $settings.cardboardXShift, in: -100...100, step: 5)
                    Stepper("Cardboard Vertical Shift: \(settings.cardboardYShift)",
                            value: $settings.cardboardYShift, in: -100...100, step: 5)
                }
            } header: {
                InfoHeader("3D") {
                    Text("Anaglyph needs red/cyan glasses. Interlaced shows the eyes on alternate rows "
                         + "of the screen. Side by Side and Cardboard VR show one picture per eye, for "
                         + "a headset or glasses holder (landscape). These modes don't use MetalFX.")
                }
            }

            .listRowBackground(LGRowBackground())

            Section {
                Picker("Upscaling", selection: $settings.upscaling) {
                    ForEach(UpscalingOption.allCases) { Text($0.title).tag($0) }
                }
                .disabled(!AZEmulator.isUpscalingSupported)
                if settings.upscaling != .off {
                    VStack(alignment: .leading) {
                        Text("Scale: " + (settings.upscaleFactorTenths == 0
                             ? "Fit to screen"
                             : String(format: "%.1fx", Double(settings.upscaleFactorTenths) / 10)))
                        // The last step (4.5) means "fit to screen".
                        Slider(value: Binding(
                            get: {
                                settings.upscaleFactorTenths == 0
                                    ? 4.5 : Double(settings.upscaleFactorTenths) / 10
                            },
                            set: { settings.upscaleFactorTenths = $0 >= 4.5 ? 0 : Int(($0 * 10).rounded()) }),
                            in: 1.0...4.5, step: 0.5)
                    }
                    .disabled(!AZEmulator.isUpscalingSupported)
                }
            } header: {
                InfoHeader("MetalFX") {
                    if AZEmulator.isUpscalingSupported {
                        Text("MetalFX Spatial scales each screen up to the size it is shown at with "
                             + "Apple's GPU upscaler, which looks sharper than plain filtering and "
                             + "lets you use a lower Internal Resolution (1x or 2x) for the same "
                             + "picture. It applies to the next frame, you do not need to restart. "
                             + "The scale sets how far it enlarges a screen (1.5x, 2x, ...); the "
                             + "result is then stretched to the size on the display. A lower scale "
                             + "costs less GPU time, \"Fit to screen\" goes straight to the "
                             + "display size. Scales that would not enlarge the screen by at least "
                             + "a quarter use plain filtering.")
                    } else {
                        Text("MetalFX is not available on this device.")
                    }
                }
            }

            .listRowBackground(LGRowBackground())
    }

    @ViewBuilder private var shaderSections: some View {
            Section {
                Picker("Effect", selection: $settings.postEffect) {
                    Text("None").tag(0)
                    Text("Sharpen").tag(1)
                    Text("LCD Grid").tag(2)
                    Text("Scanlines").tag(3)
                }
                Toggle("Sharp Pixels", isOn: $settings.sharpPixels)
                VStack(alignment: .leading) {
                    Text("Sharpening: " + (settings.sharpness == 0 ? "Off" : "\(settings.sharpness)%"))
                    Slider(value: Binding(
                        get: { Double(settings.sharpness) },
                        set: { settings.sharpness = Int(($0 / 5).rounded()) * 5 }),
                        in: 0...100, step: 5)
                }
                Picker("Sharpening Method", selection: $settings.sharpenMethod) {
                    Text("Unsharp Mask").tag(0)
                    Text("Contrast Adaptive").tag(1)
                }
                Picker("Scaling Filter", selection: $settings.scaleFilter) {
                    Text("Bilinear").tag(0)
                    Text("Bicubic").tag(1)
                    Text("Lanczos").tag(2)
                }
                .disabled(settings.sharpPixels)
                VStack(alignment: .leading) {
                    Text("LCD Grid Darkness: " + (settings.gridStrength == 0 ? "Default" : "\(settings.gridStrength)%"))
                    Slider(value: Binding(
                        get: { Double(settings.gridStrength) },
                        set: { settings.gridStrength = Int(($0 / 5).rounded()) * 5 }),
                        in: 0...100, step: 5)
                }
                .disabled(settings.postEffect != 2)
                VStack(alignment: .leading) {
                    Text("Saturation: \(settings.saturation)%")
                    Slider(value: Binding(
                        get: { Double(settings.saturation) },
                        set: { settings.saturation = Int(($0 / 5).rounded()) * 5 }),
                        in: -100...100, step: 5)
                    Text("Contrast: \(settings.contrast)%")
                    Slider(value: Binding(
                        get: { Double(settings.contrast) },
                        set: { settings.contrast = Int(($0 / 5).rounded()) * 5 }),
                        in: -100...100, step: 5)
                    Text("Brightness: \(settings.brightness)%")
                    Slider(value: Binding(
                        get: { Double(settings.brightness) },
                        set: { settings.brightness = Int(($0 / 5).rounded()) * 5 }),
                        in: -100...100, step: 5)
                }
                Picker("Custom Shader", selection: $settings.postShader) {
                    Text("None").tag("")
                    ForEach(postShaders, id: \.self) { Text($0).tag($0) }
                }
                Button("Show Shaders Folder in Files") {
                    _ = PostShaders.availableNames()
                    FileLocations.showInFiles(FileLocations.postShadersDirectory)
                }
            } header: {
                InfoHeader("Post-Processing") {
                    Text("Sharp Pixels keeps the pixels of a screen crisp when it is enlarged (only "
                         + "the edge of a pixel is smoothed). Sharpening adds contrast at edges, "
                         + "after MetalFX, and can be combined with an effect. Contrast Adaptive "
                         + "sharpening only sharpens where the picture is soft and keeps edges "
                         + "free of halos. The Scaling Filter sets how a screen is enlarged "
                         + "(Bicubic and Lanczos keep edges crisper than Bilinear; Sharp Pixels "
                         + "takes its place). LCD Grid Darkness is how dark the gaps of the LCD "
                         + "Grid effect are, Saturation, Contrast and Brightness adjust the "
                         + "colors. "
                         + "An effect is applied to the finished screens. A custom shader replaces it: "
                         + "put a .metal file with a post_effect function in Files › Azahar › shaders "
                         + "› post (Example.metal shows how). A shader that does not compile is "
                         + "ignored, the log tells why. Applies when a game starts.")
                }
            }
            .listRowBackground(LGRowBackground())

            Section {
                Toggle("Sync to Display Refresh", isOn: $settings.displaySync)
                if settings.displaySync {
                    Toggle("30 FPS Frame Cap", isOn: $settings.frameCap30)
                    if settings.frameCap30 {
                        Toggle("Run Game at 30", isOn: $settings.frameCapHoldsGame)
                    }
                }
                // The older pacing, only used when Sync to Display Refresh is off.
                if !settings.displaySync {
                    Toggle("Frame Pacing", isOn: $settings.framePacing)
                    Picker("Frame Pacing Buffer", selection: $settings.pacingBufferMs) {
                        Text("Off").tag(0)
                        Text("8 ms").tag(8)
                        Text("17 ms").tag(17)
                        Text("25 ms").tag(25)
                    }
                    .disabled(!settings.framePacing)
                }
                Picker("Display Refresh Rate", selection: $settings.refreshRate) {
                    ForEach(RefreshRateOption.allCases) { Text($0.title).tag($0) }
                }
            } header: {
                InfoHeader("Smoothness") {
                    Text("Frame Pacing shows each frame for the same amount of time, which makes motion "
                         + "look smoother, most on displays that refresh faster than the game. It does "
                         + "not apply while fast-forwarding. Display Refresh Rate asks ProMotion displays to run at "
                         + "60 Hz (saves power, one refresh per game frame) or up to 120 Hz; games run "
                         + "at 60 frames per second either way. iOS may ignore the request, especially "
                         + "when the app runs inside another app. The performance overlay's Pace value is "
                         + "how much the time between frames varies (lower is smoother). 30 FPS Frame "
                         + "Cap (with Sync to Display Refresh) shows every new frame of the game for "
                         + "exactly two 3DS refreshes, for games that run at 30 FPS but hand their "
                         + "frames over unevenly (for example with Minimum GPU Time); a game running "
                         + "faster is held to 30. Run Game at 30 also makes the game itself run "
                         + "at 30 (its 3D work never finishes within one refresh), so it hands "
                         + "over frames evenly and does not draw frames that would be thrown "
                         + "away; it works with or without Simulate 3DS GPU Timing. Turn it off "
                         + "for a game that slows down instead of dropping to 30. The cap is best "
                         + "set per game. Applies when a game starts.")
                }
            }

            .listRowBackground(LGRowBackground())

            Section {
                Toggle("Async Shader Compilation", isOn: $settings.asyncShaders)
                Toggle("Hold Frame While Shaders Build", isOn: $settings.holdFramesForShaders)
                    .disabled(!settings.asyncShaders)
                Picker("Wait for Shaders", selection: $settings.shaderWaitMs) {
                    Text("Never").tag(0)
                    ForEach([1, 2, 4, 8], id: \.self) { Text("\($0) ms").tag($0) }
                }
                .disabled(!settings.asyncShaders)
                Toggle("Disk Shader Cache", isOn: $settings.diskShaderCache)
                Button("Clear Shader Cache", role: .destructive) {
                    clearedShaderCache = FileLocations.clearShaderCache()
                }
            } header: {
                InfoHeader("Shaders") {
                    Text("Async Shader Compilation builds new shaders in the background instead of "
                         + "pausing the game, an object may be missing for a moment instead. Hold "
                         + "Frame keeps showing the previous frame (up to three in a row) while a "
                         + "shader is still building, so nothing flickers; the picture repeats "
                         + "for a moment instead. Wait for Shaders lets a draw wait a few milliseconds "
                         + "for its shader first (at most four times that per frame), which avoids "
                         + "most skipped draws for a tiny hitch. Only frames with a hole in the "
                         + "screen-sized picture are held. The disk "
                         + "shader cache remembers the shaders of each game and builds them while it "
                         + "boots, so they don't need to be built again during play.")
                }
            }

            .listRowBackground(LGRowBackground())
            .alert("Shader Cache Cleared", isPresented: $clearedShaderCache) {
                Button("OK", role: .cancel) {}
            }
    }

    /// Built in steps: one long concatenation with a condition in it is too much for the type
    /// checker.
    private static var performanceFooter: String {
        var text: String = "Frame Generation makes the frames between the real ones with Apple's "
        text += "frame interpolation, so a 30 FPS game looks like 60. Games that already run "
        text += "at 60 FPS are left alone unless Also 60 FPS Games is on. "
        if !FrameGenerator.isSupported {
            text += "Not available on this device or iOS version. "
        }
        text += "It adds about half a frame of delay, can smear text and the 3D depth, and uses "
        text += "more GPU and battery. It turns on Frame Presentation Skip. Frame Generation Size "
        text += "is the size the in-between frames are made at (the real frames are always full "
        text += "size): Full is sharpest and slowest, each step down is less work and heat with "
        text += "softer in-between frames, for slower devices. It stays at the size you pick: if "
        text += "the device can't make frames that size, frame generation stays off. Adaptive "
        text += "starts at Half and goes down to Quarter while the in-between frames come too late "
        text += "(heavy scenes such as water), then back up when there is room again (a size "
        text += "that was too slow is not tried again for 30 seconds, and a smaller size that "
        text += "does not make the frames faster is undone). Frame Buffer keeps one more frame "
        text += "waiting before showing them, so a frame that comes a little late still makes "
        text += "its refresh: a steadier frame rate (most noticeable at 120 Hz) for one refresh "
        text += "more input latency. Pause When "
        text += "Behind shows the game's own frames, a steady frame rate without in-between "
        text += "frames, only while made frames are actually being lost (several a second) and "
        text += "no smaller size helps, and tries frame generation again after 5 seconds (longer "
        text += "when it has to pause again soon after). Top Screen "
        text += "Only leaves "
        text += "the bottom screen at the game's own frame rate, which halves the work in most "
        text += "layouts. Display-Driven Pacing (iOS 17 or later, experimental) lets the display "
        text += "pick up each frame at its own refresh instead of timing the frames in advance, "
        text += "which should keep them even at 60 Hz too. Also 60 FPS Games makes 60 FPS games "
        text += "show 120 frames per second; it needs Display-Driven Pacing and the display at "
        text += "120 Hz, adds about 8 ms of delay and twice the heat.\n\n"
        text += "Frame Presentation Skip does not present a frame the game did not draw again, so "
        text += "a 30 FPS game shows 30 frames per second instead of every frame twice at 60. "
        text += "The emulation speed and the audio stay the same. Meant for frame generation and "
        text += "interpolation tools that need the real frame rate."
        return text
    }

    @ViewBuilder private var performanceSections: some View {
            Section {
                Toggle("Frame Presentation Skip", isOn: $settings.skipDuplicateFrames)
                Toggle("Frame Generation (Experimental)", isOn: Binding(
                    get: { settings.frameGeneration > 0 },
                    set: { settings.frameGeneration = $0 ? 1 : 0 }))
                Picker("Frame Generation Size", selection: $settings.frameGenerationSize) {
                    Text("Adaptive").tag(5)
                    Text("Full").tag(0)
                    Text("3/4").tag(4)
                    Text("Half").tag(1)
                    Text("3/8").tag(2)
                    Text("Quarter").tag(3)
                }
                .disabled(settings.frameGeneration == 0)
                Toggle("Pause When Behind", isOn: $settings.frameGenerationPause)
                    .disabled(settings.frameGeneration == 0)
                Toggle("Frame Buffer", isOn: $settings.frameGenerationBuffer)
                    .disabled(settings.frameGeneration == 0)
                Toggle("Use MetalFX Interpolation (Experimental)",
                       isOn: $settings.frameGenerationMetalFX)
                    .disabled(settings.frameGeneration == 0)
                Toggle("Top Screen Only", isOn: $settings.frameGenerationTopOnly)
                    .disabled(settings.frameGeneration == 0)
                Toggle("Display-Driven Pacing", isOn: $settings.frameGenerationDisplayPaced)
                    .disabled(settings.frameGeneration == 0)
                Toggle("Also 60 FPS Games (120 Hz)", isOn: $settings.frameGenerationAllGames)
                    .disabled(settings.frameGeneration == 0 || !settings.frameGenerationDisplayPaced)
                .disabled(!FrameGenerator.isSupported)
                MetalFXProbeRow()
                Toggle("Show Performance Overlay", isOn: $settings.showPerformanceOverlay)
            } header: {
                InfoHeader("Performance") {
                    Text(verbatim: Self.performanceFooter)
                }
            }
            .listRowBackground(LGRowBackground())
    }

    var body: some View {
        Form {
            rendererSections
            enhancementSections
            shaderSections
            performanceSections
        }
        .navigationTitle("Graphics")
        .lgSettingsStyle()
    }
}

struct LayoutSettingsView: View {
    @EnvironmentObject private var settings: AppSettings
    @State private var savingPreset = false
    @State private var presetName = ""

    var body: some View {
        Form {
            Section {
                Picker("Screen Layout", selection: $settings.layout) {
                    ForEach(ScreenLayout.allCases) { Text($0.title).tag($0) }
                }
            } header: {
                InfoHeader("Landscape") {
                    if settings.layout == .custom {
                        Text("Place the screens with Edit Screen Layout in the menu while a game runs.")
                    }
                }
            }
            .listRowBackground(LGRowBackground())
            Section {
                Picker("Screen Layout", selection: $settings.portraitLayout) {
                    ForEach(PortraitScreenLayout.allCases) { Text($0.title).tag($0) }
                }
            } header: {
                InfoHeader("Portrait") {
                    if settings.portraitLayout == .custom {
                        Text("Place the screens with Edit Screen Layout in the menu while a game runs.")
                    }
                }
            }
            .listRowBackground(LGRowBackground())
            Section {
                Toggle("Swap Screens", isOn: $settings.swapScreens)
                Toggle("Upright Screens", isOn: $settings.uprightScreens)
                Stepper("Screen Gap: \(settings.screenGap)", value: $settings.screenGap,
                        in: 0...200, step: 5)
                if settings.layout == .largeScreen {
                    VStack(alignment: .leading) {
                        Text(String(format: "Large Screen Size: %.1fx", settings.largeScreenProportion))
                        Slider(value: $settings.largeScreenProportion, in: 1...16, step: 0.5)
                    }
                    Picker("Small Screen Position", selection: $settings.smallScreenPlacement) {
                        ForEach(SmallScreenPlacement.allCases) { Text($0.title).tag($0) }
                    }
                }
            } header: {
                InfoHeader {
                    EmptyView()
                } info: {
                    Text("Upright Screens turns the screens on their side. Screen Gap applies to the "
                         + "layouts that have a gap, the large screen options to the Large Screen "
                         + "layout.")
                }
            }
            .listRowBackground(LGRowBackground())
            Section {
                Toggle("Integer Scaling", isOn: $settings.integerScaling)
                Picker("Aspect Ratio", selection: $settings.aspectRatio) {
                    Text("Default").tag(0)
                    Text("16:9").tag(1)
                    Text("4:3").tag(2)
                    Text("21:9").tag(3)
                    Text("16:10").tag(4)
                    Text("Stretch to Fit").tag(5)
                }
                Toggle("Stretch Top Screen", isOn: $settings.topStretch)
                Toggle("Stretch Bottom Screen", isOn: $settings.bottomStretch)
                if settings.topStretch {
                    Stepper("Top Screen Side Padding: \(settings.topPaddingH)",
                            value: $settings.topPaddingH, in: 0...400, step: 10)
                    Stepper("Top Screen Top/Bottom Padding: \(settings.topPaddingV)",
                            value: $settings.topPaddingV, in: 0...400, step: 10)
                }
                if settings.bottomStretch {
                    Stepper("Bottom Screen Side Padding: \(settings.bottomPaddingH)",
                            value: $settings.bottomPaddingH, in: 0...400, step: 10)
                    Stepper("Bottom Screen Top/Bottom Padding: \(settings.bottomPaddingV)",
                            value: $settings.bottomPaddingV, in: 0...400, step: 10)
                }
            } header: {
                InfoHeader("Shape") {
                    Text("Aspect Ratio changes the shape the screens are drawn in. Stretching fills the "
                         + "window with a screen apart from the padding, in pixels. Applies to the "
                         + "layouts that do not place the screens by hand.")
                }
            }
            .listRowBackground(LGRowBackground())
            Section {
                ForEach(settings.layoutPresets) { preset in
                    Button {
                        settings.applyLayoutPreset(preset)
                    } label: {
                        Text(preset.name).foregroundStyle(.primary)
                    }
                }
                .onDelete { settings.layoutPresets.remove(atOffsets: $0) }
                Button("Save Current Layout…") {
                    presetName = ""
                    savingPreset = true
                }
            } header: {
                InfoHeader("Saved Layouts") {
                    Text("Tap a saved layout to use it, swipe to delete one. In a game, Next Screen "
                         + "Layout in the menu (or a controller shortcut) cycles through them, and "
                         + "Save Screen Layout saves what you set up with Edit Screen Layout.")
                }
            }
            .listRowBackground(LGRowBackground())
            Section {
                Toggle("Respect Notch", isOn: $settings.respectNotch)
            } header: {
                InfoHeader {
                    EmptyView()
                } info: {
                    Text("Keeps the screens clear of the notch, the Dynamic Island and the rounded "
                         + "corners. Turn it off to draw the game edge to edge.")
                }
            }
            .listRowBackground(LGRowBackground())
        }
        .navigationTitle("Layout")
        .lgSettingsStyle()
        .alert("Save Layout", isPresented: $savingPreset) {
            TextField("Name", text: $presetName)
            Button("Save") { settings.saveLayoutPreset(named: presetName) }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("Saves the current screen layout. A layout with the same name is replaced.")
        }
    }
}

struct AudioSettingsView: View {
    @EnvironmentObject private var settings: AppSettings

    var body: some View {
        Form {
            Section {
                VStack(alignment: .leading) {
                    LabeledContent("Volume", value: "\(Int(settings.volume * 100))%")
                    Slider(value: $settings.volume, in: 0...1)
                }
                Toggle("Audio Stretching", isOn: $settings.audioStretching)
                Picker("Audio Emulation", selection: $settings.audioEmulation) {
                    Text("HLE (Fast)").tag(0)
                    Text("LLE (Accurate)").tag(1)
                    Text("LLE Multithreaded").tag(2)
                }
                Toggle("Realtime Audio", isOn: $settings.realtimeAudio)
                Toggle("Use Microphone", isOn: $settings.useMicrophone)
            } header: {
                InfoHeader {
                    EmptyView()
                } info: {
                    Text("Audio stretching reduces stuttering when the emulation runs below full speed. "
                         + "HLE is the fast built-in audio. LLE runs the console's own audio "
                         + "processor, which is more accurate for games with audio glitches but "
                         + "slower (the multithreaded form runs it on its own thread) and needs the "
                         + "console's system files. Realtime Audio keeps the sound at its normal speed while a game runs "
                         + "slower than full speed, where it would slow down with the game; it can "
                         + "drift out of step with the picture in games that sync to it. Use "
                         + "Microphone lets games listen to the microphone of this device (it asks "
                         + "for permission when a game starts). Without it games hear static noise. "
                         + "Applies when a game starts.")
                }
            }
            .listRowBackground(LGRowBackground())
        }
        .navigationTitle("Audio")
        .lgSettingsStyle()
    }
}

struct SystemSettingsView: View {
    @EnvironmentObject private var settings: AppSettings
    @State private var storageName = CustomStorage.currentURL?.lastPathComponent

    var body: some View {
        Form {
            Section {
                NavigationLink("Console Profile") { ConsoleProfileView() }
                Toggle("New 3DS Mode", isOn: $settings.isNew3DS)
                Picker("Region", selection: $settings.region) {
                    ForEach(ConsoleRegion.allCases) { Text($0.title).tag($0) }
                }
            } header: {
                InfoHeader {
                    EmptyView()
                } info: {
                    Text("System settings take effect the next time a game is started. Leave Region on "
                         + "Auto-select unless a game needs a specific one: a region that does not "
                         + "match the game (for example USA with a European copy) can stop it from "
                         + "booting.")
                }
            }
            .listRowBackground(LGRowBackground())

            Section {
                Toggle("Fixed Clock", isOn: $settings.fixedClock)
                if settings.fixedClock {
                    DatePicker("Console Time", selection: Binding(
                        get: { Date(timeIntervalSince1970: settings.fixedTime) },
                        set: { settings.fixedTime = $0.timeIntervalSince1970.rounded() }))
                }
                Toggle("Fixed Battery", isOn: $settings.fixedBattery)
                if settings.fixedBattery {
                    Stepper("Battery Level: \(settings.batteryLevel) of 4",
                            value: $settings.batteryLevel, in: 0...4)
                    Toggle("Charging", isOn: $settings.batteryCharging)
                }
                Stepper("Steps per Hour: \(settings.stepsPerHour)",
                        value: $settings.stepsPerHour, in: 0...20000, step: 100)
                Toggle("Region-Free Patch", isOn: $settings.regionFreePatch)
            } header: {
                InfoHeader("Console") {
                    Text("A fixed clock starts every game at the time above (useful for games that "
                         + "depend on the date). Without a fixed battery the console reports this "
                         + "device's battery. Steps per Hour feeds games that count steps (the "
                         + "Play Coins). The region-free patch lets a game run on a console of "
                         + "another region.")
                }
            }

            .listRowBackground(LGRowBackground())

            Section {
                Toggle("Deterministic Async Operations", isOn: $settings.deterministicAsync)
                Toggle("Simulate Headphones Plugged In", isOn: $settings.simulateHeadphones)
            } header: {
                InfoHeader("Compatibility") {
                    Text("Deterministic Async Operations runs file and install operations that "
                         + "normally finish in the background at a fixed time and order, which helps "
                         + "netplay and movies stay in sync and can fix games that break on the "
                         + "timing. Simulate Headphones makes the console report headphones plugged "
                         + "in, which some games use to pick their audio mix.")
                }
            }

            .listRowBackground(LGRowBackground())

            Section {
                Toggle("Built-in Game Hacks", isOn: $settings.gameHacksEnabled)
            } header: {
                InfoHeader("Game Hacks") {
                    Text("The core knows games that need a workaround (accurate shader math for Ocarina "
                         + "of Time 3D, no right-eye skipping for the Luigi's Mansion games) and "
                         + "applies it by itself. Turn this off to run every game without them. A "
                         + "single game can decide each workaround for itself in its Game Settings "
                         + "under Game Hacks.")
                }
            }

            .listRowBackground(LGRowBackground())

            Section {
                Toggle("LLE Applets", isOn: $settings.lleApplets)
                Toggle("Plugin Loader (3GX)", isOn: $settings.pluginLoader)
                Button("Show Plugins Folder in Files") {
                    try? FileManager.default.createDirectory(
                        at: FileLocations.pluginsDirectory, withIntermediateDirectories: true)
                    FileLocations.showInFiles(FileLocations.pluginsDirectory)
                }
            } header: {
                InfoHeader("Plugins") {
                    Text("LLE Applets runs the console's own system applets (the Mii maker and the "
                         + "like) instead of the built-in ones; it needs the system files of a real "
                         + "console. Loads Luma-style 3GX plugins for games. Put a plugin in the folder of the "
                         + "game's title ID (Files › Azahar › sdmc › luma › plugins › 0004000000XXXXXX "
                         + "› plugin.3gx) or name it default.3gx in plugins. Applies when a game "
                         + "starts.")
                }
            }

            .listRowBackground(LGRowBackground())

            Section {
                Toggle("Nintendo Network Support", isOn: $settings.onlineModules)
                NavigationLink("Online Play Guide") { OnlinePlayGuideView() }
            } header: {
                InfoHeader("Online Play") {
                    Text("Runs the console's own online system modules (network, certificates, "
                         + "friends) instead of the built-in ones, which online games need. It needs "
                         + "the system files of a real console and a replacement for Nintendo's "
                         + "servers. See the guide. Applies when a game starts.")
                }
            }

            .listRowBackground(LGRowBackground())

            Section {
                Toggle("Virtual SD Card", isOn: $settings.useVirtualSD)
                Toggle("Compress Installed CIAs", isOn: $settings.compressCIAInstalls)
                LabeledContent("Storage Folder", value: storageName ?? "Azahar folder")
                Button("Choose Storage Folder…") {
                    CustomStorage.choose { url in
                        if let url { storageName = url.lastPathComponent }
                    }
                }
                if storageName != nil {
                    Button("Use the Azahar Folder", role: .destructive) {
                        CustomStorage.reset()
                        storageName = nil
                    }
                }
            } header: {
                InfoHeader("Storage") {
                    Text("A chosen folder holds the console's nand and SD card (as the folders nand "
                         + "and sdmc inside it) instead of Files › Azahar. Existing data is not moved. "
                         + "Apps that run inside another app (LiveContainer) may not be allowed to "
                         + "use a folder outside their own; the Azahar folder is used then. Applies "
                         + "when a game starts.")
                }
            }

            .listRowBackground(LGRowBackground())

            Section {
                NavigationLink {
                    SaveStateManagerView()
                } label: {
                    Label("Save States", systemImage: "square.stack.3d.down.right")
                }
            } footer: {
                Text("See the save states of all games, how much space they take, and delete the "
                     + "ones you do not need.")
            }

            .listRowBackground(LGRowBackground())

            Section {
                LabeledContent("Data Folder", value: "Files › Azahar")
                LabeledContent("Games Folder", value: "Files › ROMs")
            } header: {
                InfoHeader {
                    EmptyView()
                } info: {
                    Text("System files, saves and the virtual SD card live in the Azahar folder of "
                         + "the app's documents, accessible from the Files app.")
                }
            }

            .listRowBackground(LGRowBackground())
        }
        .navigationTitle("System")
        .lgSettingsStyle()
    }
}

struct ControlsSettingsView: View {
    @EnvironmentObject private var settings: AppSettings
    @EnvironmentObject private var layout: ControllerLayoutStore
    @State private var confirmingLayoutReset = false

    var body: some View {
        Form {
            Section {
                Toggle("Show Controller", isOn: $settings.showController)
                Toggle("Show Swap Screens Button", isOn: $settings.showSwapButton)
                Toggle("Show Home Button", isOn: $settings.showHomeButton)
                    .disabled(!settings.showController)
                Toggle("Hide When a Controller Is Connected",
                       isOn: $settings.hideControllerWithHardware)
                    .disabled(!settings.showController)
                Toggle("Fade Out Menu Buttons With a Controller",
                       isOn: $settings.autoHideMenuButtons)
                VStack(alignment: .leading) {
                    LabeledContent("Opacity", value: "\(Int(settings.controllerOpacity * 100))%")
                    Slider(value: $settings.controllerOpacity, in: 0.1...1)
                }
                if LiquidGlass.isAvailable {
                    Toggle("Liquid Glass Controls", isOn: $settings.glassControls)
                    if settings.glassControls {
                        VStack(alignment: .leading) {
                            LabeledContent("Glass Tint",
                                           value: settings.glassControlTint <= 0.02
                                               ? "Clear" : "\(Int(settings.glassControlTint * 100))%")
                            Slider(value: $settings.glassControlTint, in: 0...1)
                        }
                    }
                }
                Toggle("Haptic Feedback", isOn: $settings.hapticFeedback)
                Button("Reset Controller Layout", role: .destructive) {
                    confirmingLayoutReset = true
                }
            } header: {
                InfoHeader("On-Screen Controller") {
                    Text("While playing, open the menu and choose Edit Controller Layout to move and "
                         + "resize the buttons.")
                }
            }
            .listRowBackground(LGRowBackground())
            Section {
                Toggle("Motion Controls", isOn: $settings.useMotion)
            } header: {
                InfoHeader {
                    EmptyView()
                } info: {
                    Text("Uses the gyroscope and accelerometer for games that use motion controls.")
                }
            }
            .listRowBackground(LGRowBackground())
            Section {
                NavigationLink("Controller Mapping") { ControllerMappingView() }
                Text("MFi, Xbox, PlayStation and Switch controllers are detected automatically. "
                     + "By default the face buttons are mapped by position like on a Nintendo "
                     + "console. Controller Mapping changes that and adds shortcuts.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            } header: {
                Text("Game Controllers")
            }
            .listRowBackground(LGRowBackground())
        }
        .navigationTitle("Controls")
        .lgSettingsStyle()
        .confirmationDialog("Reset the controller layout?", isPresented: $confirmingLayoutReset,
                            titleVisibility: .visible) {
            Button("Reset", role: .destructive) { layout.resetEverything() }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("Every button goes back to its default position and size.")
        }
    }
}

struct AboutView: View {
    @EnvironmentObject private var settings: AppSettings
    @State private var sharedLog: SharedFile?
    @State private var showingNoLog = false

    var body: some View {
        List {
            Section {
                VStack(spacing: 8) {
                    Image(systemName: "gamecontroller.fill")
                        .font(.system(size: 48))
                        .foregroundStyle(.tint)
                    Text("Azahar").font(.title.bold())
                    Text(AZEmulator.versionString)
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity)
                .padding(.vertical)
            }
            .listRowBackground(LGRowBackground())
            Section {
                LabeledContent("Renderer", value: "Metal (native)")
                LabeledContent("JIT", value: AZEmulator.isJITAvailable ? "Available" : "Unavailable")
            }
            .listRowBackground(LGRowBackground())
            Section {
                Toggle("Write Log", isOn: $settings.loggingEnabled)
                Toggle("GPU Pass Timing", isOn: $settings.gpuPassTiming)
                    .disabled(!settings.loggingEnabled)
                Toggle("Save Shaders for Analysis", isOn: $settings.dumpShaders)
                Button {
                    if let url = LogExporter.makeShareableLog() {
                        sharedLog = SharedFile(url: url)
                    } else {
                        showingNoLog = true
                    }
                } label: {
                    Label("Share Log", systemImage: "square.and.arrow.up")
                }
            } header: {
                InfoHeader("Diagnostics") {
                    Text("Shares the start and the end of the log and the last crash report, so the "
                         + "file stays small even when the full log is huge. Write Log turns the "
                         + "log off (only critical errors are still written) and stops the "
                         + "statistics line and its counters, which saves a little work in the "
                         + "emulator. It is off by default, so turn it on before you play a game you want a log of. "
                         + "GPU Pass Timing (with Write Log on) adds the GPU time of every kind of "
                         + "render pass to the log. It costs GPU time and can cause stutter, so "
                         + "only turn it on to measure something. Save Shaders for Analysis writes "
                         + "the game's most used shaders to Files › Azahar › shader_dump › (game "
                         + "ID) every 10 seconds or so, with an index.txt of how often each is "
                         + "used; leave it off for playing. Both apply when a game starts.")
                }
            }
            .listRowBackground(LGRowBackground())
            Section {
                Text("Azahar is an open-source Nintendo 3DS emulator based on Citra. This iOS "
                     + "port uses a native Metal renderer. Azahar is licensed under the GPLv2. "
                     + "Only use games and system files dumped from hardware you own.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }
            .listRowBackground(LGRowBackground())
        }
        .navigationTitle("About")
        .lgSettingsStyle()
        .sheet(item: $sharedLog) { file in
            ShareSheet(url: file.url)
        }
        .alert("No Log Found", isPresented: $showingNoLog) {
            Button("OK", role: .cancel) {}
        } message: {
            Text("Nothing has been logged yet.")
        }
    }
}

/// What online play needs and how to set it up.
struct OnlinePlayGuideView: View {
    var body: some View {
        List {
            Section("What you need") {
                Text("1. The system files of your own 3DS, installed with the Artic Setup Tool "
                     + "(Games › the antenna button › Setup Tool). They include the certificates "
                     + "and the online system modules.")
                Text("2. Nintendo's servers are gone. Games connect to a replacement, such as "
                     + "Pretendo Network, which needs its own patch for the game and for the "
                     + "console's account. Follow the instructions of the service you use.")
                Text("3. Turn on Nintendo Network Support in Settings › System, and the plugin "
                     + "loader if the service's patch is a 3GX plugin.")
            }
            .listRowBackground(LGRowBackground())
            Section("Good to know") {
                Text("This runs the real online modules, so it only works with a complete setup "
                     + "from your console. It is slower to start a game, and it has not been "
                     + "tested with every service.")
                Text("Local wireless play with others on the same network is separate: use "
                     + "Multiplayer in the game menu.")
            }
            .listRowBackground(LGRowBackground())
        }
        .navigationTitle("Online Play")
        .lgSettingsStyle()
    }
}

/// The user name, birthday, language, country, sound mode and Play Coins of the emulated console.
struct ConsoleProfileView: View {
    @State private var username = ""
    @State private var birthMonth = 1
    @State private var birthDay = 1
    @State private var language = 1
    @State private var country = 49
    @State private var soundMode = 1
    @State private var playCoins = 0
    @State private var loaded = false
    @State private var saved = false
    @State private var failed = false

    private static let languages = [
        "Japanese", "English", "French", "German", "Italian", "Spanish", "Chinese (Simplified)",
        "Korean", "Dutch", "Portuguese", "Russian", "Chinese (Traditional)",
    ]
    private static let months = [
        "January", "February", "March", "April", "May", "June", "July", "August", "September",
        "October", "November", "December",
    ]

    var body: some View {
        Form {
            Section {
                TextField("User Name", text: Binding(
                    get: { username },
                    set: { username = String($0.prefix(10)) }))
                Picker("Birth Month", selection: $birthMonth) {
                    ForEach(1...12, id: \.self) { Text(Self.months[$0 - 1]).tag($0) }
                }
                Picker("Birth Day", selection: $birthDay) {
                    ForEach(1...31, id: \.self) { Text("\($0)").tag($0) }
                }
            } header: {
                Text("You")
            }
            .listRowBackground(LGRowBackground())

            Section {
                Picker("Language", selection: $language) {
                    ForEach(0..<Self.languages.count, id: \.self) {
                        Text(Self.languages[$0]).tag($0)
                    }
                }
                Picker("Country", selection: $country) {
                    ForEach(CountryNames.all, id: \.code) { Text($0.name).tag($0.code) }
                }
                Picker("Sound", selection: $soundMode) {
                    Text("Mono").tag(0)
                    Text("Stereo").tag(1)
                    Text("Surround").tag(2)
                }
                Stepper("Play Coins: \(playCoins)", value: $playCoins, in: 0...300)
            } header: {
                InfoHeader("Console") {
                    Text("These are the settings of the emulated console, which games read. A country "
                         + "that does not belong to the console's region can stop some games from "
                         + "starting.")
                }
            }

            .listRowBackground(LGRowBackground())

            Section {
                Button("Save") {
                    let profile = AZConsoleProfile()
                    profile.username = username
                    profile.birthMonth = birthMonth
                    profile.birthDay = birthDay
                    profile.language = language
                    profile.country = country
                    profile.soundMode = soundMode
                    profile.playCoins = playCoins
                    if AZEmulator.writeConsoleProfile(profile) {
                        saved = true
                    } else {
                        failed = true
                    }
                }
            }

            .listRowBackground(LGRowBackground())
        }
        .navigationTitle("Console Profile")
        .lgSettingsStyle()
        .onAppear {
            guard !loaded else { return }
            loaded = true
            let profile = AZEmulator.readConsoleProfile()
            username = profile.username
            birthMonth = max(1, profile.birthMonth)
            birthDay = max(1, profile.birthDay)
            language = profile.language
            country = profile.country
            soundMode = profile.soundMode
            playCoins = profile.playCoins
        }
        .alert("Saved", isPresented: $saved) { Button("OK", role: .cancel) {} }
        .alert("Not Saved", isPresented: $failed) {
            Button("OK", role: .cancel) {}
        } message: {
            Text("The profile can only be changed while no game runs.")
        }
    }
}


/// The header of a section with a small (i) button that opens what the settings of the section do,
/// which keeps long explanations off the list itself.
struct InfoHeader<Title: View, Info: View>: View {
    private let titleText: String?
    private let title: Title
    private let info: Info
    @State private var showing = false

    init(@ViewBuilder title: () -> Title, @ViewBuilder info: () -> Info) {
        self.titleText = nil
        self.title = title()
        self.info = info()
    }

    var body: some View {
        HStack {
            title
            Spacer(minLength: 8)
            Button {
                showing = true
            } label: {
                Image(systemName: "info.circle")
                    .imageScale(.medium)
                    .contentShape(Rectangle())
            }
            .buttonStyle(.borderless)
            .accessibilityLabel("About these settings")
        }
        .sheet(isPresented: $showing) {
            InfoSheet(title: titleText ?? "About These Settings") { info }
        }
    }
}

extension InfoHeader where Title == Text {
    init(_ title: String, @ViewBuilder info: () -> Info) {
        self.titleText = title
        self.title = Text(title)
        self.info = info()
    }
}

extension InfoHeader where Title == EmptyView {
    init(@ViewBuilder info: () -> Info) {
        self.titleText = nil
        self.title = EmptyView()
        self.info = info()
    }
}

/// What a section's settings do, in a sheet.
private struct InfoSheet<Content: View>: View {
    let title: String
    @ViewBuilder let content: () -> Content
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            ScrollView {
                content()
                    .font(.body)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding()
            }
            .navigationTitle(title)
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
        .presentationDetents([.medium, .large])
    }
}


/// The colors blended in the soft background of the app (one set for light mode, one for dark
/// mode), and how strong it is.
struct AppColorsSettingsView: View {
    @EnvironmentObject private var settings: AppSettings
    @Environment(\.colorScheme) private var colorScheme
    /// Which set is being edited; it starts with the mode the device is in.
    @State private var editingDark: Bool?

    private var dark: Bool { editingDark ?? (colorScheme == .dark) }

    private var keyPaths: [ReferenceWritableKeyPath<AppSettings, String>] {
        dark ? [\.backdropDarkColor1, \.backdropDarkColor2, \.backdropDarkColor3]
             : [\.backdropColor1, \.backdropColor2, \.backdropColor3]
    }

    private func binding(_ index: Int) -> Binding<Color> {
        let keyPath = keyPaths[index]
        return Binding(get: { Color(hex: settings[keyPath: keyPath]) },
                       set: { settings[keyPath: keyPath] = $0.hexString })
    }

    private func apply(_ colors: [String], toDark: Bool) {
        let paths: [ReferenceWritableKeyPath<AppSettings, String>] = toDark
            ? [\.backdropDarkColor1, \.backdropDarkColor2, \.backdropDarkColor3]
            : [\.backdropColor1, \.backdropColor2, \.backdropColor3]
        for (path, hex) in zip(paths, colors) { settings[keyPath: path] = hex }
    }

    var body: some View {
        Form {
            Section {
                Picker("Appearance", selection: $settings.appearance) {
                    Text("System").tag(0)
                    Text("Light").tag(1)
                    Text("Dark").tag(2)
                }
                .pickerStyle(.segmented)
            } header: {
                Text("Theme")
            } footer: {
                Text("System follows the device's light or dark mode. Light and Dark keep the app in "
                     + "that mode whatever the device uses.")
            }
            .listRowBackground(LGRowBackground())

            Section {
                Toggle("Game Grid Background", isOn: $settings.gridBackdropEnabled)
                Toggle("Game List Background", isOn: $settings.listBackdropEnabled)
            } header: {
                Text("Games Tab")
            } footer: {
                Text("Turn the colored background off behind the game grid or the game list "
                     + "separately. The settings screens always have it.")
            }
            .listRowBackground(LGRowBackground())

            Section {
                Picker("Colors For", selection: Binding(get: { dark }, set: { editingDark = $0 })) {
                    Text("Light Mode").tag(false)
                    Text("Dark Mode").tag(true)
                }
                .pickerStyle(.segmented)
                ColorPicker("First Color", selection: binding(0), supportsOpacity: false)
                ColorPicker("Second Color", selection: binding(1), supportsOpacity: false)
                ColorPicker("Third Color", selection: binding(2), supportsOpacity: false)
                Button("Copy to \(dark ? "Light" : "Dark") Mode") {
                    apply(keyPaths.map { settings[keyPath: $0] }, toDark: !dark)
                }
                VStack(alignment: .leading) {
                    LabeledContent("Intensity", value: "\(settings.backdropIntensity)%")
                    Slider(value: Binding(get: { Double(settings.backdropIntensity) },
                                          set: { settings.backdropIntensity = Int(($0 / 5).rounded()) * 5 }),
                           in: 0...200, step: 5)
                }
            } header: {
                Text("Background")
            } footer: {
                Text("Three soft blobs of color are blended behind the game list and the settings. "
                     + "Light mode and dark mode have colors of their own; the set shown here is "
                     + "the one you are editing. Intensity above 100% makes the colors stronger.")
            }
            .listRowBackground(LGRowBackground())

            Section {
                ForEach(BackdropPalette.presets, id: \.name) { preset in
                    Button {
                        apply(preset.colors, toDark: dark)
                    } label: {
                        HStack {
                            Text(preset.name)
                                .foregroundStyle(.primary)
                            Spacer()
                            ForEach(preset.colors, id: \.self) { hex in
                                Circle().fill(Color(hex: hex)).frame(width: 22, height: 22)
                            }
                        }
                    }
                }
                Button("Reset to Default", role: .destructive) {
                    apply(BackdropPalette.defaultColors, toDark: false)
                    apply(BackdropPalette.defaultColors, toDark: true)
                    settings.backdropIntensity = 100
                }
            } header: {
                Text("Presets (for the set shown above)")
            }
            .listRowBackground(LGRowBackground())
        }
        .navigationTitle("App Colors")
        .lgSettingsStyle()
    }
}
