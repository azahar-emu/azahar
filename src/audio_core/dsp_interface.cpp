// Copyright 2017-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <chrono>
#include <cstddef>
#include <cstring>
#include "audio_core/dsp_interface.h"
#include "audio_core/sink.h"
#include "audio_core/sink_details.h"
#include "common/assert.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/dumping/backend.h"

namespace AudioCore {

namespace {

/// How long JumpBegin() gives the tail to reach the device before going ahead without it.
/// Comfortably over StreamRamp's tail and the mute behind it, so it is a backstop against a
/// sink that never calls back rather than a budget the ramp is expected to fit in.
constexpr auto kJumpSettleTimeout = std::chrono::milliseconds(50);

} // namespace

DspInterface::DspInterface(Core::System& system_) : system(system_) {}

DspInterface::~DspInterface() = default;

void DspInterface::SetSink(AudioCore::SinkType sink_type, std::string_view audio_device) {
    // Dispose of the current sink first to avoid contention.
    sink.reset();

    sink = AudioCore::GetSinkDetails(sink_type).create_sink(audio_device);
    // A new sink is a new stream: nothing of the old one to continue, and it opens on a ramp.
    ramp = StreamRamp{};
    stream_settled.store(true, std::memory_order_release);
    sink->SetCallback(
        [this](s16* buffer, std::size_t num_frames) { OutputCallback(buffer, num_frames); });
    time_stretcher.SetOutputSampleRate(sink->GetNativeSampleRate());
}

Sink& DspInterface::GetSink() {
    ASSERT(sink);
    return *sink.get();
}

void DspInterface::EnableStretching(bool enable) {
    enable_time_stretching = enable;
}

void DspInterface::SetAudioRamp(bool enable) {
    enable_audio_ramp = enable;
}

void DspInterface::StreamEnd() {
    core_silenced.store(true, std::memory_order_release);
}

void DspInterface::StreamBegin() {
    core_silenced.store(false, std::memory_order_release);
}

bool DspInterface::JumpBegin() {
    if (core_silenced.exchange(true, std::memory_order_acq_rel)) {
        return false;
    }
    // The tail is the audio thread's to play. A load replaces this object and its sink, and a
    // reset closes the sink outright, so unless the tail has reached the device by then it is
    // cut off like the audio it was to replace: wait for a callback to report the stream
    // settled, down with its tail out. A source that had already stopped, its tail long gone,
    // is settled as it stands, and there is nothing to wait for. Deadline-bounded, since a sink
    // that never calls back (null, or the libretro sink's immediate submission) settles nothing;
    // a wakeup lost between the predicate and the wait costs the deadline, not the tail.
    const auto deadline = std::chrono::steady_clock::now() + kJumpSettleTimeout;
    std::unique_lock lock{settled_mutex};
    settled_cv.wait_until(lock, deadline,
                          [this] { return stream_settled.load(std::memory_order_acquire); });
    return true;
}

void DspInterface::JumpEnd(bool ramped) {
    if (ramped) {
        StreamBegin();
    }
}

void DspInterface::DiscardPending() {
    // Produced before the stream was taken down; behind the tail it would only splice in.
    while (fifo.Pop(pop_scratch.data(), kPopChunkFrames) > 0) {
    }
}

void DspInterface::OutputFrame(StereoFrame16 frame) {
    if (!sink) {
        return;
    }

    if (sink->ImmediateSubmission()) {
        sink->PushSamples(frame.data(), frame.size());
    } else {
        fifo.Push(frame.data(), frame.size());
    }

    auto video_dumper = system.GetVideoDumper();
    if (video_dumper && video_dumper->IsDumping()) {
        video_dumper->AddAudioFrame(std::move(frame));
    }
}

void DspInterface::OutputSample(std::array<s16, 2> sample) {
    if (!sink) {
        return;
    }

    if (sink->ImmediateSubmission()) {
        sink->PushSamples(&sample, 1);
    } else {
        fifo.Push(&sample, 1);
    }

    auto video_dumper = system.GetVideoDumper();
    if (video_dumper && video_dumper->IsDumping()) {
        video_dumper->AddAudioSample(std::move(sample));
    }
}

void DspInterface::OutputCallback(s16* buffer, std::size_t num_frames) {
    // Determine if we should stretch based on the current emulation speed.
    // TODO: Only activate audio stretching when emulation speed goes below 95% threshold
    //       (see #2487) -OS
    if (performing_time_stretching && !enable_time_stretching) {
        // If we just stopped stretching, flush the stretcher before returning to normal output.
        flushing_time_stretcher = true;
    }
    performing_time_stretching = enable_time_stretching.load();

    // Taken down on purpose: nothing is popped, so the stretcher's bookkeeping stands still
    // and picks up where it left off, and the ramp below fills the buffer with the tail.
    const bool silenced = core_silenced.load(std::memory_order_acquire);

    std::size_t frames_written = 0;
    if (silenced) {
        DiscardPending();
    } else if (performing_time_stretching) {
        // Not a bare Pop(): that value-inits a vector to the FIFO's whole capacity every
        // callback. Sized to Size() instead, so a racing push just waits for the next one.
        const std::vector<s16> in{fifo.Pop(fifo.Size())};
        const std::size_t num_in{in.size() / 2};
        frames_written = time_stretcher.Process(in.data(), num_in, buffer, num_frames);
    } else {
        if (flushing_time_stretcher) {
            time_stretcher.Flush();
            frames_written = time_stretcher.Process(nullptr, 0, buffer, num_frames);
            flushing_time_stretcher = false;

            // Make sure any frames that did not fit are cleared from the time stretcher,
            // so that they do not bleed into the next time the stretcher is enabled.
            time_stretcher.Clear();
        }
        frames_written += fifo.Pop(buffer, num_frames - frames_written);
    }

    // What the source did not fill is the ramp's to fill, below.
    if (frames_written < num_frames) {
        std::memset(buffer + (frames_written * 2), 0,
                    (num_frames - frames_written) * 2 * sizeof(s16));
    }

    // Last before the volume, so the history it keeps is what was played and the tail it
    // synthesizes from that history is scaled like everything else. Where the source stopped
    // short, deliberately or not, the tail takes over from the frame it stopped on; when it
    // returns, the first frames ramp in.
    const bool ramp_enabled = enable_audio_ramp.load();
    if (ramp_was_enabled && !ramp_enabled) {
        // Turned off mid-stream, possibly mid-tail. Drop that state rather than freeze it: a
        // tail left pending would hold stream_settled false for good, and every JumpBegin()
        // after it would wait out its whole deadline for a tail that will never play.
        ramp = StreamRamp{};
    }
    ramp_was_enabled = ramp_enabled;
    if (ramp_enabled) {
        ramp.Process(buffer, num_frames, frames_written);
    }

    // Signaled on the rising edge alone: a notify every callback would put a futex wake on the
    // audio thread once a buffer, where this fires only at a takedown, which is rare and is the
    // only time anyone is waiting. Never takes settled_mutex, so the callback cannot be made to
    // wait on the thread that is waiting on it.
    const bool settled = ramp.Down() && !ramp.InTail();
    if (settled) {
        if (!stream_settled.exchange(true, std::memory_order_acq_rel)) {
            settled_cv.notify_all();
        }
    } else {
        stream_settled.store(false, std::memory_order_release);
    }

    // Implementation of the hardware volume slider
    // A cubic curve is used to approximate a linear change in human-perceived loudness
    const float linear_volume = std::clamp(Settings::Volume(), 0.0f, 1.0f);
    if (linear_volume != 1.0) {
        const float volume_scale_factor = linear_volume * linear_volume * linear_volume;
        for (std::size_t i = 0; i < num_frames; i++) {
            buffer[i * 2 + 0] = static_cast<s16>(buffer[i * 2 + 0] * volume_scale_factor);
            buffer[i * 2 + 1] = static_cast<s16>(buffer[i * 2 + 1] * volume_scale_factor);
        }
    }
}

} // namespace AudioCore
