// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

import CoreMotion
import SwiftUI
import UIKit

// The Liquid Glass pieces of the app in one place.
//
// Glass is the system's own material (iOS 26): it refracts and lights itself from what is behind
// it and adapts its contrast by itself, so nothing here imitates it with opacity or blur. The
// app still runs on iOS 16, where the same call sites fall back to the system materials.
//
// Rules the pieces follow:
// - Glass is for controls that float over content (the game, the library), not for content.
// - Glass surfaces that are near each other share one LGContainer: glass cannot sample other
//   glass, and a container also draws them in one pass.
// - Nothing above a glass surface uses an opacity below 1, which would flatten the material.
// - A surface changes by the value of its glass (pressed, tinted), not by adding and removing the
//   modifier, so SwiftUI keeps the view's identity and can animate the change.

// MARK: - Containers and surfaces

/// Groups glass surfaces so they are drawn together (and can merge and morph). Below iOS 26 it is
/// only the content.
struct LGContainer<Content: View>: View {
    var spacing: CGFloat = 0
    @ViewBuilder var content: () -> Content

    var body: some View {
        if #available(iOS 26.0, *) {
            GlassEffectContainer(spacing: spacing) { content() }
        } else {
            content()
        }
    }
}

/// How a surface is tinted.
enum LGTint: Equatable {
    /// The standard glass, which adapts fully to what is behind it.
    case regular
    /// Very transparent glass, for small controls over the game.
    case clear
    /// Standard glass with a color that means something (an active state, a warning).
    case color(Color)
    /// The clear glass of the on-screen controls with their tint setting (0 is ultra clear).
    case control(Double)
}

private struct LGSurface<S: Shape>: ViewModifier {
    let shape: S
    let tint: LGTint
    let interactive: Bool

    func body(content: Content) -> some View {
        if #available(iOS 26.0, *) {
            content.glassEffect(glass, in: shape)
        } else {
            content
                .background(tint == .clear || isControl ? Material.ultraThinMaterial : Material.regularMaterial,
                            in: shape)
                .overlay { fallbackTint }
                .overlay { LGSpecularBorder(shape: shape) }
        }
    }

    private var isControl: Bool {
        if case .control = tint { return true }
        return false
    }

    @available(iOS 26.0, *)
    private var glass: Glass {
        let base: Glass
        switch tint {
        case .regular: base = .regular
        case .clear: base = .clear
        case .control(let amount):
            base = amount <= 0.02 ? .clear : .clear.tint(Color.white.opacity(amount * 0.35))
        case .color(let color): base = .regular.tint(color)
        }
        return interactive ? base.interactive() : base
    }

    @ViewBuilder private var fallbackTint: some View {
        if case .color(let color) = tint {
            shape.fill(color.opacity(0.25)).allowsHitTesting(false)
        }
    }
}

extension View {
    /// A floating glass surface in the given shape. `interactive` is for surfaces that respond to
    /// touch themselves and are not buttons (buttons with a glass style already do).
    func lgGlass<S: Shape>(in shape: S, tint: LGTint = .regular,
                           interactive: Bool = false) -> some View {
        modifier(LGSurface(shape: shape, tint: tint, interactive: interactive))
    }

    /// Ties a glass surface to another one with the same id in the same container: when one
    /// leaves and the other enters inside an animation, the glass morphs between them.
    func lgGlassID<ID: Hashable & Sendable>(_ id: ID, in namespace: Namespace.ID) -> some View {
        modifier(LGMorphID(id: id, namespace: namespace))
    }

    /// The enter and leave transition of a glass surface. Glass materializes by itself; only the
    /// material fallback gets a transition of its own.
    func lgPanelTransition(anchor: UnitPoint = .center) -> some View {
        modifier(LGPanelTransition(anchor: anchor))
    }
}

private struct LGMorphID<ID: Hashable & Sendable>: ViewModifier {
    let id: ID
    let namespace: Namespace.ID

    func body(content: Content) -> some View {
        if #available(iOS 26.0, *) {
            content.glassEffectID(id, in: namespace)
        } else {
            content
        }
    }
}

private struct LGPanelTransition: ViewModifier {
    let anchor: UnitPoint

    func body(content: Content) -> some View {
        if #available(iOS 26.0, *) {
            content
        } else {
            content.transition(.scale(scale: 0.85, anchor: anchor).combined(with: .opacity))
        }
    }
}

/// The corner shape of panels and cards: a continuous rounded rectangle whose radius never
/// exceeds half its shorter side, so small panels become capsules instead of pinched corners.
struct LGPanelShape: Shape {
    var fallbackRadius: CGFloat = 28

    func path(in rect: CGRect) -> Path {
        RoundedRectangle(cornerRadius: min(fallbackRadius, min(rect.width, rect.height) / 2),
                         style: .continuous)
            .path(in: rect)
    }
}

/// A hairline light edge, brighter at the top left like light falling on the rim of glass. Used
/// by the material fallback and by content that wants a glass-like rim (cards). System glass has
/// its own edge and does not need it.
struct LGSpecularBorder<S: Shape>: View {
    let shape: S
    var lineWidth: CGFloat = 0.75

    var body: some View {
        shape
            .stroke(
                LinearGradient(
                    colors: [.white.opacity(0.55), .white.opacity(0.08), .white.opacity(0.25)],
                    startPoint: .topLeading, endPoint: .bottomTrailing),
                lineWidth: lineWidth)
            .allowsHitTesting(false)
    }
}

// MARK: - Bars

private struct LGTabBarMinimize: ViewModifier {
    func body(content: Content) -> some View {
        if #available(iOS 26.0, *) {
            content.tabBarMinimizeBehavior(.onScrollDown)
        } else {
            content
        }
    }
}

extension View {
    /// The glass tab bar shrinks out of the way while the content scrolls down (iOS 26). The tab
    /// bar, the navigation bar and the search field are the system's and are glass by themselves.
    func lgTabBarMinimizes() -> some View {
        modifier(LGTabBarMinimize())
    }
}

// MARK: - Settings screens

/// The colors of the backdrop, as hex strings (RRGGBB), and the ready-made sets of them.
enum BackdropPalette {
    static let defaultColors = ["5856D6", "30B0C7", "AF52DE"]

    static let presets: [(name: String, colors: [String])] = [
        ("Default", defaultColors),
        ("Ocean", ["0A84FF", "32ADE6", "64D2FF"]),
        ("Sunset", ["FF6B35", "FF2D55", "BF5AF2"]),
        ("Forest", ["34C759", "30B0C7", "A2845E"]),
        ("Candy", ["FF2D92", "5E5CE6", "64D2FF"]),
        ("Graphite", ["8E8E93", "636366", "48484A"]),
    ]
}

extension Color {
    /// A color from "RRGGBB" (the default is gray for anything that is not a color).
    init(hex: String) {
        var value: UInt64 = 0x808080
        Scanner(string: hex).scanHexInt64(&value)
        self.init(red: Double((value >> 16) & 0xFF) / 255, green: Double((value >> 8) & 0xFF) / 255,
                  blue: Double(value & 0xFF) / 255)
    }

    /// "RRGGBB" of the color in the sRGB space.
    var hexString: String {
        var red: CGFloat = 0, green: CGFloat = 0, blue: CGFloat = 0, alpha: CGFloat = 0
        UIColor(self).getRed(&red, green: &green, blue: &blue, alpha: &alpha)
        return String(format: "%02X%02X%02X", Int((red * 255).rounded()), Int((green * 255).rounded()),
                      Int((blue * 255).rounded()))
    }
}

/// A soft, still backdrop of color for the settings lists and the game list, so their translucent
/// rows and the glass bars have something to show through. The colors are set in Settings > App
/// Colors. It does not move, so it costs nothing to keep.
struct LGAmbientBackdrop: View {
    @Environment(\.colorScheme) private var colorScheme
    @EnvironmentObject private var settings: AppSettings

    var body: some View {
        GeometryReader { geometry in
            let size = max(geometry.size.width, geometry.size.height)
            ZStack {
                Color(uiColor: .systemGroupedBackground)
                Circle().fill(Color(hex: settings.backdropColor1))
                    .frame(width: size * 0.7).offset(x: -size * 0.3, y: -size * 0.35)
                Circle().fill(Color(hex: settings.backdropColor2))
                    .frame(width: size * 0.6).offset(x: size * 0.35, y: size * 0.05)
                Circle().fill(Color(hex: settings.backdropColor3))
                    .frame(width: size * 0.6).offset(x: -size * 0.15, y: size * 0.45)
            }
            .frame(width: geometry.size.width, height: geometry.size.height)
            .blur(radius: 90)
            .opacity((colorScheme == .dark ? 0.55 : 0.35) * Double(settings.backdropIntensity) / 100)
            .background(Color(uiColor: .systemGroupedBackground))
        }
        .ignoresSafeArea()
    }
}

/// The background of a row of a settings list: a material, so the backdrop shows through.
struct LGRowBackground: View {
    var body: some View {
        Rectangle().fill(.regularMaterial)
    }
}

private struct LGSettingsStyle: ViewModifier {
    @EnvironmentObject private var settings: AppSettings
    @State private var tick = 0

    func body(content: Content) -> some View {
        content
            .scrollContentBackground(.hidden)
            .background { LGAmbientBackdrop() }
            // Rows that appear and disappear with a setting move with a spring.
            .animation(LGMotion.panel, value: tick)
            .onReceive(settings.objectWillChange) { _ in tick &+= 1 }
    }
}

extension View {
    /// A settings list over the ambient backdrop, with spring motion for rows that come and go.
    func lgSettingsStyle() -> some View {
        modifier(LGSettingsStyle())
    }
}

// MARK: - Device tilt

/// How the device is held, for the parallax of the library cards. Runs only while something asks
/// for it, at a low rate, and not at all with Reduce Motion.
@MainActor
final class TiltMotion: ObservableObject {
    static let shared = TiltMotion()

    /// Left-right and forward-back tilt from where the device was when the motion started,
    /// each in -1...1.
    @Published private(set) var x: Double = 0
    @Published private(set) var y: Double = 0

    private let manager = CMMotionManager()
    private var reference: (roll: Double, pitch: Double)?
    private var users = 0

    func start() {
        users += 1
        guard users == 1, manager.isDeviceMotionAvailable,
              !UIAccessibility.isReduceMotionEnabled else { return }
        reference = nil
        manager.deviceMotionUpdateInterval = 1.0 / 30.0
        manager.startDeviceMotionUpdates(to: .main) { [weak self] motion, _ in
            guard let attitude = motion?.attitude else { return }
            let roll = attitude.roll
            let pitch = attitude.pitch
            Task { @MainActor in self?.update(roll: roll, pitch: pitch) }
        }
    }

    func stop() {
        users = max(0, users - 1)
        guard users == 0 else { return }
        manager.stopDeviceMotionUpdates()
        x = 0
        y = 0
    }

    private func update(roll: Double, pitch: Double) {
        guard users > 0 else { return }
        if reference == nil { reference = (roll, pitch) }
        guard let reference else { return }
        // About 25 degrees of tilt is the whole range. The resting position follows slowly, so
        // the cards settle back however the device is held.
        let range = 0.45
        let newX = max(-1, min(1, (roll - reference.roll) / range))
        let newY = max(-1, min(1, (pitch - reference.pitch) / range))
        self.reference = (reference.roll + (roll - reference.roll) * 0.02,
                          reference.pitch + (pitch - reference.pitch) * 0.02)
        if abs(newX - x) > 0.01 || abs(newY - y) > 0.01 {
            x = newX
            y = newY
        }
    }
}

// MARK: - Motion

enum LGMotion {
    /// Presses and small state changes: quick, with a little bounce.
    static let press = Animation.interpolatingSpring(stiffness: 420, damping: 24)
    /// Panels that open, close and morph.
    static let panel = Animation.interpolatingSpring(stiffness: 260, damping: 26)
}

// MARK: - Buttons

/// A button on glass: the system's glass button on iOS 26, a material capsule before.
struct LGButtonStyle: ButtonStyle {
    var prominent = false

    func makeBody(configuration: Configuration) -> some View {
        LGButtonBody(configuration: configuration, prominent: prominent)
    }

    private struct LGButtonBody: View {
        let configuration: ButtonStyleConfiguration
        let prominent: Bool
        @Environment(\.isEnabled) private var isEnabled

        var body: some View {
            configuration.label
                .font(.subheadline.weight(.semibold))
                .padding(.horizontal, 16)
                .padding(.vertical, 10)
                .foregroundStyle(prominent ? Color.white : Color.primary)
                .lgGlass(in: Capsule(), tint: prominent ? .color(.accentColor) : .regular,
                         interactive: true)
                .contentShape(Capsule())
                .scaleEffect(configuration.isPressed ? 0.95 : 1)
                .animation(LGMotion.press, value: configuration.isPressed)
                .saturation(isEnabled ? 1 : 0.3)
        }
    }
}

/// Content that tilts a little toward the finger while pressed, for cards.
struct LGTiltButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .scaleEffect(configuration.isPressed ? 0.96 : 1)
            .rotation3DEffect(.degrees(configuration.isPressed ? 6 : 0),
                              axis: (x: 1, y: -0.4, z: 0), perspective: 0.6)
            .animation(LGMotion.press, value: configuration.isPressed)
    }
}

// MARK: - The on-screen controller

/// How the on-screen controller is drawn, handed down through the environment.
struct ControlGlassStyle: Equatable {
    /// Liquid Glass controls (iOS 26). Off: the plain translucent controls.
    var enabled = false
    /// 0 is ultra clear glass, 1 the most tinted.
    var tint: Double = 0.3
}

private struct ControlGlassStyleKey: EnvironmentKey {
    static let defaultValue = ControlGlassStyle()
}

extension EnvironmentValues {
    var controlGlassStyle: ControlGlassStyle {
        get { self[ControlGlassStyleKey.self] }
        set { self[ControlGlassStyleKey.self] = newValue }
    }
}

/// The surface of a control of the on-screen controller. As glass it lights up and gives a little
/// under the finger; otherwise it is the translucent fill the controller always had.
private struct ControlSurface<S: Shape>: ViewModifier {
    let shape: S
    let pressed: Bool
    let lineWidth: CGFloat
    @Environment(\.controlGlassStyle) private var style

    func body(content: Content) -> some View {
        if #available(iOS 26.0, *), style.enabled {
            content
                .glassEffect(glass, in: shape)
                .scaleEffect(pressed ? 0.93 : 1)
                .animation(LGMotion.press, value: pressed)
        } else {
            content
                .background { shape.fill(Color.white.opacity(pressed ? 0.55 : 0.22)) }
                .overlay { shape.stroke(Color.white.opacity(0.6), lineWidth: lineWidth) }
        }
    }

    @available(iOS 26.0, *)
    private var glass: Glass {
        // The clear glass: the regular one adapts to what is behind it and flips between a dark
        // and a light look as the game's picture changes, which shows as flashes over a game.
        if pressed {
            // Energized: more light in the glass while it is held.
            return .clear.tint(Color.white.opacity(0.4))
        }
        if style.tint <= 0.02 {
            return .clear
        }
        return .clear.tint(Color.white.opacity(style.tint * 0.35))
    }
}

extension View {
    func controlSurface<S: Shape>(_ shape: S, pressed: Bool, lineWidth: CGFloat = 1.5) -> some View {
        modifier(ControlSurface(shape: shape, pressed: pressed, lineWidth: lineWidth))
    }
}

/// Whether this system has Liquid Glass.
enum LiquidGlass {
    static var isAvailable: Bool {
        if #available(iOS 26.0, *) { return true }
        return false
    }
}

// MARK: - Showcase

/// The components by themselves, over a colorful backdrop so the glass has something to refract.
struct LiquidGlassShowcase: View {
    @State private var expanded = false
    @State private var pressed = false
    @State private var tint = 0.3
    @Namespace private var namespace

    var body: some View {
        ZStack {
            LinearGradient(colors: [.indigo, .teal, .orange], startPoint: .topLeading,
                           endPoint: .bottomTrailing)
                .ignoresSafeArea()
            Circle().fill(.pink).frame(width: 220).offset(x: -90, y: -220)
            Circle().fill(.yellow).frame(width: 160).offset(x: 110, y: 160)

            VStack(spacing: 28) {
                Text("Liquid Glass").font(.largeTitle.bold()).foregroundStyle(.white)

                LGContainer(spacing: 0) {
                    HStack(spacing: 14) {
                        Button("Glass") {}.buttonStyle(LGButtonStyle())
                        Button("Prominent") {}.buttonStyle(LGButtonStyle(prominent: true))
                    }
                }

                // Controller controls: tap to see the pressed state, drag the slider for the tint.
                LGContainer(spacing: 0) {
                    HStack(spacing: 18) {
                        Text("A")
                            .font(.system(size: 22, weight: .bold, design: .rounded))
                            .foregroundStyle(.white)
                            .frame(width: 56, height: 56)
                            .controlSurface(Circle(), pressed: pressed)
                        Text("ZL")
                            .font(.system(size: 15, weight: .bold, design: .rounded))
                            .foregroundStyle(.white)
                            .frame(width: 64, height: 32)
                            .controlSurface(Capsule(), pressed: pressed)
                    }
                }
                .environment(\.controlGlassStyle, ControlGlassStyle(enabled: true, tint: tint))
                .onTapGesture { pressed.toggle() }
                Slider(value: $tint, in: 0...1).frame(width: 220)

                // A pill that morphs into a panel.
                LGContainer(spacing: 0) {
                    if expanded {
                        VStack(spacing: 10) {
                            Text("Panel").font(.headline)
                            Text("Tap to close").font(.caption).foregroundStyle(.secondary)
                        }
                        .frame(width: 240, height: 120)
                        .lgGlass(in: LGPanelShape())
                        .lgGlassID("demo", in: namespace)
                        .lgPanelTransition()
                    } else {
                        Image(systemName: "ellipsis")
                            .font(.title3.weight(.bold))
                            .frame(width: 52, height: 52)
                            .lgGlass(in: Circle())
                            .lgGlassID("demo", in: namespace)
                            .lgPanelTransition()
                    }
                }
                .contentShape(Rectangle())
                .onTapGesture { withAnimation(LGMotion.panel) { expanded.toggle() } }
            }
            .padding()
        }
    }
}

#Preview("Liquid Glass components") {
    LiquidGlassShowcase()
}
