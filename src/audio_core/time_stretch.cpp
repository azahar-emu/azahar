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

namespace {
constexpr double kBacklogCapSeconds = 1.0;
} // namespace

static_assert(std::is_same_v<soundtouch::SAMPLETYPE, float> ||
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
        // integer PCM uses -32768..32767, so scale on the way in. The scratch grows to the
        // largest put and stays, so the audio thread stops allocating once it has.
        if (put_scratch.size() < 2 * num_in) {
            put_scratch.resize(2 * num_in);
        }
        for (std::size_t i = 0; i < (2 * num_in); i++) {
            put_scratch[i] = static_cast<float>(in[i]) / std::numeric_limits<s16>::max();
        }
        sound_touch->putSamples(reinterpret_cast<const soundtouch::SAMPLETYPE*>(put_scratch.data()),
                                static_cast<u32>(num_in));
    } else {
        sound_touch->putSamples(reinterpret_cast<const soundtouch::SAMPLETYPE*>(in),
                                static_cast<u32>(num_in));
    }
    fed.Record(in, num_in);
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

    // Twice the target: the servo steers toward 50% of this.
    const double max_backlog = 2.0 * target_backlog_seconds * native_sample_rate;
    const double backlog_fullness = sound_touch->numSamples() / max_backlog;
    // The cap on what it will hold is a fixed second, what master's 400% of its 0.25 s came
    // to. Tied to the target it would fall with it, and a drain toward a small target would
    // then drop the input it was supposed to play.
    if (sound_touch->numSamples() > kBacklogCapSeconds * native_sample_rate) {
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
    fed.Clear();
}

void TimeStretcher::SetTargetBacklog(double seconds) {
    target_backlog_seconds = seconds;
}

void TimeStretcher::SetRatioBounds(double lo, double hi) {
    min_ratio = lo;
    max_ratio = hi;
    stretch_ratio = std::clamp(stretch_ratio, min_ratio, max_ratio);
}

void TimeStretcher::SetRatio(double ratio) {
    stretch_ratio = std::clamp(ratio, min_ratio, max_ratio);
    sound_touch->setTempo(stretch_ratio);
}

std::size_t TimeStretcher::OutputBatchFrames() const {
    return static_cast<std::size_t>(
        std::max(0, sound_touch->getSetting(SETTING_NOMINAL_OUTPUT_SEQUENCE)));
}

std::size_t TimeStretcher::BeginPrime() {
    stretch_ratio = 1.0;
    sound_touch->setTempo(1.0);
    fed.Clear();
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
    // SoundTouch's own flush() trims to what it expected when each frame was fed, at the
    // tempo of that moment; after a tempo step with input resident that expectation can be
    // short by the whole residency, which it then discards, some 50 ms after a fast-forward
    // release. So: pad with silence, read past where the audio can end, and trim back to it.
    // The estimate of what is inside is the backlog plus the residency at the current tempo;
    // the last round lands within a seek window of that, and blends its tail with the
    // silence over one overlap. The read goes that far into the padding, and the trailing
    // silence is trimmed along with the blended overlap, so the flush ends on clean audio.
    const std::size_t residency = sound_touch->numUnprocessedSamples();
    const std::size_t overlap = OverlapFrames();
    const std::size_t seek = SeekFrames();
    const std::size_t expected =
        sound_touch->numSamples() +
        static_cast<std::size_t>(static_cast<double>(residency) / stretch_ratio);
    const std::size_t want = std::min(expected + seek + overlap, max_frames);

    // Bounded, for a stretcher that cannot make another round however much it is fed. Put
    // directly: the padding is not audio and must not be recorded as fed.
    static constexpr std::size_t kPadFrames = 128;
    static constexpr std::array<soundtouch::SAMPLETYPE, kPadFrames * 2> zeros{};
    for (std::size_t padded = 0; sound_touch->numSamples() < want && padded < (2 * want) + 8192;
         padded += kPadFrames) {
        sound_touch->putSamples(zeros.data(), static_cast<u32>(kPadFrames));
    }

    std::size_t got = 0;
    while (got < want) {
        const std::size_t n = Receive(out + (got * 2), want - got);
        if (n == 0) {
            break;
        }
        got += n;
    }
    if (expected > max_frames) {
        LOG_WARNING(Audio, "Stretcher flush overran its stash by {} frames; dropped",
                    expected - max_frames);
    }
    sound_touch->clear();

    // Trim the silence the padding made, then the overlap blended into it. Only zeros the
    // padding could have made: the residency counts the last round's offset, which the
    // round before already played, so the audio ends at or before `expected`, and the last
    // round starts anywhere within a seek window of its nominal place, so it cannot end
    // before `expected` less one; the overlap on top is slack (measured: never below
    // `expected` less 294). A run of zeros reaching that floor is the audio's own silence,
    // kept in full: a game silent on a load screen buffers time in that silence, and a
    // flush that dropped it would hand Bypass an empty stash, which re-engages on the next
    // callback.
    const std::size_t floor = expected > seek + overlap ? expected - seek - overlap : 0;
    std::size_t end = got;
    while (end > floor && out[(end - 1) * 2] == 0 && out[((end - 1) * 2) + 1] == 0) {
        end--;
    }
    if (end < got && end > floor) {
        end = end > overlap ? end - overlap : 0;
    }
    return end;
}

std::size_t TimeStretcher::CopyFedTail(s16* out, std::size_t max_frames) const {
    return fed.Last(std::min(max_frames, kFedTailFrames), out);
}

std::size_t TimeStretcher::OverlapFrames() const {
    return static_cast<std::size_t>(sound_touch->getSetting(SETTING_OVERLAP_MS)) *
           static_cast<std::size_t>(native_sample_rate) / 1000;
}

std::size_t TimeStretcher::FlushShortfall() const {
    return SeekFrames() + OverlapFrames();
}

std::size_t TimeStretcher::SeekFrames() const {
    // The setting reads 0 while SoundTouch picks the window itself, 15 to 20 ms by tempo
    // (TDStretch::calcSeqParameters(), externals/soundtouch/source/SoundTouch/TDStretch.cpp);
    // the widest covers every tempo.
    static constexpr int kAutoSeekMaxMs = 20;
    int ms = sound_touch->getSetting(SETTING_SEEKWINDOW_MS);
    if (ms <= 0) {
        ms = kAutoSeekMaxMs;
    }
    return static_cast<std::size_t>(ms) * static_cast<std::size_t>(native_sample_rate) / 1000;
}

std::size_t TimeStretcher::OutputBacklog() const {
    return sound_touch->numSamples();
}

std::size_t TimeStretcher::InputResidency() const {
    return sound_touch->numUnprocessedSamples();
}

} // namespace AudioCore
