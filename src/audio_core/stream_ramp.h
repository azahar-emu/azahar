// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <numbers>
#include "audio_core/period_finder.h"
#include "common/common_types.h"

namespace AudioCore {

/// Ends an output stream on a ramp and brings it back on one, so a pause, a state load or an
/// underrun lands in silence instead of cutting the waveform where it stands. The tail repeats
/// the best-matching recent period under a raised cosine, as a packet-loss concealer does:
/// fading the last sample as a constant is a decaying DC offset heard as a thump, whereas
/// repeating one period keeps the spectrum of what was playing and only takes its level away.
/// Owned by the audio thread; not thread-safe.
class StreamRamp {
public:
    // Frames at the output rate. The history must hold the longest period the tail may pick
    // plus the window it is matched over.
    static constexpr unsigned kHistoryFrames = 2048;
    static constexpr unsigned kTailFrames = 512;
    static constexpr unsigned kMinPeriod = 96;
    static constexpr unsigned kMaxPeriod = 1024;
    static constexpr unsigned kCorrFrames = 128;
    static constexpr unsigned kJoinFrames = 128;
    // Source frames dropped after a tail, to swallow whatever was buffered behind it.
    static constexpr unsigned kMuteFrames = 512;
    static constexpr unsigned kRampInFrames = 512;

    /// True from End() until the tail and the mute that follows it are spent.
    bool Ended() const {
        return tail_frames > 0 || mute_frames > 0;
    }
    bool InTail() const {
        return tail_frames > 0;
    }
    /// True while no source frames are flowing. A fresh stream starts down, so its first
    /// frames ramp in rather than step from the silence the device was playing.
    bool Down() const {
        return down;
    }

    /// Takes the stream down: arms a tail continuing what was last played. Nothing to do if
    /// that was silence.
    void End() {
        if (last_out[0] == 0.0f && last_out[1] == 0.0f) {
            return;
        }
        // The tail reads the history as it stands now; what it writes goes into the live one.
        tail_src = hist;
        tail_src_pos = hist_pos;
        tail_src_fill = hist_fill;
        tail_period = FindPeriod();
        tail_from = last_out;
        if (tail_period != 0) {
            // Age period+1, not period: the frame matching the last one played is a whole
            // period before it, and age period is already its successor, the frame the tail
            // opens on. However good the match, the repeat starts at its own value rather
            // than the one the stream stopped on; the difference is corrected away below.
            const float* f = TailAt(tail_period + 1);
            tail_join = {last_out[0] - f[0], last_out[1] - f[1]};
        } else {
            tail_join = {0.0f, 0.0f};
        }
        tail_frames = kTailFrames;
        fade_in_frames = 0;
    }

    /// Fills frames in place of source audio: the tail, then silence once it is spent.
    void FillTail(s16* frames, std::size_t num_frames) {
        for (std::size_t i = 0; i < num_frames; i++) {
            float l = 0.0f;
            float r = 0.0f;
            if (tail_frames > 0) {
                TailFrame(l, r);
            }
            frames[(i * 2) + 0] = ToSample(l);
            frames[(i * 2) + 1] = ToSample(r);
        }
        Record(frames, num_frames);
    }

    /// Brings the stream back: the next source frames ramp in. If a tail is still playing the
    /// ramp waits until it and the mute behind it are spent.
    void Begin() {
        if (Ended()) {
            ramp_in_latched = true;
            return;
        }
        fade_in_frames = kRampInFrames;
    }

    /// Source frames about to be played: applies the mute and the ramp in, and remembers the
    /// tail of the buffer for a later End() to continue from.
    void Track(s16* frames, std::size_t num_frames) {
        std::size_t head = 0;
        // A tail the last buffer could not finish takes the head of this one. The source
        // frames under it arrived during the tail and fall in the mute behind it either way;
        // left pending instead, the tail would play on at the next End() as a stale fragment.
        if (tail_frames > 0) {
            head = std::min<std::size_t>(tail_frames, num_frames);
            for (std::size_t j = 0; j < head; j++) {
                float l = 0.0f;
                float r = 0.0f;
                TailFrame(l, r);
                frames[(j * 2) + 0] = ToSample(l);
                frames[(j * 2) + 1] = ToSample(r);
            }
        }
        if (mute_frames > 0 && head < num_frames) {
            const std::size_t n = std::min<std::size_t>(mute_frames, num_frames - head);
            std::memset(frames + (head * 2), 0, n * 2 * sizeof(s16));
            mute_frames -= static_cast<unsigned>(n);
            head += n;
        }
        if (ramp_in_latched && !Ended()) {
            ramp_in_latched = false;
            fade_in_frames = kRampInFrames;
        }
        if (fade_in_frames > 0 && head < num_frames) {
            const std::size_t n = std::min<std::size_t>(fade_in_frames, num_frames - head);
            for (std::size_t j = 0; j < n; j++) {
                // Progress against the whole ramp, not what is left of it: the counter falls
                // as the ramp is spent, so measuring from it would restart the gain near zero
                // at the head of every buffer.
                const unsigned done = kRampInFrames - fade_in_frames + static_cast<unsigned>(j) + 1;
                const float g = 0.5f * (1.0f - std::cos(std::numbers::pi_v<float> *
                                                        static_cast<float>(done) / kRampInFrames));
                s16* frame = frames + ((head + j) * 2);
                frame[0] = ToSample(frame[0] * g / 32768.0f);
                frame[1] = ToSample(frame[1] * g / 32768.0f);
            }
            fade_in_frames -= static_cast<unsigned>(n);
        }
        Record(frames, num_frames);
    }

    /// One buffer about to be played, of which the first real_frames came from the source and
    /// the rest did not: the source is either down or ran dry. Takes the stream down and
    /// brings it back on the edges.
    void Process(s16* frames, std::size_t num_frames, std::size_t real_frames) {
        if (real_frames > 0) {
            if (down) {
                down = false;
                Begin();
            }
            Track(frames, real_frames);
        }
        if (real_frames < num_frames) {
            s16* rest = frames + (real_frames * 2);
            const std::size_t n = num_frames - real_frames;
            // A source that is producing, but short, while the ramp in is still climbing is
            // the first buffers after a load, where the emulator's ticks do not yet fill a
            // callback. Pad the gap and keep climbing, rather than end a stream that has
            // barely started and bring it back on a second ramp.
            if (real_frames > 0 && fade_in_frames > 0) {
                std::memset(rest, 0, n * 2 * sizeof(s16));
                Record(rest, n);
                return;
            }
            if (!down) {
                down = true;
                // A tail already pending runs on; starting another would replay it from
                // the top at every short buffer and never finish.
                if (!Ended()) {
                    End();
                }
            }
            FillTail(rest, n);
        }
    }

private:
    static s16 ToSample(float v) {
        return static_cast<s16>(std::clamp(std::lround(v * 32768.0f), -32768L, 32767L));
    }

    /// The next frame of the tail, advancing it; arms the mute once it is spent.
    void TailFrame(float& l, float& r) {
        const unsigned pos = kTailFrames - tail_frames;
        // Raised cosine: a linear ramp still corners at both ends.
        const float g = 0.5f * (1.0f + std::cos(std::numbers::pi_v<float> *
                                                static_cast<float>(pos + 1) / kTailFrames));
        if (tail_period != 0) {
            // Age period continues the waveform from where it stopped; wrapping back to it
            // repeats that cycle for as long as the envelope lasts. The join correction must
            // be spent by the time the repeat wraps, or it would recur every wrap as a
            // sawtooth at the period's own rate.
            const unsigned u = pos % tail_period;
            const float* f = TailAt(tail_period - u);
            const unsigned jn = std::min(kJoinFrames, tail_period - 1);
            const float jw = (u < jn) ? 1.0f - (static_cast<float>(u) / jn) : 0.0f;
            l = (f[0] + (tail_join[0] * jw)) * g;
            r = (f[1] + (tail_join[1] * jw)) * g;
        } else {
            l = tail_from[0] * g;
            r = tail_from[1] * g;
        }
        tail_frames--;
        if (tail_frames == 0) {
            mute_frames = kMuteFrames;
        }
    }

    /// The frame `age` frames back from the most recently played one in the tail's snapshot,
    /// age 1 being that frame itself.
    const float* TailAt(unsigned age) const {
        const unsigned idx = (tail_src_pos + kHistoryFrames - age) % kHistoryFrames;
        return &tail_src[idx * 2];
    }

    /// Period, in frames, whose copy of the last kCorrFrames frames matches them best; see
    /// AudioCore::FindPeriod() (audio_core/period_finder.h). Zero when there is too little
    /// history to search, leaving the DC ramp.
    unsigned FindPeriod() const {
        return AudioCore::FindPeriod(
            [this](unsigned k) {
                const float* f = TailAt(1 + k);
                return f[0] + f[1];
            },
            tail_src_fill, kMinPeriod, kMaxPeriod, kCorrFrames);
    }

    void Record(const s16* frames, std::size_t num_frames) {
        if (num_frames == 0) {
            return;
        }
        std::size_t first = 0;
        std::size_t n = num_frames;
        if (n > kHistoryFrames) {
            first = n - kHistoryFrames;
            n = kHistoryFrames;
        }
        for (std::size_t j = 0; j < n; j++) {
            hist[(hist_pos * 2) + 0] = frames[((first + j) * 2) + 0] / 32768.0f;
            hist[(hist_pos * 2) + 1] = frames[((first + j) * 2) + 1] / 32768.0f;
            hist_pos = (hist_pos + 1) % kHistoryFrames;
        }
        hist_fill = static_cast<unsigned>(std::min<std::size_t>(hist_fill + n, kHistoryFrames));
        last_out[0] = frames[((num_frames - 1) * 2) + 0] / 32768.0f;
        last_out[1] = frames[((num_frames - 1) * 2) + 1] / 32768.0f;
    }

    // Rolling copy of the most recent frames played, normalized. A copy of what has already
    // gone out, never a delay line, so it costs nothing on the live path.
    std::array<float, kHistoryFrames * 2> hist{};
    unsigned hist_pos = 0;
    unsigned hist_fill = 0;
    std::array<float, 2> last_out{};

    std::array<float, kHistoryFrames * 2> tail_src{};
    unsigned tail_src_pos = 0;
    unsigned tail_src_fill = 0;
    unsigned tail_frames = 0;
    unsigned tail_period = 0;
    std::array<float, 2> tail_from{};
    std::array<float, 2> tail_join{};

    unsigned mute_frames = 0;
    unsigned fade_in_frames = 0;
    bool ramp_in_latched = false;
    bool down = true;
};

} // namespace AudioCore
