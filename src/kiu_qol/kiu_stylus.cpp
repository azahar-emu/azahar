// Licensed under GPLv2 or any later version.
#include "kiu_qol/kiu_stylus.h"

#include <algorithm>
#include <cmath>

namespace KiuQol {

namespace {
constexpr float kCenterX = kBottomW / 2.0f;
constexpr float kCenterY = kBottomH / 2.0f;
// Buffered motion beyond this is discarded so a huge mouse jerk during a recenter can't slam
// the stylus straight into the opposite edge.
constexpr float kMaxPendingX = kBottomW * 0.4f;
constexpr float kMaxPendingY = kBottomH * 0.4f;
} // namespace

void VirtualStylus::AddMouseDelta(float dx, float dy) {
    pending_x_ += dx * cfg_.mouse_sensitivity;
    pending_y_ += dy * cfg_.mouse_sensitivity;
}

float VirtualStylus::ApplyCurve(float v) const {
    const float a = std::fabs(v);
    if (a <= cfg_.stick_deadzone)
        return 0.0f;
    const float n = std::min(1.0f, (a - cfg_.stick_deadzone) / (1.0f - cfg_.stick_deadzone));
    return std::copysign(std::pow(n, cfg_.stick_curve), v);
}

void VirtualStylus::Reset() {
    phase_ = Phase::Idle;
    pending_x_ = pending_y_ = 0;
    x_ = kCenterX;
    y_ = kCenterY;
}

TouchOut VirtualStylus::Tick(float dt, float stick_x, float stick_y) {
    const float sx = ApplyCurve(stick_x);
    const float sy = ApplyCurve(cfg_.invert_y ? stick_y : -stick_y); // stick up = stylus up
    const bool stick_active = sx != 0.0f || sy != 0.0f;
    stick_idle_ = stick_active ? 0.0f : stick_idle_ + dt;
    pending_x_ += sx * cfg_.stick_speed * dt;
    pending_y_ += sy * cfg_.stick_speed * dt;
    pending_x_ = std::clamp(pending_x_, -kMaxPendingX, kMaxPendingX);
    pending_y_ = std::clamp(pending_y_, -kMaxPendingY, kMaxPendingY);

    const bool engaged =
        mouse_engaged_ || stick_idle_ * 1000.0f < static_cast<float>(cfg_.stick_release_ms);

    switch (phase_) {
    case Phase::Idle:
        if (!engaged) {
            pending_x_ = pending_y_ = 0;
            return {x_, y_, false};
        }
        x_ = kCenterX;
        y_ = kCenterY;
        phase_ = Phase::Down;
        // Touch down at center this tick; motion applies from the next tick so the game sees
        // a clean press before the drag starts.
        return {x_, y_, true};

    case Phase::Down: {
        if (!engaged) {
            phase_ = Phase::Idle;
            pending_x_ = pending_y_ = 0;
            return {x_, y_, false};
        }
        x_ += pending_x_;
        y_ += pending_y_;
        pending_x_ = pending_y_ = 0;
        x_ = std::clamp(x_, 0.0f, kBottomW - 1.0f);
        y_ = std::clamp(y_, 0.0f, kBottomH - 1.0f);
        const float m = cfg_.edge_margin;
        if (x_ < m || y_ < m || x_ > kBottomW - 1 - m || y_ > kBottomH - 1 - m) {
            phase_ = Phase::Settling;
            timer_ = cfg_.settle_ms / 1000.0f;
        }
        return {x_, y_, true};
    }

    case Phase::Settling:
        timer_ -= dt;
        if (timer_ <= 0) {
            phase_ = Phase::Lifting;
            timer_ = cfg_.lift_ms / 1000.0f;
            return {x_, y_, false};
        }
        return {x_, y_, true}; // held still; motion keeps buffering

    case Phase::Lifting:
        timer_ -= dt;
        if (timer_ > 0)
            return {x_, y_, false};
        if (!engaged) {
            phase_ = Phase::Idle;
            pending_x_ = pending_y_ = 0;
            return {x_, y_, false};
        }
        x_ = kCenterX;
        y_ = kCenterY;
        phase_ = Phase::Down;
        return {x_, y_, true};
    }
    return {x_, y_, false};
}

void MacroPlayer::Start(const MacroConfig& macro, bool stylus_was_down) {
    if (macro.steps.empty())
        return;
    macro_ = &macro;
    step_ = 0;
    in_gap_ = false;
    timer_ = macro.steps[0].hold_ms / 1000.0f;
    // If the aim stylus was down, lift it for a couple of frames first; otherwise the game
    // would read the jump to the tap position as a drag.
    pre_lift_ = stylus_was_down ? 0.034f : 0.0f;
}

std::optional<TouchOut> MacroPlayer::Tick(float dt) {
    if (!macro_)
        return std::nullopt;
    if (pre_lift_ > 0) {
        pre_lift_ -= dt;
        return TouchOut{0, 0, false};
    }
    const TapStep& s = macro_->steps[step_];
    const TouchOut out{static_cast<float>(s.x), static_cast<float>(s.y), !in_gap_};
    timer_ -= dt;
    if (timer_ <= 0) {
        if (!in_gap_) {
            in_gap_ = true;
            timer_ = s.gap_ms / 1000.0f;
            // A zero gap still needs one released tick so consecutive taps register.
            if (timer_ <= 0)
                timer_ = 1e-6f;
        } else if (++step_ < macro_->steps.size()) {
            in_gap_ = false;
            timer_ = macro_->steps[step_].hold_ms / 1000.0f;
        } else {
            macro_ = nullptr;
        }
    }
    return out;
}

} // namespace KiuQol
