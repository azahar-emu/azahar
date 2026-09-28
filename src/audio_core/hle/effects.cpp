// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cmath>
#include <string>
#include "audio_core/hle/effects.h"
#include "common/logging/log.h"
#include "common/string_util.h"

namespace AudioCore::HLE {

static s32 ClampToS32(float value) {
    // Clamp to valid S32 range.
    constexpr float min = -2147483648.0f;
    constexpr float max = 2147483520.0f;
    return static_cast<s32>(std::clamp(value, min, max));
}

void DelayEffect::Reset() {
    enabled = false;
    delay_samples = 0;
    g = a = b = 0.0f;
    for (auto& channel : delay_output) {
        channel.clear();
    }
    last_output.fill(0.0f);
    delay_pos = 0;
}

void DelayEffect::Resize(u32 samples) {
    if (samples == delay_samples && !delay_output[0].empty()) {
        return;
    }
    delay_samples = samples;
    delay_pos = 0;
    last_output.fill(0.0f);
    for (auto& channel : delay_output) {
        channel.assign(samples, 0.0f);
    }
}

void DelayEffect::ParseConfig(DspConfiguration::DelayEffect& config, std::size_t index) {
    if (!config.dirty_raw) {
        return;
    }

    const bool was_enabled = enabled;
    enabled = config.enable != 0;

    if (!enabled) {
        config.dirty_raw = 0;
        if (was_enabled) {
            LOG_DEBUG(Audio_DSP, "delay_effect[{}] disabled", index);
            Reset();
        }
        return;
    }

    u32 samples = static_cast<u32>(config.frame_count) * samples_per_frame;
    if (samples == 0) {
        // Delay of zero samples is invalid as it would make the DSP read the sample it is
        // about to write. Figure out what real HW does in this case.
        LOG_DEBUG(Audio_DSP, "delay_effect[{}] enabled with frame_count=0, bypassing", index);
        enabled = false;
        config.dirty_raw = 0;
        Reset();
        return;
    }
    if (samples > max_delay_samples) {
        LOG_WARNING(Audio_DSP, "delay_effect[{}] frame_count={} exceeds sane range, clamping",
                    index, static_cast<u32>(config.frame_count));
        samples = max_delay_samples;
    }

    // Convert s16 with 7 fractional bits to float.
    constexpr float q7_scale = 1.0f / 128.0f;

    g = static_cast<s16>(config.g) * q7_scale;
    a = static_cast<s16>(config.a) * q7_scale;
    b = static_cast<s16>(config.b) * q7_scale;

    Resize(samples);

    LOG_DEBUG(Audio_DSP,
              "delay_effect[{}] enabled work_buffer_address=0x{:08X} frames={} samples={} g={} "
              "a={} b={} outputs={:#x}",
              index, static_cast<u32>(config.work_buffer_address),
              static_cast<u32>(config.frame_count), samples, g, a, b,
              static_cast<u32>(config.outputs));

    // We do not need the work buffer provided by the application as it is kept track separately.
    // Figure out if it is needed to have it in memory in case some application tries to access it.

    config.dirty_raw = 0;
}

void DelayEffect::ProcessFrame(QuadFrame32& frame) {
    if (!enabled) {
        return;
    }

    for (std::size_t sample = 0; sample < samples_per_frame; sample++) {
        const std::size_t pos = (delay_pos + sample) % delay_samples;
        for (std::size_t channel = 0; channel < 4; channel++) {
            /*
             * Formula:
             *      H(z) = a z^-N / (1 - b z^-1 + a g z^-N),   N = frame_count * samples_per_frame
             *      \/
             *      Y(z) * (1 - b z^-1 + a g z^-N) = a z^-N * X(z)
             *      \/
             *      y[n] - b y[n-1] + a g y[n-N] = a x[n-N]
             *      \/
             *      y[n] = b y[n-1] - a g y[n-N] + a x[n-N]
             *          Can be simplified to no need two buffers y[n-N] and x[n-N]
             *      \/
             *      y[n] = b y[n-1] + a ( x[n-N] - g y[n-N] )
             *      \/
             *      v[n-N] = x[n-N] - g y[n-N]
             *      y[n] = b y[n-1] + a v[n-N]
             */

            const float x = static_cast<float>(frame[sample][channel]);
            const float delayed = delay_output[channel][pos];

            // y[n] = b y[n-1] + a v[n-N]
            const float y = b * last_output[channel] + a * delayed;
            // v[n] = x[n] - g y[n]
            delay_output[channel][pos] = x - g * y;

            last_output[channel] = y;
            frame[sample][channel] = ClampToS32(y);
        }
    }

    delay_pos = (delay_pos + samples_per_frame) % delay_samples;
}

void ReverbEffect::Reset() {
    // TODO: Implement
}

void ReverbEffect::ParseConfig(const DspConfiguration::ReverbEffect& config, std::size_t index) {
    // TODO: Implement, log the configuration for now.
    LOG_DEBUG(Audio_DSP, "(stubbed) reverb_effect[{}], raw block: {}", index,
              Common::BytesToHex({reinterpret_cast<const u8*>(&config), sizeof(config)}, 4));
}

void ReverbEffect::ProcessFrame(QuadFrame32& frame) {
    // TODO: Implement
    (void)frame;
}
} // namespace AudioCore::HLE
