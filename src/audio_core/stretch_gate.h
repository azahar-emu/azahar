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
/// seconds with the ratio near one, then Drain lowers the backlog at a bounded tempo, and
/// Handover returns to Bypass once what is left fits a callback. Turning stretching off forces
/// the drain from wherever it stands, and aborts a warm-up outright.
class StretchGate {
public:
    enum class Mode { Bypass, Warming, Stretch, Drain };
    enum class Edge { None, Engage, Sync, Abort, Handover };

    struct Input {
        double speed;           // smoothed arrival / requested
        std::size_t buffered;   // fifo + stash, frames, at the start of the callback
        std::size_t backlog;    // stretcher output backlog, frames
        double ratio;           // stretcher ratio
        bool enabled;           // enable_audio_stretching
        bool prefilling;        // Bypass has not reached its fill target yet
        bool silenced;          // core_silenced
        bool synced;            // Warming: the stretcher's output covers the raw play point
        std::size_t num_frames; // this callback
    };

    static constexpr double kEngageLow = 0.95;
    static constexpr double kEngageHigh = 1.05;
    static constexpr double kDisengageBand = 0.01;
    static constexpr double kRatioBand = 0.03;
    static constexpr double kDrainMinRatio = 1.0;
    static constexpr double kDrainMaxRatio = 1.1;
    /// Two seconds of callbacks at 32728 Hz.
    static constexpr std::size_t kDwellFrames = 65456;
    /// Half a second: about two stretcher rounds at the slowest speed a one-period-per-callback
    /// warm-up can outrun. Past it the switch is forced and plays a short replay instead.
    static constexpr std::size_t kWarmTimeoutFrames = 16364;

    Edge Update(const Input& in) {
        if (in.silenced) {
            return Edge::None;
        }
        switch (mode) {
        case Mode::Bypass:
            if (!in.enabled) {
                return Edge::None;
            }
            if (in.speed < kEngageLow || in.speed > kEngageHigh ||
                (!in.prefilling && in.buffered < in.num_frames)) {
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
            in_band_frames = InBand(in.speed) ? in_band_frames + in.num_frames : 0;
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
            if (in.backlog <= in.num_frames && in.ratio >= kDrainMinRatio - 1e-9 &&
                in.ratio <= kDrainMaxRatio + 1e-9) {
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
        return std::abs(speed - 1.0) < kDisengageBand;
    }

    Mode mode = Mode::Bypass;
    std::size_t in_band_frames = 0;
    std::size_t warm_frames = 0;
    bool drain_forced = false;
};

} // namespace AudioCore
