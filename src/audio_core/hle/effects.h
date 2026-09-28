// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include <vector>
#include <boost/serialization/array.hpp>
#include <boost/serialization/vector.hpp>
#include "audio_core/audio_types.h"
#include "audio_core/hle/shared_memory.h"
#include "common/common_types.h"

namespace AudioCore::HLE {

class DelayEffect final {
public:
    void Reset();

    void ParseConfig(DspConfiguration::DelayEffect& config, std::size_t index);

    void ProcessFrame(QuadFrame32& frame);

    bool IsEnabled() const {
        return enabled;
    }

private:
    // Set a max delay to prevent garbage data allocating too much memory.
    // 4 seconds seems like a reasonable amount.
    // TODO: Check if real HW limits this in another way.
    static constexpr u32 max_delay_samples = 4 * native_sample_rate;

    void Resize(u32 samples);

    bool enabled = false;
    u32 delay_samples = 0;

    // Coefficients, converted from the s16 Q7 values in shared memory.
    float g = 0.0f;
    float a = 0.0f;
    float b = 0.0f;

    std::array<std::vector<float>, 4> delay_output{};
    std::array<float, 4> last_output{};
    std::size_t delay_pos = 0;

    template <class Archive>
    void serialize(Archive& ar, const unsigned int) {
        ar & enabled;
        ar & delay_samples;
        ar & g;
        ar & a;
        ar & b;
        ar & delay_output;
        ar & last_output;
        ar & delay_pos;
    }
    friend class boost::serialization::access;
};

// TODO: Figure out the reverb effect.
class ReverbEffect final {
public:
    void Reset();

    void ParseConfig(const DspConfiguration::ReverbEffect& config, std::size_t index);

    void ProcessFrame(QuadFrame32& frame);

private:
    template <class Archive>
    void serialize(Archive&, const unsigned int) {}
    friend class boost::serialization::access;
};

} // namespace AudioCore::HLE
