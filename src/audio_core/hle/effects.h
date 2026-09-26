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

/**
 * Reverb effect.
 *
 * Signal chain:
 *
 *      x --> pre delay --> comb 0 + comb 1 -----> all-pass --> late --> * fused_g --+--> y
 *        |                                                                          |
 *        \ --> early delay ------------------> early --> * early_g ---------------- /
 *
 * Each comb has a single-pole filter (coeficients a and b) in its feedback arm.
 */
class ReverbEffect final {
public:
    void Reset();

    void ParseConfig(DspConfiguration::ReverbEffect& config, std::size_t index);

    void ProcessFrame(QuadFrame32& frame);

    bool IsEnabled() const {
        return enabled;
    }

private:
    // Set a max delay to prevent garbage data allocating too much memory, as in DelayEffect.
    static constexpr u32 max_delay_samples = 4 * native_sample_rate;

    // A quadraphonic delay line. All four channels share a length and a read/write position.
    struct DelayLine {
        std::array<std::vector<float>, 4> buffer{};
        u32 length = 0;
        std::size_t pos = 0;

        void Resize(u32 samples);
        void Clear();

        bool Empty() const {
            return length == 0;
        }
        float Peek(std::size_t channel) const {
            return buffer[channel][pos];
        }
        void Poke(std::size_t channel, float value) {
            buffer[channel][pos] = value;
        }
        // Advances by one sample.
        void Advance() {
            pos = (pos + 1) % length;
        }

        template <class Archive>
        void serialize(Archive& ar, const unsigned int) {
            ar & buffer;
            ar & length;
            ar & pos;
        }
        friend class boost::serialization::access;
    };

    // Converts a frame count into a clamped sample count, returns 0 for a bypassed stage.
    u32 FrameCountToSamples(s16 frame_count, std::size_t index, const char* name) const;

    bool enabled = false;

    DelayLine pre_delay{};
    DelayLine early_delay{};
    std::array<DelayLine, 2> comb{};
    DelayLine all_pass{};

    // Single-pole filter state, per comb, per channel.
    std::array<std::array<float, 4>, 2> comb_filter_state{};

    float early_g = 0.0f;
    float fused_g = 0.0f;
    float all_pass_coef = 0.0f;
    std::array<float, 2> comb_coef{};
    float a = 0.0f;
    float b = 0.0f;

    template <class Archive>
    void serialize(Archive& ar, const unsigned int) {
        ar & enabled;
        ar & pre_delay;
        ar & early_delay;
        ar & comb;
        ar & all_pass;
        ar & comb_filter_state;
        ar & early_g;
        ar & fused_g;
        ar & all_pass_coef;
        ar & comb_coef;
        ar & a;
        ar & b;
    }
    friend class boost::serialization::access;
};

} // namespace AudioCore::HLE
