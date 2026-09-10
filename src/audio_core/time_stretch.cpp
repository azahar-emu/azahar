// Copyright 2016-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <type_traits>
#include <vector>
#include <SoundTouch.h>

#include "audio_core/audio_types.h"
#include "audio_core/time_stretch.h"
#include "common/logging/log.h"

namespace AudioCore {

static_assert(std::is_floating_point_v<soundtouch::SAMPLETYPE> ||
              std::is_same_v<soundtouch::SAMPLETYPE, s16>);

TimeStretcher::TimeStretcher() : sound_touch(std::make_unique<soundtouch::SoundTouch>()) {
    sound_touch->setChannels(2);
    sound_touch->setSampleRate(native_sample_rate);
    sound_touch->setPitch(1.0);
    sound_touch->setTempo(1.0);
    // SoundTouch runs its anti-alias FIR whenever the rate is at or above 1.0, including
    // exactly 1.0, where nothing is resampled. The rate here never changes, so the filter
    // only costs CPU and smears the output. Off, the body of each round is a verbatim copy
    // of the input, which the pipeline test relies on to locate frames.
    sound_touch->setSetting(SETTING_USE_AA_FILTER, 0);
}

TimeStretcher::~TimeStretcher() = default;

void TimeStretcher::SetOutputSampleRate(unsigned int sample_rate) {
    sound_touch->setSampleRate(sample_rate);
}

std::size_t TimeStretcher::Put(const s16* in, std::size_t num_in) {
    if (num_in == 0) {
        return 0;
    }
    if constexpr (std::is_floating_point_v<soundtouch::SAMPLETYPE>) {
        // The SoundTouch library on most systems expects float samples in -1..1; conventional
        // integer PCM uses -32768..32767, so scale on the way in.
        std::vector<soundtouch::SAMPLETYPE> float_in(2 * num_in);
        for (std::size_t i = 0; i < (2 * num_in); i++) {
            float_in[i] = static_cast<soundtouch::SAMPLETYPE>(static_cast<float>(in[i]) /
                                                              std::numeric_limits<s16>::max());
        }
        sound_touch->putSamples(float_in.data(), static_cast<u32>(num_in));
    } else {
        sound_touch->putSamples(reinterpret_cast<const soundtouch::SAMPLETYPE*>(in),
                                static_cast<u32>(num_in));
    }
    return num_in;
}

std::size_t TimeStretcher::Receive(s16* out, std::size_t num_out) {
    if (num_out == 0) {
        return 0;
    }
    if constexpr (std::is_floating_point_v<soundtouch::SAMPLETYPE>) {
        // Scratch rather than a local vector: Discard() and FlushInto() call this in a
        // loop, so a local would allocate several times inside one callback. Grows to the
        // largest receive and stays, as put_scratch does.
        if (recv_scratch.size() < 2 * num_out) {
            recv_scratch.resize(2 * num_out);
        }
        const std::size_t got = sound_touch->receiveSamples(
            reinterpret_cast<soundtouch::SAMPLETYPE*>(recv_scratch.data()),
            static_cast<u32>(num_out));
        for (std::size_t i = 0; i < (2 * got); i++) {
            out[i] = static_cast<s16>(recv_scratch[i] * std::numeric_limits<s16>::max());
        }
        return got;
    } else {
        return sound_touch->receiveSamples(reinterpret_cast<soundtouch::SAMPLETYPE*>(out),
                                           static_cast<u32>(num_out));
    }
}

std::size_t TimeStretcher::Process(const s16* in, std::size_t num_in, s16* out,
                                   std::size_t num_out) {
    if (num_out == 0) {
        // Nothing asked for: keep the input, and skip the servo, which divides by the request.
        Put(in, num_in);
        return 0;
    }
    const double time_delta = static_cast<double>(num_out) / native_sample_rate; // seconds
    double current_ratio = static_cast<double>(num_in) / static_cast<double>(num_out);

    // Twice the target: the servo steers toward 50% of this, and stops accepting input at 400%.
    const double max_backlog = 2.0 * target_backlog_seconds * native_sample_rate;
    const double backlog_fullness = sound_touch->numSamples() / max_backlog;
    if (backlog_fullness > 4.0) {
        // Too many samples in backlog: Don't push anymore on
        num_in = 0;
    }

    // We ideally want the backlog to be about 50% full.
    // This gives some headroom both ways to prevent underflow and overflow.
    // We tweak current_ratio to encourage this.
    constexpr double tweak_time_scale = 0.050; // seconds
    const double tweak_correction = (backlog_fullness - 0.5) * (time_delta / tweak_time_scale);
    current_ratio *= std::pow(1.0 + 2.0 * tweak_correction, tweak_correction < 0 ? 3.0 : 1.0);

    // This low-pass filter smoothes out variance in the calculated stretch ratio.
    // The time-scale determines how responsive this filter is.
    constexpr double lpf_time_scale = 0.712; // seconds
    const double lpf_gain = 1.0 - std::exp(-time_delta / lpf_time_scale);
    stretch_ratio += lpf_gain * (current_ratio - stretch_ratio);

    // Clamped on the state itself, so a bound lifted later continues from here.
    stretch_ratio = std::clamp(stretch_ratio, min_ratio, max_ratio);
    sound_touch->setTempo(stretch_ratio);

    LOG_TRACE(Audio, "{:5}/{:5} ratio:{:0.6f} backlog:{:0.6f}", num_in, num_out, stretch_ratio,
              backlog_fullness);

    Put(in, num_in);
    return Receive(out, num_out);
}

void TimeStretcher::Clear() {
    sound_touch->clear();
}

void TimeStretcher::Flush() {
    sound_touch->flush();
}

void TimeStretcher::SetTargetBacklog(double seconds) {
    target_backlog_seconds = seconds;
}

void TimeStretcher::SetRatioBounds(double lo, double hi) {
    min_ratio = lo;
    max_ratio = hi;
    stretch_ratio = std::clamp(stretch_ratio, min_ratio, max_ratio);
}

std::size_t TimeStretcher::BeginPrime() {
    stretch_ratio = 1.0;
    sound_touch->setTempo(1.0);
    // Sixteen over the latency: RateTransposer keeps one input frame back for its
    // interpolation, so exactly the latency leaves TDStretch one short of a round.
    return static_cast<std::size_t>(std::max(0, sound_touch->getSetting(SETTING_INITIAL_LATENCY))) +
           16;
}

void TimeStretcher::Feed(const s16* in, std::size_t num_in) {
    Put(in, num_in);
}

std::size_t TimeStretcher::Discard(std::size_t max_frames) {
    std::size_t dropped = 0;
    while (dropped < max_frames) {
        const std::size_t want = std::min(max_frames - dropped, kDiscardChunkFrames);
        const std::size_t got = Receive(discard_scratch.data(), want);
        if (got == 0) {
            break;
        }
        dropped += got;
    }
    return dropped;
}

std::size_t TimeStretcher::FlushInto(s16* out, std::size_t max_frames) {
    sound_touch->flush();
    std::size_t got = 0;
    while (got < max_frames) {
        const std::size_t n = Receive(out + (got * 2), max_frames - got);
        if (n == 0) {
            break;
        }
        got += n;
    }
    const std::size_t left = sound_touch->numSamples();
    if (left > 0) {
        LOG_WARNING(Audio, "Stretcher flush overran its stash by {} frames; dropped", left);
    }
    sound_touch->clear();
    return got;
}

std::size_t TimeStretcher::OutputBacklog() const {
    return sound_touch->numSamples();
}

std::size_t TimeStretcher::InputResidency() const {
    return sound_touch->numUnprocessedSamples();
}

} // namespace AudioCore
