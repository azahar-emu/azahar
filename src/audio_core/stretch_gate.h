// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cmath>
#include <cstddef>

namespace AudioCore {

/// Decides, once per callback, whether output comes straight from the FIFO or through the
/// time stretcher, and drives the transitions between the two. Pure: it reads the numbers it
/// is given and reports edges; OutputPipeline::RenderChunk() (audio_core/output_pipeline.cpp)
/// carries them out. Audio thread only.
///
/// Bypass is the rest state. Engage moves to Warming, where the raw path keeps playing while
/// the stretcher is primed and fed until its output covers the raw play point; Sync then
/// switches to Stretch. Stretch stays until speed has sat within a percent of full for two
/// seconds with the ratio near one, then Drain brings the stretcher's content down at a
/// bounded tempo, and Handover returns to Bypass once what is left is what Bypass wants to
/// start from: enough that the trough of the beat stays above the engage depth, and not so
/// much that the splicer has a lot to trim. Turning stretching off forces
/// the drain from wherever it stands, and aborts a warm-up outright.
class StretchGate {
public:
    enum class Mode { Bypass, Warming, Stretch, Drain };
    enum class Edge { None, Engage, Sync, Abort, Handover };

    struct Input {
        double speed;              // arrival / requested over the last ten seconds
        bool speed_settled;        // `speed` covers its whole window
        double speed_fast;         // the same over ~0.3 s: quicker, but it dips on burst gaps
        std::size_t buffered;      // fifo + stash, frames, at the start of the callback
        std::size_t low_water;     // Bypass engages below this depth, once prefilled
        std::size_t stretched;     // what a flush now would put in the stash: the frames
                                   // inside, input and output, less what a flush falls short
        std::size_t handover_low;  // Drain hands over with at least this much to flush...
        std::size_t handover_high; // ...and at most this much, unless the drain was forced
        double ratio;              // stretcher ratio
        bool enabled;              // enable_audio_stretching
        bool prefilling;           // Bypass has not reached its fill target yet
        bool silenced;             // core_silenced
        bool synced;               // Warming: the stretcher's output covers the raw play point
        std::size_t num_frames;    // this callback
    };

    static constexpr double kEngageLow = 0.95;
    static constexpr double kEngageHigh = 1.05;
    /// The fast estimate sees a burst gap as a dip to ~0.95 and a double burst as ~1.065, so
    /// it engages only well outside that.
    static constexpr double kEngageFast = 0.90;
    static constexpr double kEngageFastHigh = 1.10;
    /// Stretch drains toward Bypass only inside [kDisengageLow, kDisengageHigh). Bypass can
    /// trim a surplus but not cover a deficit, so the low side sits just under the least the
    /// slow estimate can read at true full speed: one burst short over its ten-second window
    /// is 0.9983 at any callback size, while a host at 99.5% reads at most 0.9967. Such a
    /// host stays stretched rather than cycling through a handover every ten seconds.
    static constexpr double kDisengageLow = 0.998;
    static constexpr double kDisengageHigh = 1.01;
    static constexpr double kRatioBand = 0.03;
    static constexpr double kDrainMinRatio = 1.0;
    static constexpr double kDrainMaxRatio = 1.1;
    /// Slack on the drain bounds, for a ratio clamped to them in floating point.
    static constexpr double kRatioEpsilon = 1e-9;
    /// Two seconds of callbacks at 32728 Hz.
    static constexpr std::size_t kDwellFrames = 65456;
    /// One second: room for the stretcher to build a round of reserve at the slowest speed a
    /// one-period-per-callback warm-up can outrun. Past it the switch is forced and plays a
    /// short replay instead.
    static constexpr std::size_t kWarmTimeoutFrames = 32728;

    Edge Update(const Input& in) {
        if (in.silenced) {
            return Edge::None;
        }
        switch (mode) {
        case Mode::Bypass:
            if (!in.enabled) {
                return Edge::None;
            }
            if (in.speed_fast < kEngageFast || in.speed_fast > kEngageFastHigh ||
                in.speed < kEngageLow || in.speed > kEngageHigh ||
                (!in.prefilling && in.buffered < in.low_water)) {
                mode = Mode::Warming;
                warm_frames = 0;
                return Edge::Engage;
            }
            return Edge::None;
        case Mode::Warming:
            if (!in.enabled) {
                mode = Mode::Bypass;
                return Edge::Abort;
            }
            warm_frames += in.num_frames;
            if (in.synced || warm_frames >= kWarmTimeoutFrames) {
                mode = Mode::Stretch;
                in_band_frames = 0;
                return Edge::Sync;
            }
            return Edge::None;
        case Mode::Stretch:
            if (!in.enabled) {
                mode = Mode::Drain;
                drain_forced = true;
                return Edge::None;
            }
            in_band_frames =
                in.speed_settled && InBand(in.speed) ? in_band_frames + in.num_frames : 0;
            if (in_band_frames >= kDwellFrames && std::abs(in.ratio - 1.0) < kRatioBand) {
                mode = Mode::Drain;
                drain_forced = false;
            }
            return Edge::None;
        case Mode::Drain:
            if (!in.enabled) {
                drain_forced = true;
            }
            if (!drain_forced && !InBand(in.speed)) {
                mode = Mode::Stretch;
                in_band_frames = 0;
                return Edge::None;
            }
            if ((drain_forced || in.stretched >= in.handover_low) &&
                in.stretched <= in.handover_high && in.ratio >= kDrainMinRatio - kRatioEpsilon &&
                in.ratio <= kDrainMaxRatio + kRatioEpsilon) {
                mode = Mode::Bypass;
                drain_forced = false;
                return Edge::Handover;
            }
            return Edge::None;
        }
        return Edge::None;
    }

    Mode CurrentMode() const {
        return mode;
    }

    void Reset() {
        *this = StretchGate{};
    }

private:
    static bool InBand(double speed) {
        return speed >= kDisengageLow && speed < kDisengageHigh;
    }

    Mode mode = Mode::Bypass;
    std::size_t in_band_frames = 0;
    std::size_t warm_frames = 0;
    bool drain_forced = false;
};

} // namespace AudioCore
