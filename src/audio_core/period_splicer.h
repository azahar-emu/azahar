// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <numbers>
#include "audio_core/frame_buffers.h"
#include "audio_core/period_finder.h"
#include "common/common_types.h"

namespace AudioCore {

/// Removes or repeats one pitch period of the raw stream at a time, at the offset that best
/// matches the frames about to play, with a raised-cosine join. A whole period cut or
/// repeated from periodic audio leaves the waveform's phase where it was; on noise the join
/// is the least audible cut available; on silence any cut is exact. One period per call
/// bounds the rate: the bypass trims a latency excess over a few hundred milliseconds and
/// the warm-up slows the raw path to let the stretcher catch up. Stateless. Audio thread
/// only.
class PeriodSplicer {
public:
    static constexpr unsigned kMinPeriod = 96;
    static constexpr unsigned kMaxPeriod = 1024;
    static constexpr unsigned kCorrFrames = 128;
    static constexpr unsigned kJoinFrames = 128;

    struct Result {
        std::size_t written;  // frames written to out
        std::size_t consumed; // source frames taken from the stash
        std::size_t spliced;  // frames cut, or frames inserted
    };

    /// Fills `out` from the front of `stash`, removing up to `budget` frames, at most one
    /// period, at a matched offset. A silent lead-in is removed outright, only as far as it is
    /// silent and with no join: what follows starts at its own onset either way. A stash
    /// shorter than `num_frames` plus a period is copied as far as it goes.
    Result Cut(s16* out, std::size_t num_frames, FrameStash& stash, std::size_t budget) {
        const std::size_t avail = stash.Size();
        const s16* a = stash.Data();
        std::size_t p = 0;
        bool exact = false;
        if (budget >= kMinPeriod && avail > num_frames) {
            const std::size_t max_p = std::min<std::size_t>(
                {static_cast<std::size_t>(kMaxPeriod), budget, avail - num_frames});
            if (max_p >= kMinPeriod) {
                p = Find(a, avail, max_p);
                if (p == 0) {
                    // Nothing to match in silence, and nothing to hear: drop the silent run,
                    // but only that, so whatever follows keeps its onset.
                    const std::size_t silent = LeadingSilence(a, max_p);
                    if (silent >= kMinPeriod) {
                        p = silent;
                        exact = true;
                    }
                }
            }
        }
        const std::size_t n = std::min(num_frames, avail - p);
        if (p == 0) {
            std::memcpy(out, a, n * 2 * sizeof(s16));
        } else if (exact) {
            std::memcpy(out, a + (p * 2), n * 2 * sizeof(s16));
        } else {
            const std::size_t join = std::min<std::size_t>(kJoinFrames, n);
            for (std::size_t i = 0; i < join; i++) {
                const float w = Ramp(i, join);
                out[i * 2] = Blend(a[i * 2], a[(i + p) * 2], w);
                out[(i * 2) + 1] = Blend(a[(i * 2) + 1], a[((i + p) * 2) + 1], w);
            }
            std::memcpy(out + (join * 2), a + ((join + p) * 2), (n - join) * 2 * sizeof(s16));
        }
        stash.Consume(n + p);
        return {n, n + p, p};
    }

    /// Fills `out` from the front of `stash`, repeating one matched period so the raw path
    /// falls behind real time by that much. The repeat covers a stash short of `num_frames`
    /// by up to the period; shorter than that, or with no period to match, a plain copy.
    Result Insert(s16* out, std::size_t num_frames, FrameStash& stash) {
        const std::size_t avail = stash.Size();
        if (num_frames < kJoinFrames + kMinPeriod) {
            return Cut(out, num_frames, stash, 0);
        }
        const s16* a = stash.Data();
        // The search and the join both read frames p + kJoinFrames deep, which must exist;
        // the repeat then reads back from the front, which needs num_frames - p of them.
        const std::size_t max_p = std::min<std::size_t>(kMaxPeriod, num_frames - kJoinFrames);
        const std::size_t p = Find(a, std::min(avail, num_frames), max_p);
        if (p == 0 || avail < num_frames - p) {
            return Cut(out, num_frames, stash, 0);
        }
        std::memcpy(out, a, p * 2 * sizeof(s16));
        for (std::size_t i = 0; i < kJoinFrames; i++) {
            const float w = Ramp(i, kJoinFrames);
            out[(p + i) * 2] = Blend(a[(p + i) * 2], a[i * 2], w);
            out[((p + i) * 2) + 1] = Blend(a[((p + i) * 2) + 1], a[(i * 2) + 1], w);
        }
        std::memcpy(out + ((p + kJoinFrames) * 2), a + (kJoinFrames * 2),
                    (num_frames - p - kJoinFrames) * 2 * sizeof(s16));
        stash.Consume(num_frames - p);
        return {num_frames, num_frames - p, p};
    }

private:
    /// Raised cosine from 0 at i = 0 to 1 at i = n - 1: correlated material on both sides, so
    /// a constant-gain cross-fade is right, and a cosine leaves both ends with zero slope.
    static float Ramp(std::size_t i, std::size_t n) {
        return 0.5f * (1.0f - std::cos(std::numbers::pi_v<float> * static_cast<float>(i + 1) /
                                       static_cast<float>(n)));
    }
    static s16 Blend(s16 from, s16 to, float w) {
        return static_cast<s16>(
            std::lround((static_cast<float>(from) * (1.0f - w)) + (static_cast<float>(to) * w)));
    }
    static std::size_t Find(const s16* a, std::size_t avail, std::size_t max_p) {
        return FindPeriod(
            [a](unsigned k) {
                return static_cast<float>(a[k * 2]) + static_cast<float>(a[(k * 2) + 1]);
            },
            static_cast<unsigned>(avail), kMinPeriod, static_cast<unsigned>(max_p), kCorrFrames);
    }
    /// Frames of silence from the front, at most `limit`.
    static std::size_t LeadingSilence(const s16* a, std::size_t limit) {
        std::size_t k = 0;
        while (k < limit && a[k * 2] == 0 && a[(k * 2) + 1] == 0) {
            k++;
        }
        return k;
    }
};

} // namespace AudioCore
