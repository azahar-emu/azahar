// Copyright 2016-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <vector>
#include "audio_core/frame_buffers.h"
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
    /// Sets the servo state outright, within the bounds: a stretcher that starts from a
    /// measured speed rather than 1.0 keeps its backlog from the first callback.
    void SetRatio(double ratio);
    /// Output frames one processing round produces at the current tempo: how much output the
    /// stretcher must hold to ride out the wait for its next round.
    std::size_t OutputBatchFrames() const;

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
    /// Flushes: pads SoundTouch with silence until everything it holds has come out, reads
    /// that into `out` and trims the padding back off, then clears. Ends on clean audio, or
    /// on the audio's own silence, at most FlushShortfall() short of what was fed;
    /// SoundTouch's own count is not used, since it goes wrong across tempo steps. Returns
    /// the frames kept; anything past `max_frames` is dropped with a warning.
    std::size_t FlushInto(s16* out, std::size_t max_frames);
    /// How much a flush can fall short of what is inside: one seek window, which the last
    /// round starts anywhere within, plus one overlap, which it blends into the padding.
    std::size_t FlushShortfall() const;
    /// Copies the last kFedTailFrames frames fed, oldest first, into `out`: more than the
    /// flush falls short of, so a handover's join can find where the raw stream continues
    /// the flush exactly, on any material. Exactly the frames that entered the stretcher,
    /// never the flush's padding, so the frame after the last is the caller's next.
    /// Returns the frames copied, at most `max_frames`.
    std::size_t CopyFedTail(s16* out, std::size_t max_frames) const;
    static constexpr std::size_t kFedTailFrames = 2048;
    /// One overlap, and one seek window, at the current settings, in frames.
    std::size_t OverlapFrames() const;
    std::size_t SeekFrames() const;
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
    std::vector<float> put_scratch;  // the float build's input, converted; grows once
    std::vector<float> recv_scratch; // the float build's output, likewise
    FrameHistory fed;                // the last frames fed, for CopyFedTail()
};

} // namespace AudioCore
