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

void ReverbEffect::DelayLine::Resize(u32 samples) {
    if (samples == length && !buffer[0].empty()) {
        return;
    }
    length = samples;
    pos = 0;
    for (auto& channel : buffer) {
        channel.assign(samples, 0.0f);
    }
}

void ReverbEffect::DelayLine::Clear() {
    length = 0;
    pos = 0;
    for (auto& channel : buffer) {
        channel.clear();
    }
}

void ReverbEffect::Reset() {
    enabled = false;
    pre_delay.Clear();
    early_delay.Clear();
    for (auto& line : comb) {
        line.Clear();
    }
    all_pass.Clear();
    for (auto& state : comb_filter_state) {
        state.fill(0.0f);
    }
    early_g = fused_g = all_pass_coef = 0.0f;
    comb_coef.fill(0.0f);
    a = b = 0.0f;
}

u32 ReverbEffect::FrameCountToSamples(s16 frame_count, std::size_t index, const char* name) const {
    if (frame_count <= 0) {
        return 0;
    }
    u32 samples = static_cast<u32>(frame_count) * samples_per_frame;
    if (samples > max_delay_samples) {
        LOG_WARNING(Audio_DSP, "reverb_effect[{}] {} frame_count={} exceeds sane range, clamping",
                    index, name, frame_count);
        samples = max_delay_samples;
    }
    return samples;
}

void ReverbEffect::ParseConfig(DspConfiguration::ReverbEffect& config, std::size_t index) {
    if (!config.dirty_raw) {
        return;
    }

    const bool was_enabled = enabled;
    enabled = config.enable != 0;

    if (!enabled) {
        config.dirty_raw = 0;
        if (was_enabled) {
            LOG_DEBUG(Audio_DSP, "reverb_effect[{}] disabled", index);
            Reset();
        }
        return;
    }

    pre_delay.Resize(FrameCountToSamples(config.pre_delay_frame_count, index, "pre_delay"));
    early_delay.Resize(FrameCountToSamples(config.early_delay_frame_count, index, "early_delay"));
    all_pass.Resize(FrameCountToSamples(config.all_pass_frame_count, index, "all_pass"));
    for (std::size_t i = 0; i < comb.size(); i++) {
        comb[i].Resize(FrameCountToSamples(config.comb_frame_count[i], index, "comb"));
    }

    if (!comb[0].Empty() && comb[0].length == comb[1].length) {
        LOG_WARNING(Audio_DSP, "reverb_effect[{}] both combs are {} samples, tail will be thin",
                    index, comb[0].length);
    }

    static constexpr float q7_scale = 1.0f / 128.0f;

    early_g = static_cast<u16>(config.early_g) * q7_scale;
    fused_g = static_cast<u16>(config.fused_g) * q7_scale;
    all_pass_coef = static_cast<u16>(config.all_pass_coef) * q7_scale;
    for (std::size_t i = 0; i < comb_coef.size(); i++) {
        comb_coef[i] = static_cast<u16>(config.comb_coef[i]) * q7_scale;
    }

    a = static_cast<s16>(config.a) * q7_scale;
    b = static_cast<s16>(config.b) * q7_scale;

    LOG_DEBUG(Audio_DSP,
              "reverb_effect[{}] enabled pre_delay={} early_delay={} comb={{{},{}}} all_pass={} "
              "early_g={} fused_g={} comb_coef={{{},{}}} all_pass_coef={} a={} b={} outputs={:#x}",
              index, pre_delay.length, early_delay.length, comb[0].length, comb[1].length,
              all_pass.length, early_g, fused_g, comb_coef[0], comb_coef[1], all_pass_coef, a, b,
              static_cast<u32>(config.outputs));

    config.dirty_raw = 0;
}

void ReverbEffect::ProcessFrame(QuadFrame32& frame) {
    if (!enabled) {
        return;
    }

    for (std::size_t sample = 0; sample < samples_per_frame; sample++) {
        for (std::size_t channel = 0; channel < 4; channel++) {
            const float x = static_cast<float>(frame[sample][channel]);

            // Pre-delay: the gap before the tail starts building.
            float tail_in = x;
            if (!pre_delay.Empty()) {
                tail_in = pre_delay.Peek(channel);
                pre_delay.Poke(channel, x);
            }

            // Two comb filters in parallel, each with the single pole filter in its feedback arm:
            //  f[n] = b f[n-1] + a c[n-M]
            //  c[n] = tail_in[n] + comb_coef * f[n]
            float late = 0.0f;
            for (std::size_t i = 0; i < comb.size(); i++) {
                if (comb[i].Empty()) {
                    continue;
                }
                const float delayed = comb[i].Peek(channel);
                float& filter_state = comb_filter_state[i][channel];
                filter_state = b * filter_state + a * delayed;
                comb[i].Poke(channel, tail_in + comb_coef[i] * filter_state);
                late += delayed;
            }

            // All-pass diffuser, A(z) = (-k + z^-M) / (1 - k z^-M), which spreads the comb output
            // in time without colouring its magnitude response:
            //  v[n] = late[n] + k v[n-M]
            //  y[n] = v[n-M] - k v[n]
            if (!all_pass.Empty()) {
                const float delayed = all_pass.Peek(channel);
                const float v = late + all_pass_coef * delayed;
                all_pass.Poke(channel, v);
                late = delayed - all_pass_coef * v;
            }

            // Early reflections are tapped off the dry input, not off the tail.
            float early = x;
            if (!early_delay.Empty()) {
                early = early_delay.Peek(channel);
                early_delay.Poke(channel, x);
            }

            frame[sample][channel] = ClampToS32(early_g * early + fused_g * late);
        }

        // Every line advances one sample per sample, once all four channels have been processed.
        if (!pre_delay.Empty()) {
            pre_delay.Advance();
        }
        if (!early_delay.Empty()) {
            early_delay.Advance();
        }
        if (!all_pass.Empty()) {
            all_pass.Advance();
        }
        for (auto& line : comb) {
            if (!line.Empty()) {
                line.Advance();
            }
        }
    }
}
} // namespace AudioCore::HLE
