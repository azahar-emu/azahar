// Copyright 2017-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cstddef>
#include <limits>
#include "audio_core/dsp_interface.h"
#include "audio_core/sink.h"
#include "audio_core/sink_details.h"
#include "common/assert.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/dumping/backend.h"

namespace AudioCore {

DspInterface::DspInterface(Core::System& system_) : system(system_) {}

DspInterface::~DspInterface() = default;

void DspInterface::SetSink(AudioCore::SinkType sink_type, std::string_view audio_device) {
    // Dispose of the current sink first to avoid contention.
    sink.reset();

    sink = AudioCore::GetSinkDetails(sink_type).create_sink(audio_device);
    // Before SetCallback(): the SDL2 sink may call back immediately, already unpaused.
    pipeline.SetOutputSampleRate(sink->GetNativeSampleRate());
    sink->SetCallback(
        [this](s16* buffer, std::size_t num_frames) { OutputCallback(buffer, num_frames); });
}

Sink& DspInterface::GetSink() {
    ASSERT(sink);
    return *sink.get();
}

void DspInterface::EnableStretching(bool enable) {
    pipeline.SetStretching(enable);
}

void DspInterface::SetAudioRamp(bool enable) {
    pipeline.SetRamp(enable);
}

void DspInterface::StreamEnd() {
    pipeline.StreamEnd();
}

void DspInterface::StreamBegin() {
    pipeline.StreamBegin();
}

bool DspInterface::JumpBegin() {
    return pipeline.JumpBegin();
}

void DspInterface::JumpEnd(bool ramped) {
    pipeline.JumpEnd(ramped);
}

void DspInterface::OutputFrame(StereoFrame16 frame) {
    if (!sink) {
        return;
    }

    if (sink->ImmediateSubmission()) {
        sink->PushSamples(frame.data(), frame.size());
    } else {
        pipeline.Push(frame.data(), frame.size());
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
        pipeline.Push(&sample, 1);
    }

    auto video_dumper = system.GetVideoDumper();
    if (video_dumper && video_dumper->IsDumping()) {
        video_dumper->AddAudioSample(std::move(sample));
    }
}

void DspInterface::OutputCallback(s16* buffer, std::size_t num_frames) {
    // A hint for the stretcher's servo, not a decision: the pipeline measures what arrives.
    const double frame_limit = Settings::GetFrameLimit();
    pipeline.SetRequestedSpeed(frame_limit <= 0.0 ? std::numeric_limits<double>::infinity()
                                                  : frame_limit / 100.0);
    pipeline.Render(buffer, num_frames);

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
