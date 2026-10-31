// Licensed under GPLv2 or any later version.
#pragma once

#include <optional>

#include "kiu_qol/kiu_types.h"

namespace KiuQol {

/// Turns relative motion (mouse deltas, right-stick velocity) into a held stylus drag on the
/// bottom screen, which is how the game reads aiming and camera turns.
///
/// When the stylus nears an edge it has to be lifted and put back down in the middle. A lift
/// while moving reads as a flick (the camera keeps spinning), so before lifting the stylus is
/// held still for `settle_ms`. Motion that arrives during settle/lift is buffered and applied
/// once the stylus is back down, so no input is lost.
class VirtualStylus {
public:
    void Configure(const StylusConfig& cfg) { cfg_ = cfg; }

    /// Mouse motion in framebuffer px; scaled by mouse_sensitivity.
    void AddMouseDelta(float dx, float dy);
    /// Whether the mouse is currently driving aim (LMB held or pointer lock on).
    void SetMouseEngaged(bool engaged) { mouse_engaged_ = engaged; }

    /// Advances by dt seconds. stick_x/y in [-1, 1], +y = up.
    TouchOut Tick(float dt, float stick_x, float stick_y);

    /// Force the stylus up (e.g. a macro is about to take over). Buffered motion is dropped.
    void Reset();

    bool IsDown() const { return phase_ == Phase::Down || phase_ == Phase::Settling; }

private:
    enum class Phase { Idle, Down, Settling, Lifting };

    float ApplyCurve(float v) const;

    StylusConfig cfg_;
    Phase phase_ = Phase::Idle;
    float x_ = kBottomW / 2.0f, y_ = kBottomH / 2.0f;
    float pending_x_ = 0, pending_y_ = 0;
    float timer_ = 0;               ///< seconds left in Settling / Lifting
    float stick_idle_ = 1e9f;       ///< seconds since the stick was last outside the deadzone
    bool mouse_engaged_ = false;
};

/// Plays a sequence of taps (used to fire powers / press bottom-screen buttons from a key).
class MacroPlayer {
public:
    void Start(const MacroConfig& macro, bool stylus_was_down);
    bool Active() const { return macro_ != nullptr; }
    void Cancel() { macro_ = nullptr; }
    /// Returns the touch to report this tick; nullopt once the macro has finished.
    std::optional<TouchOut> Tick(float dt);

private:
    const MacroConfig* macro_ = nullptr;
    size_t step_ = 0;
    bool in_gap_ = false;
    float timer_ = 0;
    float pre_lift_ = 0; ///< seconds of "stylus up" before the first tap
};

} // namespace KiuQol
