// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include "common/common_types.h"

namespace AudioCore {

/// Sentinel meaning "do not filter", at the top of the setting's range. The range runs well
/// past Nyquist on purpose: the reference is divided by the speed, so reaching a mild cutoff at
/// 2x or 3x needs a reference above the rate itself.
constexpr u16 kSpeedupLowPassOff = 48000;

/// Speed used when the frame limiter is off; Settings::GetFrameLimit() returns 0 for unlimited.
constexpr double kUnlimitedSpeed = 10.0;

/// Lowest cutoff the low-pass will be asked for, in Hz.
constexpr double kMinLowPassCutoff = 200.0;

/// Whether a speed other than normal was requested. Keyed off the requested speed, not the
/// achieved one: the host falling behind is the stretcher's job (audio_core/stretch_gate.h),
/// and dulling the output because the host stumbled is not what this is for.
inline bool SpeedupIsOffSpeed(double speed) {
    return std::fabs(speed - 1.0) > 0.01;
}

/// Low-pass cutoff, in Hz (wide_open = transparent); reference/speed gives the applied cutoff.
/// Zero reads as off too: it would clamp to the floor, the strongest filter rather than none.
inline double SpeedupLowPassCutoff(double speed, u16 reference, double wide_open) {
    if (wide_open <= kMinLowPassCutoff) {
        return wide_open;
    }
    if (speed <= 1.0) {
        return wide_open;
    }
    if (reference == 0 || reference >= kSpeedupLowPassOff) {
        return wide_open;
    }

    return std::clamp(reference / speed, kMinLowPassCutoff, wide_open);
}

/// Fourth-order Butterworth low-pass: two cascaded RBJ biquads, stereo with independent state
/// per channel. The cutoff is smoothed rather than jumped, so engaging fast-forward slides the
/// filter shut instead of stepping the coefficients, which would click. At wide open the output
/// is left untouched.
class SpeedupLowPass {
public:
    // Time constant of the cutoff smoother, in seconds.
    static constexpr double kSmoothingTau = 0.05;
    // Fraction of wide-open at which the filter stops touching the output.
    static constexpr double kBypassThreshold = 0.995;
    // Section Q's for a fourth-order Butterworth cascade.
    static constexpr double kSectionQ[2] = {0.54119610014619698, 1.3065629648763766};
    // Lowest cutoff the coefficient design will accept.
    static constexpr double kMinCutoff = 20.0;

    void Init(double sample_rate_) {
        sample_rate = sample_rate_;
        wide_open = std::max(0.45 * sample_rate, kMinCutoff);
        for (int s = 0; s < 2; s++) {
            stages[s].z1[0] = stages[s].z1[1] = 0.0;
            stages[s].z2[0] = stages[s].z2[1] = 0.0;
        }
        SetCutoffNow(wide_open);
    }

    double WideOpenCutoff() const {
        return wide_open;
    }
    double Cutoff() const {
        return cur_cutoff;
    }
    bool Bypassed() const {
        return cur_cutoff >= (wide_open * kBypassThreshold);
    }

    /// Advance the smoothed cutoff by one block, then filter in place. The coefficients are
    /// interpolated across the block rather than replaced in one go: a transposed direct-form
    /// biquad's state encodes its past under the coefficients that produced it, so a step leaves
    /// the two inconsistent and the filter rings at every block edge while the cutoff slides.
    void Process(s16* samples, std::size_t num_frames, double target_hz, double block_seconds) {
        if (num_frames == 0) {
            return;
        }

        double from[2][5];
        double to[2][5];
        for (int s = 0; s < 2; s++) {
            stages[s].Snapshot(from[s]);
        }
        Smooth(target_hz, block_seconds);
        for (int s = 0; s < 2; s++) {
            stages[s].Snapshot(to[s]);
        }
        const bool bypass = Bypassed();

        for (std::size_t i = 0; i < num_frames; i++) {
            const double t = static_cast<double>(i + 1) / static_cast<double>(num_frames);
            for (int s = 0; s < 2; s++) {
                stages[s].Lerp(from[s], to[s], t);
            }
            for (std::size_t ch = 0; ch < 2; ch++) {
                // Runs even when bypassed: its state must stay in step with the signal, or
                // re-engaging would click.
                const double y = ProcessSample(samples[(i * 2) + ch], ch);
                if (!bypass) {
                    samples[(i * 2) + ch] = Saturate(y);
                }
            }
        }

        // Land exactly on the designed set, so rounding in the interpolation cannot accumulate
        // across blocks.
        for (int s = 0; s < 2; s++) {
            stages[s].Restore(to[s]);
        }
    }

    void Smooth(double target_hz, double block_seconds) {
        target_hz = std::clamp(target_hz, kMinCutoff, wide_open);
        const double a = 1.0 - std::exp(-block_seconds / kSmoothingTau);
        SetCutoffNow(cur_cutoff + ((target_hz - cur_cutoff) * a));
    }

    void SetCutoffNow(double cutoff_hz) {
        cur_cutoff = std::clamp(cutoff_hz, kMinCutoff, wide_open);
        for (int s = 0; s < 2; s++) {
            stages[s].Design(cur_cutoff, sample_rate, kSectionQ[s]);
        }
    }

    double ProcessSample(double x, std::size_t ch) {
        double y = x;
        for (int s = 0; s < 2; s++) {
            y = stages[s].Run(y, ch);
        }
        return y;
    }

private:
    static s16 Saturate(double y) {
        long v = std::lround(y);
        if (v > 32767) {
            v = 32767;
        }
        if (v < -32768) {
            v = -32768;
        }
        return static_cast<s16>(v);
    }

    struct Biquad {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1[2] = {0.0, 0.0};
        double z2[2] = {0.0, 0.0};

        void Design(double cutoff_hz, double sample_rate, double q) {
            const double w0 = 2.0 * std::numbers::pi * (cutoff_hz / sample_rate);
            const double cw = std::cos(w0);
            const double alpha = std::sin(w0) / (2.0 * q);
            const double a0 = 1.0 + alpha;

            b0 = ((1.0 - cw) * 0.5) / a0;
            b1 = (1.0 - cw) / a0;
            b2 = b0;
            a1 = (-2.0 * cw) / a0;
            a2 = (1.0 - alpha) / a0;
        }

        // Transposed direct form II.
        double Run(double x, std::size_t ch) {
            const double y = (b0 * x) + z1[ch];
            z1[ch] = (b1 * x) - (a1 * y) + z2[ch];
            z2[ch] = (b2 * x) - (a2 * y);
            return y;
        }

        void Snapshot(double out[5]) const {
            out[0] = b0;
            out[1] = b1;
            out[2] = b2;
            out[3] = a1;
            out[4] = a2;
        }

        void Restore(const double in[5]) {
            b0 = in[0];
            b1 = in[1];
            b2 = in[2];
            a1 = in[3];
            a2 = in[4];
        }

        void Lerp(const double from[5], const double to[5], double t) {
            b0 = from[0] + ((to[0] - from[0]) * t);
            b1 = from[1] + ((to[1] - from[1]) * t);
            b2 = from[2] + ((to[2] - from[2]) * t);
            a1 = from[3] + ((to[3] - from[3]) * t);
            a2 = from[4] + ((to[4] - from[4]) * t);
        }
    };

    double sample_rate = 48000.0;
    double wide_open = 21600.0;
    double cur_cutoff = 21600.0;
    Biquad stages[2];
};

} // namespace AudioCore
