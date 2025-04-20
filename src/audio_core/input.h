// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <vector>
#include "common/common_types.h"

namespace AudioCore {

enum class Signedness : u8 {
    Signed,
    Unsigned,
};

using Samples = std::vector<u8>;

struct InputParameters {
    Signedness sign;
    u8 sample_size;
    bool buffer_loop;
    u32 sample_rate;
    u32 buffer_offset;
    u32 buffer_size;
};

class Input {
public:
    Input() = default;

    virtual ~Input() = default;

    /// Starts the microphone. Called by Core
    virtual void StartSampling(const InputParameters& params) = 0;

    /// Stops the microphone. Called by Core
    virtual void StopSampling() = 0;

    /// Checks whether the microphone is currently sampling.
    virtual bool IsSampling() = 0;

    /**
     * Adjusts the Parameters. Implementations should update the parameters field in addition to
     * changing the mic to sample according to the new parameters. Called by Core
     */
    virtual void AdjustSampleRate(u32 sample_rate) = 0;

    /**
     * Called from the actual event timing at a constant period under a given sample rate.
     * When sampling is enabled this function is expected to return a buffer of 16 samples in ideal
     * conditions, but can be lax if the data is coming in from another source like a real mic.
     */
    virtual Samples Read() = 0;

    /**
     * Generates a buffer of silence.
     * Takes into account the sample size and signedness of the input.
     */
    virtual Samples GenerateSilentSamples(const InputParameters& params) {
        u8 silent_value = 0x00;

        if (params.sample_size == 8) {
            silent_value = params.sign == Signedness::Unsigned ? 0x80 : 0x00;
            return std::vector<u8>(32, silent_value);
        } else {
            silent_value = params.sign == Signedness::Unsigned ? 0x80 : 0x00;
            return std::vector<u8>(64, silent_value);
        }
    }

protected:
    InputParameters parameters;
};

} // namespace AudioCore
