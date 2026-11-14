// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

import SwiftUI
import UIKit

/// Sizes and default positions of the controls for a given screen size.
struct ControllerMetrics {
    let size: CGSize

    private let edge: CGFloat = 16
    private let bottom: CGFloat = 12

    var portrait: Bool { size.height > size.width }
    var unit: CGFloat { min(size.width, size.height) * (portrait ? 0.14 : 0.12) }

    func baseSize(of id: ControlID) -> CGSize {
        switch id {
        case .shoulderLeft, .shoulderRight: return CGSize(width: 64, height: 72)
        case .circlePad, .dpad: return CGSize(width: unit * 2.1, height: unit * 2.1)
        case .faceButtons: return CGSize(width: unit * 2.2, height: unit * 2.2)
        case .cStick: return CGSize(width: unit * 1.2, height: unit * 1.2)
        case .systemButtons: return CGSize(width: 152, height: 26)
        case .menuButton: return CGSize(width: 44, height: 44)
        case .turboButton: return CGSize(width: 64, height: 32)
        case .swapButton: return CGSize(width: 44, height: 44)
        }
    }

    func defaultCenter(of id: ControlID) -> CGPoint {
        let shoulderTop: CGFloat = portrait ? size.height * 0.52 : 56
        switch id {
        case .shoulderLeft: return CGPoint(x: edge + 32, y: shoulderTop + 36)
        case .shoulderRight: return CGPoint(x: size.width - edge - 32, y: shoulderTop + 36)
        case .dpad:
            return CGPoint(x: edge + unit * 1.05, y: size.height - bottom - unit * 1.05)
        case .circlePad:
            return CGPoint(x: edge + unit * 1.05, y: size.height - bottom - unit * 3.5)
        case .faceButtons:
            return CGPoint(x: size.width - edge - unit * 1.1, y: size.height - bottom - unit * 2.65)
        case .cStick:
            return CGPoint(x: size.width - edge - unit * 1.1, y: size.height - bottom - unit * 0.6)
        case .systemButtons:
            return CGPoint(x: size.width / 2, y: size.height - bottom - 17)
        case .menuButton:
            // Top right, where the menu button always was.
            return CGPoint(x: size.width - 8 - 22, y: 22)
        case .swapButton:
            // Top right, to the left of the menu button.
            return CGPoint(x: size.width - 8 - 22 - 52, y: 22)
        case .turboButton:
            // Below the right shoulder buttons.
            let shoulderTop: CGFloat = portrait ? size.height * 0.52 : 56
            return CGPoint(x: size.width - edge - 32, y: shoulderTop + 36 + 112)
        }
    }

    /// The frame of a control, kept fully on screen.
    func frame(of id: ControlID, placement: ControlPlacement) -> CGRect {
        let base = baseSize(of: id)
        let scale = CGFloat(placement.scale)
        let width = base.width * scale
        let height = base.height * scale
        let center = defaultCenter(of: id)
        let x = max(width / 2, min(center.x + CGFloat(placement.dx) * size.width,
                                   size.width - width / 2))
        let y = max(height / 2, min(center.y + CGFloat(placement.dy) * size.height,
                                    size.height - height / 2))
        return CGRect(x: x - width / 2, y: y - height / 2, width: width, height: height)
    }

    /// The placement with the offset corrected for what is actually possible on screen, so
    /// dragging past an edge does not build up an offset that is not visible.
    func clamped(_ placement: ControlPlacement, of id: ControlID) -> ControlPlacement {
        let frame = frame(of: id, placement: placement)
        let center = defaultCenter(of: id)
        var result = placement
        result.dx = Double((frame.midX - center.x) / size.width)
        result.dy = Double((frame.midY - center.y) / size.height)
        return result
    }
}

/// On-screen 3DS controller drawn on top of the emulated screens. Every part can be moved and
/// resized in the layout editor (`isEditing`).
struct ControllerOverlay: View {
    let opacity: Double
    let haptics: Bool
    /// Liquid Glass controls and their tint (see ControlGlassStyle).
    var glass = ControlGlassStyle()
    /// The Home button of the Select / Home / Start control.
    var showHomeButton = true
    @Binding var isEditing: Bool

    @EnvironmentObject private var layout: ControllerLayoutStore
    @StateObject private var touchState = ControllerTouchState()
    @State private var selected: ControlID?

    private var usesGlass: Bool { glass.enabled && LiquidGlass.isAvailable }

    var body: some View {
        GeometryReader { geometry in
            let metrics = ControllerMetrics(size: geometry.size)
            let portrait = metrics.portrait

            ZStack {
                if isEditing {
                    // Dims the game and keeps touches away from it while editing.
                    Color.black.opacity(0.4)
                        .contentShape(Rectangle())
                        .onTapGesture { selected = nil }
                }

                // Each control carries its own glass. No GlassEffectContainer here: a container
                // the size of the screen made the system capture the whole game picture behind
                // it, and that full-screen glass layer flashed over the game now and then.
                ZStack {
                    // The menu button is drawn by EmulationView, except while it is edited here.
                    ForEach(isEditing ? ControlID.allCases : ControlID.controllerControls) { id in
                        let placement = layout.placement(for: id, portrait: portrait)
                        let frame = metrics.frame(of: id, placement: placement)
                        if isEditing || !placement.hidden {
                            content(of: id, scale: CGFloat(placement.scale), unit: metrics.unit)
                                .frame(width: frame.width, height: frame.height)
                                .opacity(placement.hidden ? 0.3 : 1)
                                .position(x: frame.midX, y: frame.midY)
                        }
                    }
                }
                .environment(\.controlGlassStyle, glass)
                // Dark glass whatever the game shows: glass that follows the brightness behind it
                // would switch between dark and light as the picture changes.
                .environment(\.colorScheme, .dark)
                .allowsHitTesting(false)
                // Glass must not be faded (it would turn into a flat picture): its look is set
                // by the tint. The plain controls keep their opacity.
                .opacity(isEditing || usesGlass ? 1 : opacity)

                if !isEditing {
                    ControllerTouchLayer(
                        regions: ControlID.controllerControls.compactMap { id in
                            let placement = layout.placement(for: id, portrait: portrait)
                            if placement.hidden { return nil }
                            return TouchRegion(id: id,
                                               frame: metrics.frame(of: id, placement: placement),
                                               scale: CGFloat(placement.scale),
                                               showHome: showHomeButton)
                        },
                        haptics: haptics,
                        state: touchState)
                    .allowsHitTesting(false)
                }

                if isEditing {
                    ForEach(ControlID.allCases) { id in
                        EditHandle(id: id, metrics: metrics, isSelected: selected == id) {
                            selected = id
                        }
                    }
                    editorPanel(portrait: portrait)
                }
            }
        }
        .ignoresSafeArea(.keyboard)
        .onChange(of: isEditing) { editing in
            if !editing { selected = nil }
        }
    }

    @ViewBuilder
    private func content(of id: ControlID, scale: CGFloat, unit: CGFloat) -> some View {
        let pressed = touchState.pressed
        switch id {
        case .shoulderLeft:
            ShoulderPair(top: "ZL", bottom: "L", scale: scale,
                         topPressed: pressed.contains(.zl), bottomPressed: pressed.contains(.l))
        case .shoulderRight:
            ShoulderPair(top: "ZR", bottom: "R", scale: scale,
                         topPressed: pressed.contains(.zr), bottomPressed: pressed.contains(.r))
        case .circlePad:
            CirclePad(size: unit * 2.1 * scale, knob: touchState.sticks[.circlePad] ?? .zero)
        case .dpad:
            DPad(size: unit * 2.1 * scale, pressed: pressed)
        case .faceButtons:
            FaceButtons(size: unit * 2.2 * scale, pressed: pressed)
        case .cStick:
            CirclePad(size: unit * 1.2 * scale, knob: touchState.sticks[.cStick] ?? .zero)
        case .systemButtons:
            SystemButtons(scale: scale, pressed: pressed, showHome: showHomeButton)
        case .menuButton:
            MenuButtonIcon(scale: scale)
        case .turboButton:
            PillButton(label: "Turbo", scale: scale, pressed: touchState.turbo)
        case .swapButton:
            SwapButtonIcon(scale: scale)
        }
    }

    // MARK: Editor

    private func editorPanel(portrait: Bool) -> some View {
        VStack {
            VStack(spacing: 10) {
                if let selected {
                    let placement = layout.placement(for: selected, portrait: portrait)
                    Text(selected.title)
                        .font(.headline)
                    HStack(spacing: 10) {
                        Image(systemName: "minus.magnifyingglass")
                        Slider(
                            value: Binding(
                                get: { placement.scale },
                                set: { newScale in
                                    layout.update(selected, portrait: portrait) {
                                        $0.scale = newScale
                                    }
                                }),
                            in: ControlPlacement.scaleRange,
                            onEditingChanged: { editing in
                                if !editing { layout.commit() }
                            })
                        Image(systemName: "plus.magnifyingglass")
                        Text("\(Int((placement.scale * 100).rounded()))%")
                            .font(.callout.monospacedDigit())
                            .frame(width: 52, alignment: .trailing)
                    }
                } else {
                    Text("Drag a control to move it. Pinch it or pick it to resize or hide it.")
                        .font(.callout)
                        .multilineTextAlignment(.center)
                }
                if let selected, selected != .menuButton {
                    Toggle("Hide This Control", isOn: Binding(
                        get: { layout.placement(for: selected, portrait: portrait).hidden },
                        set: { hide in
                            layout.update(selected, portrait: portrait) { $0.hidden = hide }
                            layout.commit()
                        }))
                }
                if layout.hasActiveGame {
                    Toggle("Separate Layout for This Game", isOn: Binding(
                        get: { layout.usesGameLayout },
                        set: { layout.setUsesGameLayout($0) }))
                }
                HStack {
                    Button("Reset Control") {
                        if let selected { layout.reset(selected, portrait: portrait) }
                    }
                    .disabled(selected == nil)
                    Button("Reset All", role: .destructive) {
                        layout.resetAll(portrait: portrait)
                    }
                    Spacer()
                    Button("Done") { isEditing = false }
                        .buttonStyle(.borderedProminent)
                }
            }
            .padding(14)
            .frame(maxWidth: 400)
            .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 16))
            .padding(.top, 8)
            .padding(.horizontal, 8)
            Spacer()
        }
    }
}

/// Outline of a control in the layout editor. Dragging it moves the control, pinching resizes it.
private struct EditHandle: View {
    let id: ControlID
    let metrics: ControllerMetrics
    let isSelected: Bool
    let onSelect: () -> Void

    @EnvironmentObject private var layout: ControllerLayoutStore
    @State private var startPlacement: ControlPlacement?
    @State private var startScale: Double?

    var body: some View {
        let portrait = metrics.portrait
        let placement = layout.placement(for: id, portrait: portrait)
        let frame = metrics.frame(of: id, placement: placement)

        RoundedRectangle(cornerRadius: 12)
            .strokeBorder(
                isSelected ? Color.accentColor : Color.white.opacity(0.85),
                style: StrokeStyle(lineWidth: isSelected ? 3 : 1.5, dash: [6, 4])
            )
            .background(Color.white.opacity(isSelected ? 0.12 : 0.04),
                        in: RoundedRectangle(cornerRadius: 12))
            .frame(width: frame.width + 12, height: frame.height + 12)
            .contentShape(Rectangle())
            .position(x: frame.midX, y: frame.midY)
            .gesture(
                DragGesture(minimumDistance: 0, coordinateSpace: .global)
                    .onChanged { value in
                        if startPlacement == nil {
                            startPlacement = placement
                            onSelect()
                        }
                        guard let start = startPlacement else { return }
                        layout.update(id, portrait: portrait) {
                            $0.dx = start.dx + Double(value.translation.width / metrics.size.width)
                            $0.dy = start.dy + Double(value.translation.height / metrics.size.height)
                            $0 = metrics.clamped($0, of: id)
                        }
                    }
                    .onEnded { _ in
                        startPlacement = nil
                        layout.commit()
                    }
                    .simultaneously(
                        with: MagnificationGesture()
                            .onChanged { magnification in
                                if startScale == nil {
                                    startScale = placement.scale
                                    onSelect()
                                }
                                guard let start = startScale else { return }
                                layout.update(id, portrait: portrait) {
                                    $0.scale = start * Double(magnification)
                                    $0 = metrics.clamped($0, of: id)
                                }
                            }
                            .onEnded { _ in
                                startScale = nil
                                layout.commit()
                            })
            )
    }
}

// MARK: - Touch handling

/// What the on-screen controller is currently doing, for drawing it.
final class ControllerTouchState: ObservableObject {
    @Published fileprivate(set) var pressed: Set<ConsoleButton> = []
    /// Knob position of each stick, x and y in [-1, 1] with +y down (screen coordinates).
    @Published fileprivate(set) var sticks: [ConsoleStick: CGPoint] = [:]
    /// The turbo button is held.
    @Published fileprivate(set) var turbo = false
}

/// One control as seen by the touch view.
private struct TouchRegion {
    let id: ControlID
    let frame: CGRect
    let scale: CGFloat
    /// Whether the Home button of the system buttons is there (without it Select and Start split
    /// the control in two).
    var showHome = true
}

/// Handles every touch on the on-screen controller in UIKit instead of with SwiftUI gestures.
/// The view the game is drawn in receives all touches and offers each new one here first; the
/// ones that do not land on a control go to the emulated touch screen. Touches are handled as
/// soon as they arrive, any number of fingers work at once, a finger can slide from one button
/// to another, and a cancelled touch always lets go.
@MainActor
final class ControllerTouchHandler {
    static let shared = ControllerTouchHandler()

    /// Controls in window coordinates. Empty while the controller is hidden or being edited.
    fileprivate var regions: [TouchRegion] = [] {
        didSet { if regions.isEmpty { releaseAll() } }
    }
    fileprivate weak var state: ControllerTouchState?
    var haptics = true

    private enum Binding {
        case buttons(Set<ConsoleButton>)
        case stick(ConsoleStick, ControlID)
        case turbo
    }

    /// Called when the turbo button goes down or up.
    var onTurbo: ((Bool) -> Void)?
    private var turboHeld = false

    private var bindings: [ObjectIdentifier: Binding] = [:]
    private var pressed: Set<ConsoleButton> = []
    private let impact = UIImpactFeedbackGenerator(style: .light)

    private init() {}

    // MARK: Touch events

    /// Takes the touches that start on a control and returns the others.
    func began(_ touches: Set<UITouch>) -> Set<UITouch> {
        var unclaimed = Set<UITouch>()
        for touch in touches {
            let point = touch.location(in: nil)
            guard let region = region(at: point) else {
                unclaimed.insert(touch)
                continue
            }
            switch region.id {
            case .circlePad:
                bindings[ObjectIdentifier(touch)] = .stick(.circlePad, region.id)
                updateStick(for: touch)
            case .cStick:
                bindings[ObjectIdentifier(touch)] = .stick(.cStick, region.id)
                updateStick(for: touch)
            case .turboButton:
                bindings[ObjectIdentifier(touch)] = .turbo
                if haptics { impact.impactOccurred() }
            default:
                bindings[ObjectIdentifier(touch)] = .buttons(buttons(at: point))
            }
        }
        commit()
        return unclaimed
    }

    func moved(_ touches: Set<UITouch>) {
        var changed = false
        for touch in touches {
            guard let binding = bindings[ObjectIdentifier(touch)] else { continue }
            switch binding {
            case .buttons:
                bindings[ObjectIdentifier(touch)] = .buttons(buttons(at: touch.location(in: nil)))
                changed = true
            case .stick:
                updateStick(for: touch)
            case .turbo:
                break
            }
        }
        if changed {
            commit()
        }
    }

    /// For touches that ended or were cancelled.
    func ended(_ touches: Set<UITouch>) {
        var changed = false
        for touch in touches {
            guard let binding = bindings.removeValue(forKey: ObjectIdentifier(touch)) else {
                continue
            }
            changed = true
            if case let .stick(stick, _) = binding {
                stick.set(x: 0, y: 0)
                state?.sticks[stick] = nil
            }
        }
        if changed {
            commit()
        }
    }

    func releaseAll() {
        for case let .stick(stick, _) in bindings.values {
            stick.set(x: 0, y: 0)
        }
        bindings.removeAll()
        if state?.sticks.isEmpty == false {
            state?.sticks.removeAll()
        }
        commit()
    }

    /// Sends the buttons that changed to the emulator.
    private func commit() {
        let turbo = bindings.values.contains { if case .turbo = $0 { return true } else { return false } }
        if turbo != turboHeld {
            turboHeld = turbo
            state?.turbo = turbo
            onTurbo?(turbo)
        }
        var now: Set<ConsoleButton> = []
        for case let .buttons(buttons) in bindings.values {
            now.formUnion(buttons)
        }
        guard now != pressed else { return }
        for button in pressed.subtracting(now) {
            button.set(false)
        }
        let added = now.subtracting(pressed)
        for button in added {
            button.set(true)
        }
        if haptics && !added.isEmpty {
            impact.impactOccurred()
            impact.prepare()
        }
        pressed = now
        state?.pressed = now
    }

    // MARK: Geometry

    /// Touch area of a control: a little larger than what is drawn so presses near the edge
    /// still count.
    private func hitFrame(of region: TouchRegion) -> CGRect {
        let margin: CGFloat
        switch region.id {
        case .circlePad, .cStick: margin = 6
        default: margin = 10 * region.scale
        }
        return region.frame.insetBy(dx: -margin, dy: -margin)
    }

    private func region(at point: CGPoint) -> TouchRegion? {
        // Prefer a control that is actually under the finger over one whose margin it is in.
        regions.first(where: { $0.frame.contains(point) })
            ?? regions.first(where: { hitFrame(of: $0).contains(point) })
    }

    /// The buttons under a finger, looking at every button control so a finger can slide across.
    private func buttons(at point: CGPoint) -> Set<ConsoleButton> {
        guard let region = region(at: point) else { return [] }
        let frame = region.frame
        let local = CGPoint(x: point.x - frame.midX, y: point.y - frame.midY)
        switch region.id {
        case .shoulderLeft:
            return [local.y < 0 ? .zl : .l]
        case .shoulderRight:
            return [local.y < 0 ? .zr : .r]
        case .systemButtons:
            let x = point.x - frame.minX
            if !region.showHome { return [x < frame.width / 2 ? .select : .start] }
            let third = frame.width / 3
            return [x < third ? .select : (x < 2 * third ? .home : .start)]
        case .dpad:
            let deadzone = frame.width * 0.12
            var result: Set<ConsoleButton> = []
            if local.x < -deadzone { result.insert(.left) }
            if local.x > deadzone { result.insert(.right) }
            if local.y < -deadzone { result.insert(.up) }
            if local.y > deadzone { result.insert(.down) }
            return result
        case .faceButtons:
            return faceButtons(at: local, size: frame.width)
        case .circlePad, .cStick, .menuButton, .turboButton, .swapButton:
            return []
        }
    }

    /// Face buttons around a point relative to the middle of the cluster. A finger between two
    /// buttons presses both, like rolling a thumb across them on the real console.
    private func faceButtons(at local: CGPoint, size: CGFloat) -> Set<ConsoleButton> {
        let offset = size * FaceButtons.offset
        let radius = size * FaceButtons.diameter / 2
        let centers: [(ConsoleButton, CGPoint)] = [
            (.x, CGPoint(x: 0, y: -offset)), (.b, CGPoint(x: 0, y: offset)),
            (.y, CGPoint(x: -offset, y: 0)), (.a, CGPoint(x: offset, y: 0)),
        ]
        let distances = centers.map { entry in
            (entry.0, hypot(local.x - entry.1.x, local.y - entry.1.y))
        }
        let close = Set(distances.filter { $0.1 <= radius * 1.15 }.map { $0.0 })
        if !close.isEmpty {
            return close
        }
        // In the gap between the buttons: the nearest one.
        if let nearest = distances.min(by: { $0.1 < $1.1 }), nearest.1 <= radius * 1.6 {
            return [nearest.0]
        }
        return []
    }

    private func updateStick(for touch: UITouch) {
        guard case let .stick(stick, id)? = bindings[ObjectIdentifier(touch)],
              let region = regions.first(where: { $0.id == id }) else { return }
        let point = touch.location(in: nil)
        let radius = region.frame.width / 2
        var dx = (point.x - region.frame.midX) / radius
        var dy = (point.y - region.frame.midY) / radius
        let length = hypot(dx, dy)
        if length > 1 {
            dx /= length
            dy /= length
        }
        stick.set(x: Float(dx), y: Float(-dy))
        state?.sticks[stick] = CGPoint(x: dx, y: dy)
    }
}

/// Invisible view laid over the controller that tells the touch handler where the controls are
/// on screen. It does not take touches itself.
private final class ControllerRegionView: UIView {
    var regions: [TouchRegion] = [] {
        didSet { publish() }
    }

    override init(frame: CGRect) {
        super.init(frame: frame)
        isUserInteractionEnabled = false
        backgroundColor = .clear
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        publish()
    }

    override func didMoveToWindow() {
        super.didMoveToWindow()
        publish()
    }

    func publish() {
        guard window != nil, bounds.width > 0 else {
            ControllerTouchHandler.shared.regions = []
            return
        }
        ControllerTouchHandler.shared.regions = regions.map {
            TouchRegion(id: $0.id, frame: convert($0.frame, to: nil), scale: $0.scale,
                        showHome: $0.showHome)
        }
    }
}

private struct ControllerTouchLayer: UIViewRepresentable {
    let regions: [TouchRegion]
    let haptics: Bool
    let state: ControllerTouchState

    func makeUIView(context: Context) -> ControllerRegionView {
        ControllerRegionView()
    }

    func updateUIView(_ view: ControllerRegionView, context: Context) {
        let handler = ControllerTouchHandler.shared
        handler.haptics = haptics
        handler.state = state
        view.regions = regions
    }

    static func dismantleUIView(_ view: ControllerRegionView, coordinator: ()) {
        ControllerTouchHandler.shared.regions = []
    }
}

// MARK: - Drawing

private let pressedFill = Color.white.opacity(0.55)

/// Several separate rounded shapes (circles, capsules) drawn as one. As glass they are ONE surface
/// instead of one each: the system blurs and lights one surface per pass, so a cluster of buttons
/// costs a fraction of what the same buttons cost one by one, and they look the same, separate.
private struct ClusterShape: Shape {
    struct Part {
        /// In the points of the cluster, from its top left corner.
        var rect: CGRect
    }
    let parts: [Part]

    func path(in rect: CGRect) -> Path {
        var path = Path()
        for part in parts {
            let r = part.rect.offsetBy(dx: rect.minX, dy: rect.minY)
            let radius = min(r.width, r.height) / 2
            path.addRoundedRect(in: r, cornerSize: CGSize(width: radius, height: radius),
                                style: .continuous)
        }
        return path
    }
}

/// A cluster of buttons on one surface. The labels are drawn over the glass, and a button that
/// is held gets a light disc over the glass (the glass itself can only be tinted as a whole).
private struct ButtonCluster: View {
    struct Button {
        let rect: CGRect
        let pressed: Bool
        let label: AnyView
    }

    let size: CGSize
    let buttons: [Button]
    var lineWidth: CGFloat = 1.5

    var body: some View {
        ZStack(alignment: .topLeading) {
            ForEach(buttons.indices, id: \.self) { index in
                let button = buttons[index]
                ClusterShape(parts: [ClusterShape.Part(rect: button.rect)])
                    .fill(Color.white.opacity(button.pressed ? 0.33 : 0))
                    .animation(LGMotion.press, value: button.pressed)
                button.label
                    .scaleEffect(button.pressed ? 0.93 : 1)
                    .animation(LGMotion.press, value: button.pressed)
                    .frame(width: button.rect.width, height: button.rect.height)
                    .position(x: button.rect.midX, y: button.rect.midY)
            }
        }
        .frame(width: size.width, height: size.height, alignment: .topLeading)
        .controlSurface(ClusterShape(parts: buttons.map { ClusterShape.Part(rect: $0.rect) }),
                        pressed: false, lineWidth: lineWidth)
    }
}

private struct PillButton: View {
    let label: String
    let scale: CGFloat
    let pressed: Bool

    var body: some View {
        Text(label)
            .font(.system(size: 15 * scale, weight: .bold, design: .rounded))
            .foregroundStyle(.white)
            .frame(width: 64 * scale, height: 32 * scale)
            .controlSurface(Capsule(), pressed: pressed)
    }
}

/// ZL over L (or ZR over R): two capsules on one surface.
private struct ShoulderPair: View {
    let top: String
    let bottom: String
    let scale: CGFloat
    let topPressed: Bool
    let bottomPressed: Bool

    var body: some View {
        let width = 64 * scale, height = 32 * scale, gap = 8 * scale
        ButtonCluster(
            size: CGSize(width: width, height: height * 2 + gap),
            buttons: [
                ButtonCluster.Button(rect: CGRect(x: 0, y: 0, width: width, height: height),
                      pressed: topPressed, label: AnyView(label(top))),
                ButtonCluster.Button(rect: CGRect(x: 0, y: height + gap, width: width, height: height),
                      pressed: bottomPressed, label: AnyView(label(bottom))),
            ])
    }

    private func label(_ text: String) -> some View {
        Text(text)
            .font(.system(size: 15 * scale, weight: .bold, design: .rounded))
            .foregroundStyle(.white)
    }
}

/// Select, Home and Start: three capsules on one surface.
private struct SystemButtons: View {
    let scale: CGFloat
    let pressed: Set<ConsoleButton>
    var showHome = true

    var body: some View {
        let width = 40 * scale, height = 26 * scale, gap = 16 * scale
        // Without Home, Select and Start sit next to each other with the usual gap.
        let items: [(String, ConsoleButton)] = showHome
            ? [("minus", .select), ("house.fill", .home), ("plus", .start)]
            : [("minus", .select), ("plus", .start)]
        let columns = showHome ? 3 : 2
        ButtonCluster(
            size: CGSize(width: width * CGFloat(columns) + gap * CGFloat(columns - 1), height: height),
            buttons: items.enumerated().map { index, item in
                ButtonCluster.Button(rect: CGRect(x: CGFloat(index) * (width + gap), y: 0,
                                   width: width, height: height),
                      pressed: pressed.contains(item.1),
                      label: AnyView(Image(systemName: item.0)
                          .font(.system(size: 13 * scale, weight: .bold))
                          .foregroundStyle(.white)))
            },
            lineWidth: 1)
    }
}

private struct FaceButtons: View {
    /// Distance of each button from the middle and button diameter, relative to the cluster.
    static let offset: CGFloat = 0.32
    static let diameter: CGFloat = 0.36

    let size: CGFloat
    let pressed: Set<ConsoleButton>

    var body: some View {
        let button = size * Self.diameter
        let offset = size * Self.offset
        let middle = size / 2
        let items: [(String, ConsoleButton, CGFloat, CGFloat)] = [
            ("X", .x, 0, -offset), ("B", .b, 0, offset), ("Y", .y, -offset, 0), ("A", .a, offset, 0),
        ]
        ButtonCluster(
            size: CGSize(width: size, height: size),
            buttons: items.map { item in
                ButtonCluster.Button(rect: CGRect(x: middle + item.2 - button / 2, y: middle + item.3 - button / 2,
                                   width: button, height: button),
                      pressed: pressed.contains(item.1),
                      label: AnyView(Text(item.0)
                          .font(.system(size: button * 0.42, weight: .bold, design: .rounded))
                          .foregroundStyle(.white)))
            })
    }
}

private struct DPad: View {
    let size: CGFloat
    let pressed: Set<ConsoleButton>

    var body: some View {
        ZStack {
            ForEach([ConsoleButton.up, .down, .left, .right], id: \.self) { direction in
                let active = pressed.contains(direction)
                RoundedRectangle(cornerRadius: 6)
                    .fill(active ? pressedFill : Color.clear)
                    .frame(width: size * 0.34, height: size * 0.34)
                    .offset(offset(for: direction, distance: size * 0.33))
                Image(systemName: arrow(for: direction))
                    .font(.system(size: size * 0.16, weight: .bold))
                    .foregroundStyle(Color.white.opacity(active ? 1.0 : 0.7))
                    .offset(offset(for: direction, distance: size * 0.33))
            }
        }
        .frame(width: size, height: size)
        // The cross is one surface, so as glass it is one piece and not two bars over each other.
        // The arrows are its content, which is drawn on top of the glass.
        .controlSurface(DPadCross(), pressed: false, lineWidth: 0)
    }

    private func arrow(for direction: ConsoleButton) -> String {
        switch direction {
        case .up: return "arrowtriangle.up.fill"
        case .down: return "arrowtriangle.down.fill"
        case .left: return "arrowtriangle.left.fill"
        default: return "arrowtriangle.right.fill"
        }
    }

    private func offset(for direction: ConsoleButton, distance: CGFloat) -> CGSize {
        switch direction {
        case .up: return CGSize(width: 0, height: -distance)
        case .down: return CGSize(width: 0, height: distance)
        case .left: return CGSize(width: -distance, height: 0)
        default: return CGSize(width: distance, height: 0)
        }
    }
}

/// The outline of the D-pad: two rounded bars as one shape.
private struct DPadCross: Shape {
    func path(in rect: CGRect) -> Path {
        let arm = rect.width * 0.34
        var path = Path()
        path.addRoundedRect(
            in: CGRect(x: rect.midX - arm / 2, y: rect.minY, width: arm, height: rect.height),
            cornerSize: CGSize(width: 6, height: 6))
        path.addRoundedRect(
            in: CGRect(x: rect.minX, y: rect.midY - arm / 2, width: rect.width, height: arm),
            cornerSize: CGSize(width: 6, height: 6))
        return path
    }
}

/// Analog stick used for the circle pad and the C-stick.
private struct CirclePad: View {
    let size: CGFloat
    /// Knob position, x and y in [-1, 1].
    let knob: CGPoint

    var body: some View {
        // The knob is the content of the surface, so it is drawn on top of the glass. It is a
        // plain disc: glass on glass would have nothing to refract.
        Circle()
            .fill(Color.white.opacity(0.5))
            .overlay { Circle().stroke(Color.white.opacity(0.7), lineWidth: 1) }
            .frame(width: size * 0.5, height: size * 0.5)
            .offset(x: knob.x * size / 2, y: knob.y * size / 2)
            .frame(width: size, height: size)
            .controlSurface(Circle(), pressed: false)
    }
}

/// The icon of the emulation menu button, shared by EmulationView and the layout editor.
struct SwapButtonIcon: View {
    let scale: CGFloat
    /// Drawn on a glass disc, which is its outline: a little smaller.
    var onGlass = false

    var body: some View {
        Image(systemName: "rectangle.2.swap")
            .font(.system(size: (onGlass ? 18 : 24) * scale, weight: onGlass ? .semibold : .regular))
            .symbolRenderingMode(.hierarchical)
            .foregroundStyle(.white)
    }
}

struct MenuButtonIcon: View {
    let scale: CGFloat
    /// Drawn on a glass disc: only the dots, the disc is the circle.
    var onGlass = false

    var body: some View {
        Image(systemName: onGlass ? "ellipsis" : "ellipsis.circle.fill")
            .font(.system(size: (onGlass ? 19 : 28) * scale, weight: onGlass ? .bold : .regular))
            .symbolRenderingMode(.hierarchical)
            .foregroundStyle(.white)
    }
}
