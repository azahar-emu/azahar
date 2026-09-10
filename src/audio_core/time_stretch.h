// Copyright 2016-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <vector>
#include "common/common_types.h"

namespace soundtouch {
class SoundTouch;
}

namespace AudioCore {

class TimeStretcher {
public:
    TimeStretcher();
    ~TimeStretcher();

    void SetOutputSampleRate(unsigned int sample_rate);

    /// @param in       Input sample buffer
    /// @param num_in   Number of input frames in `in`
    /// @param out      Output sample buffer
    /// @param num_out  Desired number of output frames in `out`
    /// @returns Actual number of frames written to `out`
    std::size_t Process(const s16* in, std::size_t num_in, s16* out, std::size_t num_out);

    void Clear();

    void Flush();

    /// The output backlog the servo in Process() steers toward, in seconds: half of what it
    /// tolerates before pushing back. 0.125 s by default, master's 50% of 0.25 s.
    void SetTargetBacklog(double seconds);
    /// Bounds on the ratio the servo may set. Applied to its own state, so lifting a bound
    /// later continues from the clamped value rather than jumping to where the servo wanted
    /// to be. (0.05, unbounded) by default; the floor lets boot silence through fast.
    void SetRatioBounds(double lo, double hi);
    double Ratio() const {
        return stretch_ratio;
    }

    /// Starts a priming: tempo 1.0, servo state reset. Returns the frames to Feed() from audio
    /// already played so that a first round runs inside the priming: SoundTouch's initial
    /// latency plus a little, since its rate transposer holds one frame back. Discard() what
    /// comes out; see OutputPipeline::Engage() (audio_core/output_pipeline.cpp). SoundTouch's
    /// output index tracks its input index one for one on average from the first frame (its
    /// first round trims the skip instead of the audio), within half a seek window, so after
    /// feeding `have` frames and dropping `dropped`, its next output frame is source frame
    /// `dropped`, counted from where the history began.
    std::size_t BeginPrime();
    /// Feeds input at the current tempo, without reading output or running the servo.
    void Feed(const s16* in, std::size_t num_in);
    /// Reads and drops up to `max_frames` of output. Returns the frames dropped.
    std::size_t Discard(std::size_t max_frames);
    /// Flushes: pads SoundTouch until everything it was fed has come out, reads it all into
    /// `out`, then clears. Returns the frames read; anything past `max_frames` is dropped with
    /// a warning.
    std::size_t FlushInto(s16* out, std::size_t max_frames);
    /// Processed frames waiting to be read.
    std::size_t OutputBacklog() const;
    /// Frames fed but not yet processed.
    std::size_t InputResidency() const;

private:
    std::size_t Put(const s16* in, std::size_t num_in);
    std::size_t Receive(s16* out, std::size_t num_out);

    static constexpr std::size_t kDiscardChunkFrames = 2048;

    std::unique_ptr<soundtouch::SoundTouch> sound_touch;
    double stretch_ratio = 1.0;
    double target_backlog_seconds = 0.125;
    double min_ratio = 0.05;
    double max_ratio = std::numeric_limits<double>::infinity();
    std::array<s16, kDiscardChunkFrames * 2> discard_scratch{};
    std::vector<float> recv_scratch; // the float build's output, converted
};

} // namespace AudioCore
