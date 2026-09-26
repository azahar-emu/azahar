// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
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
    /// How far into the second stream JoinFlush() looks for the continuation, in frames.
    static constexpr unsigned kJoinSearch = 2048;
    /// The fraction of the reference's energy a join's best mismatch may reach; see JoinFlush().
    static constexpr float kJoinAcceptFraction = 0.5f;
    /// Under this fraction of the reference's energy, the best period's mismatch says the
    /// material is periodic, and only a whole period of it is cut; see Cut(). On a tone the
    /// mismatch is 2 - 2 cos(phase error), so 0.2 admits about 26 degrees.
    static constexpr float kPeriodicFraction = 0.2f;

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
                // A whole period of the material, or nothing: with the budget under the
                // material's period, the best fit within it is a phase step. Ask the budget
                // first, since a period that fits and is itself periodic is what to cut
                // (the whole range would name the best-fitting multiple, near the top, and
                // wait on a budget that never reaches it). When nothing periodic fits, look
                // wider: a period out there means periodic material, so decline and let the
                // credit wait; none means noise, and the best fit within the budget will do.
                float score = 0.0f;
                float energy = 0.0f;
                p = Find(a, avail, max_p, &score, &energy);
                if (p != 0 && score > kPeriodicFraction * energy) {
                    const std::size_t full_max = std::min<std::size_t>(
                        static_cast<std::size_t>(kMaxPeriod), avail - num_frames);
                    float wide = 0.0f;
                    float wide_energy = 0.0f;
                    if (full_max > max_p && Find(a, avail, full_max, &wide, &wide_energy) != 0 &&
                        wide <= kPeriodicFraction * wide_energy) {
                        p = 0;
                    }
                } else if (p == 0) {
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
        std::size_t p = Find(a, std::min(avail, num_frames), max_p);
        if (p == 0) {
            // Silence has no period to repeat, but repeating it is free of artifacts, and a
            // raw path that never falls behind in silence is never overtaken: insert as much
            // of the silent lead as fits, so a warm-up in silence still syncs.
            const std::size_t silent = LeadingSilence(a, std::min(avail, max_p));
            if (silent >= kMinPeriod && avail >= num_frames - silent) {
                std::memset(out, 0, silent * 2 * sizeof(s16));
                std::memcpy(out + (silent * 2), a, (num_frames - silent) * 2 * sizeof(s16));
                stash.Consume(num_frames - silent);
                return {num_frames, num_frames - silent, silent};
            }
        }
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

    /// The stash holds `boundary` frames of one stream and then a second that overlaps its
    /// end: a stretcher's flush falls short of what it was fed, and the second stream starts
    /// with the last frames fed, so the flush's last kCorrFrames recur in it, a copy on any
    /// material within a seek window and an overlap of its start. Finds where they recur,
    /// scored against every point within kJoinSearch and the earliest best taken (on periodic
    /// material a period earlier scores the same, which repeats one period, unheard),
    /// cross-fades the first's last kJoinFrames into the frames before that point, and drops
    /// what lies between, so the second stream continues the first exactly. At least
    /// kCorrFrames are dropped, since the match is scored on the frames before the candidate,
    /// which must lie in the second stream. Returns the frames dropped; 0, with the stash
    /// untouched, when either side is too short to search, when the first stream ends in
    /// silence (scored on the channel sum, so antiphase stereo counts) and any junction will
    /// do, or when nothing within reach continues it, as when the first stream's end is a
    /// WSOLA blend of two segments that the second holds only one of; the caller then steps
    /// the boundary back past the blend, or fades across the seam. `reach` bounds the search
    /// to the frames past the boundary that can hold the copy.
    std::size_t JoinFlush(FrameStash& stash, std::size_t boundary,
                          std::size_t reach = std::numeric_limits<std::size_t>::max()) {
        const std::size_t avail = stash.Size();
        if (boundary < kJoinFrames || avail < boundary + kCorrFrames + kMinPeriod) {
            return 0;
        }
        s16* a = stash.MutableData();
        const auto sum_at = [a](std::size_t i) {
            return static_cast<float>(a[i * 2]) + static_cast<float>(a[(i * 2) + 1]);
        };
        float e_ref = 0.0f;
        for (std::size_t k = 0; k < kCorrFrames; k++) {
            const float v = sum_at(boundary - kCorrFrames + k);
            e_ref += v * v;
        }
        if (e_ref <= 0.0f) {
            return 0;
        }
        // The candidate is where the second stream resumes; the kCorrFrames before it are
        // what it is scored on, and they must lie inside the second stream.
        // `reach` is how far past the boundary the copy can lie, the fed tail's length: a
        // candidate beyond it is in frames the first stream never played, where a chance
        // match on periodic material would erase real audio.
        const std::size_t first = boundary + kCorrFrames;
        const std::size_t reach_end = reach > avail - boundary ? avail : boundary + reach;
        const std::size_t last = std::min<std::size_t>({first + kJoinSearch, reach_end});
        if (last < first) {
            return 0;
        }
        std::size_t best = first;
        float best_diff = -1.0f;
        for (std::size_t p = first; p <= last; p++) {
            float diff = 0.0f;
            for (std::size_t k = 0; k < kCorrFrames; k++) {
                const float d = sum_at(boundary - kCorrFrames + k) - sum_at(p - kCorrFrames + k);
                diff += d * d;
            }
            if (best_diff < 0.0f || diff < best_diff) {
                best_diff = diff;
                best = p;
            }
        }
        // A continuation of the same material scores near zero against the reference's
        // energy; unrelated material scores around twice it, and a rest exactly it.
        if (best_diff > kJoinAcceptFraction * e_ref) {
            return 0;
        }
        for (std::size_t i = 0; i < kJoinFrames; i++) {
            const float w = Ramp(i, kJoinFrames);
            const std::size_t into = boundary - kJoinFrames + i;
            const std::size_t from = best - kJoinFrames + i;
            a[into * 2] = Blend(a[into * 2], a[from * 2], w);
            a[(into * 2) + 1] = Blend(a[(into * 2) + 1], a[(from * 2) + 1], w);
        }
        stash.Erase(boundary, best - boundary);
        return best - boundary;
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
    static std::size_t Find(const s16* a, std::size_t avail, std::size_t max_p,
                            float* score = nullptr, float* energy = nullptr) {
        return FindPeriod(
            [a](unsigned k) {
                return static_cast<float>(a[k * 2]) + static_cast<float>(a[(k * 2) + 1]);
            },
            static_cast<unsigned>(avail), kMinPeriod, static_cast<unsigned>(max_p), kCorrFrames,
            score, energy);
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
