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
    @EnvironmentObject private var bundleMaker: BundleMaker
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
                Toggle("JIT Warm-Up", isOn: $settings.jitWarmup)
                    .disabled(!settings.cpuJIT)
                Toggle("Preload Game into Memory", isOn: $settings.preloadGame)
                if settings.preloadGame {
                    Toggle("Keep Compressed in Memory", isOn: $settings.preloadGameCompressed)
                }
                Stepper(value: $settings.cpuClockPercentage, in: 25...400, step: 25) {
                    LabeledContent("CPU Clock", value: "\(settings.cpuClockPercentage)%")
                }
            } header: {
                InfoHeader("CPU") {
                    HelpList([
                        HelpItem(note: "JIT recompilation needs the app to be started with JIT enabled. Without JIT the much slower interpreter is used."),
                        HelpItem("Built-in JIT Helper", "Enables JIT by itself through a small app extension, using your device's pairing file (import it below). Enable JIT Now tries it right away."),
                        HelpItem("Open Debugger to Enable JIT", "When the app starts without JIT, opens the debugger app you pick and asks it to attach. The debugger has to be set up already (its pairing file, VPN)."),
                        HelpItem("Debugger App", "The app that attaches. The link formats are guesses: if the app doesn't react, choose Custom and enter the link from that app's documentation, with {pid} for this app."),
                        HelpItem("Test the JIT Helper / Check the Connection", "Report what the helper and the debug service answer, to find out why JIT does not turn on."),
                        HelpItem("Pairing File", "Your device's pairing file, which the built-in JIT helper needs. Import it from Files; it stays on this device."),
                        HelpItem("CPU JIT", "Runs the game's code through the recompiler (much faster). Only available while JIT is enabled."),
                        HelpItem("JIT Warm-Up", "Remembers which game code each game needed, and in later runs prepares it while the game waits for the next frame, so scenes you have been to before load without the stutter of new code. Kept with the shader cache."),
                        HelpItem("Preload Game into Memory", "Reads the whole game into memory when it starts (a compressed game decompressed), so it is never read from storage while playing. Starting takes a few seconds longer and it uses as much memory as the game is big; a game too big for the memory iOS leaves the app is read as usual, with a message."),
                        HelpItem("Keep Compressed in Memory", "Keeps a compressed game (.zcci, .zcxi) in memory as it is stored: much less memory and a faster start, still decompressed as the game reads it."),
                        HelpItem("CPU Clock", "The speed of the emulated 3DS processor. Above 100% can smooth out games that slow down on a real 3DS, below 100% saves work; some games misbehave away from 100%."),
                    ])
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
                    HelpList([
                        HelpItem("Sync Speed to Display", "Runs the game at exactly the display's 60 frames per second instead of the 3DS's 59.83, so no frame has to be repeated or dropped. The difference in speed is too small to notice."),
                        HelpItem("Limit Speed / Speed Limit", "Holds the game at the speed limit (100% is a real 3DS). Off runs as fast as the device can."),
                        HelpItem("Fast Forward Speed", "How fast Fast Forward runs. Fast Forward is in the game menu, and on a controller button in Controls › Controller Mapping."),
                        HelpItem("Hold to Fast Forward (Controller)", "The controller button only fast-forwards while it is held, instead of switching it on and off."),
                        HelpItem("Simulate 3DS GPU Timing", "Makes games see the graphics chip take as long as the real one. Games that adapt their frame rate to it (60 FPS where there is little to draw, 30 in heavy scenes) then run at the frame rate they have on a console; without it the emulated GPU is instant and they can run at 60 FPS everywhere."),
                        HelpItem("GPU Timing Strength", "How slow the simulated GPU is. The estimate is twice as fast as a console, so about 200% is the console; raise it if a game still runs at 60 FPS where the console would run 30."),
                        HelpItem("Minimum GPU Time", "Steadier than the estimate: the game never sees a frame's 3D work finish sooner than this after the vertical blank, so a game that switches between 30 and 60 holds 30. Try 20 ms; raise it if the game goes below 30, lower it if it never reaches 30."),
                    ])
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
                    HelpList([
                        HelpItem(note: "Compressed games (.zcci, .zcxi, .z3dsx and .zcia) are smaller and play the same. They are written to the games folder (Files › ROMs). Encrypted and bundled games cannot be compressed."),
                        HelpItem("Keep Original Files", "Compressing the games in the library keeps their uncompressed files instead of removing them."),
                        HelpItem("Compress Games…", "Pick files to compress. The files you pick are left where they are."),
                        HelpItem("Compress the Games in the Library", "Compresses the games in the games folder and removes their originals, unless Keep Original Files is on. A .zcia installs like a .cia."),
                    ])
                }
            }

            .listRowBackground(LGRowBackground())

            Section {
                bundleRow("Game", files: bundleMaker.game.map { [$0] } ?? [], slot: .game)
                bundleRow("Update", files: bundleMaker.update.map { [$0] } ?? [], slot: .update)
                bundleRow("DLC", files: bundleMaker.dlc, slot: .dlc)
                Button("Create Bundle") {
                    bundleMaker.create(library: library)
                }
                .disabled(!bundleMaker.canCreate)
                if bundleMaker.isRunning {
                    ProgressView(value: bundleMaker.progress)
                }
            } header: {
                InfoHeader("ROM Bundles") {
                    HelpList([
                        HelpItem(note: "A bundle (.bcci, .bcxi) is a game together with its update and DLC in one file: it plays with them without installing them, and is easy to move or back up. Encrypted games cannot be bundled."),
                        HelpItem("Game, Update, DLC", "Choose the game (.3ds, .cci, .cxi or .cia, also compressed), then its update and DLC if you have them (.cia or .zcia files). The files you choose are left where they are."),
                        HelpItem("Create Bundle", "Writes the bundle to the games folder (Files › ROMs). A game chosen as a .cia makes a .bcia, which is installed like a .cia."),
                    ])
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
        .alert("ROM Bundle", isPresented: Binding(
            get: { bundleMaker.message != nil },
            set: { if !$0 { bundleMaker.message = nil } })) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(bundleMaker.message ?? "")
        }
        .navigationTitle("General")
        .lgSettingsStyle()
    }

    /// A row of the ROM Bundles section: the chosen files, a button to choose and one to clear.
    @ViewBuilder
    private func bundleRow(_ title: String, files: [URL], slot: BundleMaker.Slot) -> some View {
        HStack {
            VStack(alignment: .leading, spacing: 2) {
                Text(title)
                Text(files.isEmpty ? (slot == .game ? "Not chosen" : "None")
                     : files.map(\.lastPathComponent).joined(separator: ", "))
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(2)
                    .truncationMode(.middle)
            }
            Spacer()
            if !files.isEmpty {
                Button(role: .destructive) {
                    bundleMaker.clear(slot)
                } label: {
                    Image(systemName: "xmark.circle.fill")
                }
                .buttonStyle(.borderless)
                .accessibilityLabel("Remove \(title)")
            }
            Button(slot == .dlc && !files.isEmpty ? "Add…" : "Choose…") {
                bundleMaker.choose(slot)
            }
            .buttonStyle(.borderless)
        }
        .disabled(bundleMaker.isRunning)
    }
}

struct GraphicsSettingsView: View {
    @EnvironmentObject private var settings: AppSettings
    @State private var postShaders = PostShaders.availableNames()
    @State private var clearedShaderCache = false

    /// What each setting of the section does, for its info sheet (HelpList).
    static var enhancementsHelp: [HelpItem] {
        let items: [HelpItem] = [
            HelpItem("Internal Resolution", "Renders the 3D at a multiple of the 3DS's resolution: sharper, more GPU work. With MetalFX, 1x or 2x often looks as good."),
            HelpItem("Linear Screen Filtering", "Smooths the screens when they are enlarged; off keeps hard pixels."),
            HelpItem("Texture Sampling", "Game Controlled uses each texture's own filtering; the others force nearest or linear filtering on all textures."),
            HelpItem("Texture Filter", "Smooths the games' textures when Internal Resolution is above 1x: xBRZ and MMPX suit pixel art, Bicubic and ScaleForce are gentler."),
            HelpItem("Anisotropic Filtering", "Keeps textures seen at an angle sharp; lower values are faster. On, textures use the GPU's own choice of mipmap level (plus the game's bias), which it needs; Off keeps the 3DS's exact formula."),
            HelpItem("Anti-Aliasing (MSAA)", "Smooths the jagged edges of the 3D: each pixel is drawn with several samples along the edges (2x, 4x or 8x, capped at what the device supports). Cheap on Apple GPUs for what it does, but it costs GPU time and memory, more at higher Internal Resolution; most useful at 1x or 2x."),
            HelpItem("Fast Shader Math", "Lets the shader compiler use faster math; can cause small graphical differences in some games."),
            HelpItem("Fast Lighting Math", "Computes lighting in half precision, which helps games with a lot of lighting (Luigi's Mansion) and can shift highlights slightly."),
            HelpItem("Fast Lighting Lookups", "Reads the lighting tables with the GPU's texture filtering, one step per lookup instead of several; the ends of the tables are held instead of extended, which can change the brightest highlights a little."),
            HelpItem("Fast Color Math", "Does the color combiners in half precision, the same 8-bit result in practice."),
            HelpItem("Fast Math Functions", "Uses faster versions of functions like normalize and pow (iOS 18), without the NaN assumptions of Fast Shader Math; it can be on together with it."),
            HelpItem("Half Precision Colors Between Stages", "Passes the vertex color to the pixel shader in half precision, which saves a little bandwidth."),
            HelpItem("Untracked Stream Buffers", "Stops Metal from tracking the buffers the emulator already keeps in order itself, which saves CPU time in games with many draws. Turn it off if a game flickers."),
            HelpItem("Reuse Shaded Vertices", "Draws the models the GPU shades without lighting natively, so a vertex shared by several triangles is shaded once instead of once per triangle (several times cheaper; lit models are not affected). Turn it off if a game shows broken models."),
            HelpItem("Fast Math Functions in Vertex Shaders", "Faster, less exact reciprocal, square root and the like in the vertex programs (iOS 18). A little cheaper, but may leave tiny cracks between models; turn it off if you see them."),
            HelpItem("Fast Math in Vertex Shaders", "Goes further (the compiler may assume values are never NaN or infinite) and turns Accurate Multiplication off; it can break models in games that depend on that (exploding or missing geometry)."),
            HelpItem("Texture Pool", "Keeps the textures of surfaces a game stopped using (up to 192 MB) and gives them to the next surface of the same size and format instead of creating new ones, which helps games that stream a lot. Turn it off if a game shows leftover pictures."),
            HelpItem("Fast Depth Readback", "Answers the small depth checks some games do every frame (Mario Kart 7's sun glare) with the previous frame's values instead of waiting for the GPU."),
            HelpItem("Wait for GPU Each Frame", "Keeps the emulation at most one frame ahead of the GPU, which keeps frame pacing even. Off lets it run further ahead, which can help a game whose CPU and GPU work are both heavy, at the cost of more input latency and less even pacing."),
            HelpItem("Disable Right Eye Render", "Skips drawing the right eye's picture while 3D is off, which saves GPU work. Some games need it drawn (the game hacks know them)."),
            HelpItem("Hidden Surface Removal", "Lets the GPU skip pixels hidden behind others (not for games using W-buffering)."),
            HelpItem("Fast W-Buffer Depth (Experimental)", "Helps games that use the 3DS's W-buffer depth mode (Dragon Quest VIII for most of its draws), which Hidden Surface Removal cannot: the depth stays exact, and the GPU can still skip pixels hidden behind ones already drawn. It relies on how games normally set up their 3D view; if parts of a scene go missing or flicker, turn it off for that game."),
            HelpItem("Draw Optimizations", "Draws depth-only geometry (like shadow volumes) without a pixel shader, does the scissor test with the GPU's own scissor, drops blending that changes nothing and draws partial clears inside the game's own render passes. Turn it off if a game shows glitches it did not have before."),
            HelpItem("Texture Compression", "Saves GPU memory bandwidth: Lossless looks identical, Lossy (A15 and later) saves more with a tiny loss of quality."),
            HelpItem(note: "All of these apply the next time a game starts."),
        ]
        return items
    }

    static func antiAliasingName(_ value: Int) -> String {
        value == 0 ? "Off" : "\(1 << value)x"
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
                    HelpList([
                        HelpItem("Renderer", "Neroli renders natively with Metal."),
                        HelpItem("Hardware Shaders", "Runs the game's vertex shaders on the GPU. Draws that can't be translated, and games that use geometry shaders, still run on the CPU."),
                        HelpItem("Accurate Multiplication", "Matches the 3DS's multiplication exactly, which fixes graphical glitches in some games at a small GPU cost."),
                        HelpItem("Shader JIT", "Speeds up the shaders that still run on the CPU. Needs JIT."),
                    ])
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
                Picker("Anti-Aliasing (MSAA)", selection: $settings.antiAliasing) {
                    ForEach(0...3, id: \.self) { Text(Self.antiAliasingName($0)).tag($0) }
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
                Toggle("Wait for GPU Each Frame", isOn: $settings.waitForGPU)
                Toggle("Disable Right Eye Render", isOn: $settings.disableRightEyeRender)
                Toggle("Hidden Surface Removal", isOn: $settings.hiddenSurfaceRemoval)
                Toggle("Fast W-Buffer Depth (Experimental)", isOn: $settings.fastWBufferDepth)
                Toggle("Draw Optimizations", isOn: $settings.drawOptimizations)
                Picker("Texture Compression", selection: $settings.textureCompression) {
                    ForEach(TextureCompressionOption.allCases) { Text($0.title).tag($0) }
                }
            } header: {
                InfoHeader("Enhancements") {
                    HelpList(Self.enhancementsHelp)
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
                    HelpList([
                        HelpItem("Use External Display", "Uses a TV or AirPlay display while one is connected. Running inside another app (such as LiveContainer) may stop iOS from offering the display to Neroli."),
                        HelpItem("External Display Shows", "Default shows the top screen on the display and the bottom screen with the controls on this device. The other choices give the display the screen layout you pick, while this device keeps its own layout. Takes effect when a game starts."),
                    ])
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
                    HelpList([
                        HelpItem("Custom Textures", "Replaces the textures of a game with the images in Files › Neroli › load › textures › the game's title ID."),
                        HelpItem("Preload Custom Textures", "Loads all of a pack's images when the game starts: more memory and a longer start, no stutter when a texture first appears."),
                        HelpItem("Load Custom Textures in the Background", "Without preloading, loads each image as it is needed without stopping the game."),
                        HelpItem("Dump Textures", "Saves the textures a game uses to Files › Neroli › dump › textures, to make a pack."),
                        HelpItem(note: "These apply the next time a game starts."),
                    ])
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
                    HelpList([
                        HelpItem("Outer Cameras, Inner Camera", "Which camera of this device each camera of the 3DS shows. Off shows a black picture. The first time a game uses a camera, iOS asks for permission. Applies the next time a game starts."),
                    ])
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
                    HelpList([
                        HelpItem("3D Mode", "Anaglyph needs red/cyan glasses. Interlaced shows the eyes on alternate rows of the screen. Side by Side and Cardboard VR show one picture per eye, for a headset or glasses holder (landscape). These modes don't use MetalFX."),
                        HelpItem("Swap Eyes", "Shows each eye's picture to the other eye, for glasses or headsets that expect them the other way round."),
                        HelpItem("Eye Shown", "With 3D off, which eye's picture is shown."),
                        HelpItem("Anaglyph Colors", "How the two eyes' pictures are mixed into red and cyan."),
                        HelpItem("Cardboard Screen Size, Shifts", "The size and position of each eye's picture in Cardboard VR, to fit your headset."),
                    ])
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
                        HelpList([
                            HelpItem("Upscaling", "MetalFX Spatial scales each screen up to the size it is shown at with Apple's GPU upscaler, which looks sharper than plain filtering and lets you use a lower Internal Resolution (1x or 2x) for the same picture. It applies to the next frame, no restart needed."),
                            HelpItem("Scale", "How far it enlarges a screen (1.5x, 2x, ...); the result is then stretched to the size on the display. A lower scale costs less GPU time, Fit to Screen goes straight to the display size. Scales that would not enlarge the screen by at least a quarter use plain filtering."),
                        ])
                    } else {
                        HelpList([HelpItem(note: "MetalFX is not available on this device.")])
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
                    HelpList([
                        HelpItem("Effect", "An effect applied to the finished screens (such as LCD Grid). LCD Grid Darkness is how dark the gaps of the grid are; Saturation, Contrast and Brightness adjust the colors."),
                        HelpItem("Sharp Pixels", "Keeps the pixels of a screen crisp when it is enlarged; only the edge of a pixel is smoothed. Takes the place of the Scaling Filter."),
                        HelpItem("Sharpening Method", "Adds contrast at edges, after MetalFX; it can be combined with an effect. Contrast Adaptive only sharpens where the picture is soft and keeps edges free of halos."),
                        HelpItem("Scaling Filter", "How a screen is enlarged: Bicubic and Lanczos keep edges crisper than Bilinear."),
                        HelpItem("Custom Shader", "Replaces the effect with your own: put a .metal file with a post_effect function in Files › Neroli › shaders › post (Example.metal shows how). A shader that does not compile is ignored, the log tells why."),
                        HelpItem(note: "These apply when a game starts."),
                    ])
                }
            }
            .listRowBackground(LGRowBackground())

            Section {
                Toggle("Sync to Display Refresh", isOn: $settings.displaySync)
                if settings.displaySync {
                    if !settings.frameCap30 {
                        Toggle("Steady 30 FPS", isOn: $settings.steadyThirty)
                    }
                    Toggle("Present Every Refresh", isOn: $settings.presentEveryRefresh)
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
                    HelpList([
                        HelpItem("Sync to Display Refresh", "Frames go to the screen in step with the display's refreshes, and the game makes each frame just after a refresh, so they stay even."),
                        HelpItem("Steady 30 FPS", "Notices when a game draws at 30 FPS and then shows each of its frames for exactly two 3DS refreshes, which removes the judder of games that hand over their frames unevenly. It follows the game again as soon as it draws faster, and does not slow it down."),
                        HelpItem("Present Every Refresh", "Shows the current frame again at every refresh that has no new one, so a ProMotion display keeps its full rate between the frames of a 30 FPS game instead of slowing down, which can make frames land a refresh late. Costs a little GPU time and battery. Not used while Frame Generation is on, nor at 60 FPS."),
                        HelpItem("30 FPS Frame Cap", "Shows every new frame of the game for exactly two refreshes and holds a faster game to 30. Best set per game, for games that run at 30 FPS but hand their frames over unevenly."),
                        HelpItem("Run Game at 30", "With the cap, the game itself also runs at 30 (its 3D work never finishes within one refresh), so it hands over frames evenly and does not draw frames that would be thrown away. Turn it off for a game that slows down instead of dropping to 30."),
                        HelpItem("Frame Pacing, Frame Pacing Buffer", "Without Sync to Display Refresh: shows each frame for the same amount of time, which makes motion smoother on displays that refresh faster than the game. The buffer adds a little delay so a slow frame still meets its slot. Not while fast-forwarding."),
                        HelpItem("Display Refresh Rate", "Asks ProMotion displays to run at 60 Hz (saves power) or up to 120 Hz; games run at their own frame rate either way. iOS may ignore the request, especially inside another app."),
                        HelpItem(note: "The performance overlay's Pace value is how much the time between frames varies (lower is smoother). These apply when a game starts."),
                    ])
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
                    HelpList([
                        HelpItem("Async Shader Compilation", "Builds new shaders in the background instead of pausing the game; an object may be missing for a moment instead."),
                        HelpItem("Hold Frame While Shaders Build", "Keeps showing the previous frame (up to three in a row) while a shader of a screen-sized picture is still building, so nothing flickers; the picture repeats for a moment instead."),
                        HelpItem("Wait for Shaders", "Lets a draw wait a few milliseconds for its shader first (at most four times that per frame), which avoids most missing objects for a tiny hitch."),
                        HelpItem("Disk Shader Cache", "Remembers the shaders of each game and builds them while it boots, so they don't need to be built again during play."),
                        HelpItem("Clear Shader Cache", "Deletes the shader caches (and the JIT warm-up lists) of all games; they are built up again as you play."),
                    ])
                }
            }

            .listRowBackground(LGRowBackground())
            .alert("Shader Cache Cleared", isPresented: $clearedShaderCache) {
                Button("OK", role: .cancel) {}
            }
    }

    /// What each setting of the section does, for its info sheet (HelpList).
    static var performanceHelp: [HelpItem] {
        let prefix: [HelpItem] = FrameGenerator.isSupported
            ? []
            : [HelpItem(note: "Frame generation is not available on this device or iOS version.")]
        let items: [HelpItem] = [
            HelpItem("Frame Presentation Skip", "Does not present a frame the game did not draw again, so a 30 FPS game shows 30 frames per second instead of every frame twice. The emulation speed and the audio stay the same. Meant for frame generation and interpolation tools that need the real frame rate."),
            HelpItem("Frame Generation (Experimental)", "Makes the frames between the real ones with Apple's frame interpolation, so a 30 FPS game looks like 60. It adds about half a frame of delay, can smear text and the 3D depth, and uses more GPU and battery. It turns on Frame Presentation Skip."),
            HelpItem("Frame Generation Size", "The size the in-between frames are made at (the real frames are always full size): Full is sharpest and slowest, each step down is less work and heat with softer in-between frames. If the device can't make frames that size, frame generation stays off. Adaptive starts at Half and goes down to Quarter while in-between frames are being lost (heavy scenes such as water), then back up when there has been room for a while; each change of size makes the game stutter briefly."),
            HelpItem("Pause When Behind", "Shows the game's own frames, at a steady rate, only while made frames are actually being lost (several a second) and no smaller size helps, and tries frame generation again after 5 seconds."),
            HelpItem("Frame Buffer", "Keeps one more frame waiting before showing them, so a frame that comes a little late still makes its refresh: steadier (most noticeable at 120 Hz) for one refresh more input latency."),
            HelpItem("Use MetalFX Interpolation (Experimental)", "Makes the in-between frames with MetalFX's frame interpolator instead of VideoToolbox's."),
            HelpItem("Top Screen Only", "Leaves the bottom screen at the game's own frame rate, which halves the work in most layouts."),
            HelpItem("Display-Driven Pacing", "Lets the display pick up each frame at its own refresh instead of timing the frames in advance (iOS 17 or later)."),
            HelpItem("Also 60 FPS Games (120 Hz)", "Makes 60 FPS games show 120 frames per second; needs Display-Driven Pacing and the display at 120 Hz, adds about 8 ms of delay and twice the heat."),
            HelpItem("Show Performance Overlay", "Shows the frame rate, speed, GPU time and frame pacing over the game."),
        ]
        return prefix + items
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
                    HelpList(Self.performanceHelp)
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
                    HelpList([
                        HelpItem("Screen Layout", "How the two screens are placed in landscape. Place the screens yourself with Edit Screen Layout in the menu while a game runs (Custom)."),
                    ])
                }
            }
            .listRowBackground(LGRowBackground())
            Section {
                Picker("Screen Layout", selection: $settings.portraitLayout) {
                    ForEach(PortraitScreenLayout.allCases) { Text($0.title).tag($0) }
                }
            } header: {
                InfoHeader("Portrait") {
                    HelpList([
                        HelpItem("Screen Layout", "How the two screens are placed in portrait. Place the screens yourself with Edit Screen Layout in the menu while a game runs (Custom)."),
                        HelpItem("Fold (Top and Bottom Half)", "For a foldable held like a laptop: the top screen fills the upper half of the display and the bottom screen sits at the top of the lower half, with the on-screen controller drawn over that half. The split is at the middle, where the hinge is; Screen Gap leaves space around it. To move or resize the screens, choose Edit Screen Layout in the game menu: it switches to Custom, starting from where the Fold layout put the screens. Edit Controller Layout moves the controls."),
                    ])
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
                    HelpList([
                        HelpItem("Integer Scaling", "Enlarges the screens only by whole multiples, so every 3DS pixel becomes the same number of display pixels."),
                        HelpItem("Aspect Ratio", "The shape the screens are drawn in."),
                        HelpItem("Stretch Top Screen, Stretch Bottom Screen", "Fills the window with the screen, apart from the padding."),
                        HelpItem("Paddings", "Space left free around a stretched screen, in pixels."),
                        HelpItem(note: "These apply to the layouts that do not place the screens by hand."),
                    ])
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
                    HelpList([
                        HelpItem("Saved Layouts", "Tap a saved layout to use it, swipe to delete one. In a game, Next Screen Layout in the menu (or a controller shortcut) cycles through them."),
                        HelpItem("Save Current Layout…", "Saves what you set up with Edit Screen Layout. Save Screen Layout in the game menu does the same."),
                    ])
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
                    HelpList([
                        HelpItem("Fixed Clock, Console Time", "Starts every game at the time set here instead of the current time (useful for games that depend on the date)."),
                        HelpItem("Fixed Battery, Battery Level, Charging", "Reports this battery level to games instead of this device's battery."),
                        HelpItem("Steps per Hour", "Steps fed to games that count them (the Play Coins)."),
                        HelpItem("Region-Free Patch", "Lets a game run on a console of another region."),
                    ])
                }
            }

            .listRowBackground(LGRowBackground())

            Section {
                Toggle("Deterministic Async Operations", isOn: $settings.deterministicAsync)
                Toggle("Simulate Headphones Plugged In", isOn: $settings.simulateHeadphones)
            } header: {
                InfoHeader("Compatibility") {
                    HelpList([
                        HelpItem("Deterministic Async Operations", "Runs file and install operations that normally finish in the background at a fixed time and order, which helps netplay and movies stay in sync and can fix games that break on the timing."),
                        HelpItem("Simulate Headphones Plugged In", "Makes the console report headphones plugged in, which some games use to pick their audio mix."),
                    ])
                }
            }

            .listRowBackground(LGRowBackground())

            Section {
                Toggle("Built-in Game Hacks", isOn: $settings.gameHacksEnabled)
            } header: {
                InfoHeader("Game Hacks") {
                    HelpList([
                        HelpItem("Built-in Game Hacks", "The core knows games that need a workaround (accurate shader math for Ocarina of Time 3D, no right-eye skipping for the Luigi's Mansion games) and applies it by itself. Turn this off to run every game without them. A single game can decide each workaround for itself in its Game Settings under Game Hacks."),
                    ])
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
                    HelpList([
                        HelpItem("LLE Applets", "Runs the console's own system applets (the Mii maker and the like) instead of the built-in ones; it needs the system files of a real console."),
                        HelpItem("Plugin Loader (3GX)", "Loads Luma-style 3GX plugins for games. Put a plugin in the folder of the game's title ID (Files › Neroli › sdmc › luma › plugins › 0004000000XXXXXX › plugin.3gx), or name it default.3gx in plugins for every game."),
                        HelpItem(note: "These apply when a game starts."),
                    ])
                }
            }

            .listRowBackground(LGRowBackground())

            Section {
                Toggle("Nintendo Network Support", isOn: $settings.onlineModules)
                NavigationLink("Online Play Guide") { OnlinePlayGuideView() }
            } header: {
                InfoHeader("Online Play") {
                    HelpList([
                        HelpItem("Nintendo Network Support", "Runs the console's own online system modules (network, certificates, friends) instead of the built-in ones, which online games need. It needs the system files of a real console and a replacement for Nintendo's servers; see the guide. Applies when a game starts."),
                    ])
                }
            }

            .listRowBackground(LGRowBackground())

            Section {
                Toggle("Virtual SD Card", isOn: $settings.useVirtualSD)
                Toggle("Compress Installed CIAs", isOn: $settings.compressCIAInstalls)
                LabeledContent("Storage Folder", value: storageName ?? "Neroli folder")
                Button("Choose Storage Folder…") {
                    CustomStorage.choose { url in
                        if let url { storageName = url.lastPathComponent }
                    }
                }
                if storageName != nil {
                    Button("Use the Neroli Folder", role: .destructive) {
                        CustomStorage.reset()
                        storageName = nil
                    }
                }
            } header: {
                InfoHeader("Storage") {
                    HelpList([
                        HelpItem("Virtual SD Card", "Gives games an SD card, where installed titles, updates, DLC and some saves live."),
                        HelpItem("Compress Installed CIAs", "Stores the titles installed from CIA files compressed, to save space."),
                        HelpItem("Storage Folder", "A folder you choose holds the console's nand and SD card (as the folders nand and sdmc inside it) instead of Files › Neroli. Existing data is not moved. Apps running inside another app (LiveContainer) may not be allowed to use a folder outside their own; the Neroli folder is used then. Applies when a game starts."),
                    ])
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
                LabeledContent("Data Folder", value: "Files › Neroli")
                LabeledContent("Games Folder", value: "Files › ROMs")
            } header: {
                InfoHeader {
                    EmptyView()
                } info: {
                    Text("System files, saves and the virtual SD card live in the Neroli folder of "
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
                    HelpList([
                        HelpItem("Show Controller", "Shows the touch controls over the game. While playing, open the menu and choose Edit Controller Layout to move and resize the buttons."),
                        HelpItem("Show Swap Screens Button, Show Home Button", "Adds those buttons to the touch controls."),
                        HelpItem("Hide When a Controller Is Connected", "Hides the touch controls while a game controller is connected."),
                        HelpItem("Fade Out Menu Buttons With a Controller", "With a controller, the menu buttons fade until you touch the screen."),
                        HelpItem("Opacity", "How see-through the touch controls are."),
                        HelpItem("Liquid Glass Controls, Glass Tint", "Draws the controls with iOS's Liquid Glass look (iOS 26). It costs some GPU time, so it is off by default."),
                        HelpItem("Haptic Feedback", "A light tap of the vibration motor when a button is pressed."),
                    ])
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
                    Text("Neroli").font(.title.bold())
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
                    HelpList([
                        HelpItem("Share Log", "Shares the start and the end of the log and the last crash report, so the file stays small even when the full log is huge."),
                        HelpItem("Write Log", "Off, only critical errors are written and the statistics line and its counters stop, which saves a little work. Turn it on before you play a game you want a log of."),
                        HelpItem("GPU Pass Timing", "With Write Log on, adds the GPU time of every kind of render pass to the log. It costs GPU time and can cause stutter, so only turn it on to measure something."),
                        HelpItem("Save Shaders for Analysis", "Writes the game's most used shaders to Files › Neroli › shader_dump › (game ID) every 10 seconds or so, with an index.txt of how often each is used. Leave it off for playing."),
                        HelpItem(note: "GPU Pass Timing and Save Shaders apply when a game starts."),
                    ])
                }
            }
            .listRowBackground(LGRowBackground())
            Section {
                Text("Neroli is an iOS port of Azahar, the open-source Nintendo 3DS emulator "
                     + "that continues Citra, with a native Metal renderer. It is not affiliated "
                     + "with the Azahar developers or with Nintendo. Neroli is free software, "
                     + "licensed under the GNU General Public License, version 3 or any later "
                     + "version. "
                     + "Only use games and system files dumped from hardware you own.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
                Link("Source Code", destination: URL(string: "https://github.com/JMillz269/Neroli")!)
                NavigationLink("Licenses") { LicensesView() }
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
                    HelpList([
                        HelpItem(note: "These are the settings of the emulated console, which games read."),
                        HelpItem("Language", "The console's language; games use it to pick theirs."),
                        HelpItem("Country", "A country that does not belong to the console's region can stop some games from starting."),
                        HelpItem("Sound", "Mono, stereo or surround output, as games see it."),
                        HelpItem("Play Coins", "The console's Play Coins, which some games spend."),
                    ])
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

/// One entry of a section's info sheet: a setting's name and what it does, or a note about the
/// whole section (no name).
struct HelpItem {
    let name: String?
    let text: String

    init(_ name: String, _ text: String) {
        self.name = name
        self.text = text
    }

    init(note: String) {
        self.name = nil
        self.text = note
    }
}

/// The settings of a section one by one, in the order of their rows: the name in bold and what
/// it does under it, so a setting can be found at a glance instead of in one long paragraph.
struct HelpList: View {
    let items: [HelpItem]

    init(_ items: [HelpItem]) {
        self.items = items
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            ForEach(Array(items.enumerated()), id: \.offset) { index, item in
                if index > 0 {
                    Divider().padding(.vertical, 12)
                }
                VStack(alignment: .leading, spacing: 4) {
                    if let name = item.name {
                        Text(name)
                            .font(.headline)
                        Text(item.text)
                    } else {
                        Text(item.text)
                            .foregroundStyle(.secondary)
                    }
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                .accessibilityElement(children: .combine)
            }
        }
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

// MARK: - Licenses

/// One component in Licenses.txt, which src/citra_ios/CMakeLists.txt builds from the license
/// files of Neroli, Azahar and every third-party component in the app.
struct LicenseEntry: Identifiable {
    let id: Int
    let name: String
    let summary: String
    let text: String
}

enum LicenseList {
    /// The entries of Licenses.txt: a line "@@@ name", a line with the license and where the code
    /// comes from, then the text.
    static let entries: [LicenseEntry] = {
        guard let url = Bundle.main.url(forResource: "Licenses", withExtension: "txt"),
              let content = try? String(contentsOf: url, encoding: .utf8)
        else { return [] }
        var result: [LicenseEntry] = []
        var name: String?
        var summary: String?
        var lines: [Substring] = []
        func finish() {
            if let name {
                result.append(LicenseEntry(id: result.count, name: name, summary: summary ?? "",
                                           text: lines.joined(separator: "\n")))
            }
        }
        for line in content.split(separator: "\n", omittingEmptySubsequences: false) {
            if line.hasPrefix("@@@ ") {
                finish()
                name = String(line.dropFirst(4))
                summary = nil
                lines = []
            } else if name != nil && summary == nil {
                summary = String(line)
            } else {
                lines.append(line)
            }
        }
        finish()
        return result
    }()
}

/// The licenses of Neroli (Azahar) and of the third-party code built into the app.
struct LicensesView: View {
    private let entries = LicenseList.entries

    var body: some View {
        Form {
            Section {
                Text("Neroli is free software: you can redistribute it and/or modify it under the "
                     + "terms of the GNU General Public License as published by the Free Software "
                     + "Foundation, either version 3 of the License, or (at your option) any later "
                     + "version. It is distributed WITHOUT ANY WARRANTY. The app also contains the "
                     + "third-party components below, each under its own license.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }
            .listRowBackground(LGRowBackground())
            Section {
                if entries.isEmpty {
                    Text("The license texts are missing from this build.")
                        .foregroundStyle(.secondary)
                }
                ForEach(entries) { entry in
                    NavigationLink {
                        LicenseTextView(entry: entry)
                    } label: {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(entry.name)
                            Text(entry.summary)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                                .lineLimit(2)
                        }
                    }
                }
            }
            .listRowBackground(LGRowBackground())
        }
        .navigationTitle("Licenses")
        .lgSettingsStyle()
    }
}

/// The full text of one license.
struct LicenseTextView: View {
    let entry: LicenseEntry

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 12) {
                Text(entry.summary)
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                Text(entry.text)
                    .font(.system(.caption, design: .monospaced))
                    .textSelection(.enabled)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding()
        }
        .navigationTitle(entry.name)
        .navigationBarTitleDisplayMode(.inline)
    }
}
