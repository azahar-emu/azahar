// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <algorithm>

namespace AudioCore {

/// Period, in frames, whose copy of the first `corr_frames` frames best matches them: the p
/// in [min_period, max_period] minimizing the summed squared difference between frame k and
/// frame k + p over k < corr_frames. `sum_at(k)` is the channel sum of frame k, readable for
/// k < avail. Scored by distance rather than correlation: a normalized correlation is blind
/// to level, so the same phrase at half the volume would score a perfect match and splicing
/// it would step the waveform. No score threshold: content with no periodicity has no right
/// answer, and the closest stretch of it is still the least audible to repeat or remove.
/// Zero when there is too little to search, or the reference is silent.
///
/// StreamRamp::FindPeriod() (audio_core/stream_ramp.h) searches its float history backward
/// through this; PeriodSplicer (audio_core/period_splicer.h) searches a stash forward.
template <typename SumAt>
unsigned FindPeriod(SumAt sum_at, unsigned avail, unsigned min_period, unsigned max_period,
                    unsigned corr_frames) {
    if (avail < min_period + corr_frames) {
        return 0;
    }
    const unsigned max_p = std::min(max_period, avail - corr_frames);
    if (max_p < min_period) {
        return 0;
    }

    // Matched on the channel sum: the two share a fundamental, and scoring them together stops
    // a quiet channel's noise choosing the period for a loud one. The energy only decides
    // whether there is anything here worth matching.
    float e_ref = 0.0f;
    for (unsigned k = 0; k < corr_frames; k++) {
        const float v = sum_at(k);
        e_ref += v * v;
    }
    if (e_ref <= 0.0f) {
        return 0;
    }

    unsigned best = 0;
    float best_diff = -1.0f;
    for (unsigned p = min_period; p <= max_p; p++) {
        float diff = 0.0f;
        for (unsigned k = 0; k < corr_frames; k++) {
            const float d = sum_at(k) - sum_at(p + k);
            diff += d * d;
        }
        if (best_diff < 0.0f || diff < best_diff) {
            best_diff = diff;
            best = p;
        }
    }
    return best;
}

} // namespace AudioCore
