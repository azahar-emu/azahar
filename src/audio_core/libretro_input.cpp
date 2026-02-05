// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <optional>
#include <vector>
#include "audio_core/libretro_input.h"
#include "citra_libretro/environment.h"
#include "common/logging/log.h"
#include "common/ring_buffer.h"
#include "libretro.h"

namespace AudioCore {

namespace {
// Global instance pointer for access from retro_run
LibRetroInput* g_libretro_input = nullptr;
} // namespace

struct LibRetroInput::Impl {
    std::optional<retro_microphone_interface> mic_interface;
    retro_microphone_t* mic_handle = nullptr;
    bool is_sampling = false;
    u8 sample_size_in_bytes = 2;
    int warmup_frames = 0;

    // The rate at which the frontend actually provides samples (may differ from
    // what the 3DS mic service requested). We open the mic at this rate to avoid
    // RetroArch's internal resampler path, which has a convergence bug when
    // downsampling (ratio < 1). We resample ourselves in Read() instead.
    u32 native_sample_rate = 0;

    // Ring buffer for thread-safe sample storage
    // Capacity: 4096 samples should be plenty for buffering between frames
    // The 3DS mic service reads 16 samples at a time at ~32728 Hz
    Common::RingBuffer<s16, 4096> sample_buffer;

    // Temporary buffer for reading from frontend
    std::vector<s16> read_buffer;

    Impl() {
        // Try to get the microphone interface from the frontend
        retro_microphone_interface interface{};
        interface.interface_version = RETRO_MICROPHONE_INTERFACE_VERSION;

        if (LibRetro::GetMicrophoneInterface(&interface)) {
            if (interface.interface_version == RETRO_MICROPHONE_INTERFACE_VERSION) {
                mic_interface = interface;
                LOG_INFO(Audio, "LibRetro microphone interface available (version {})",
                         interface.interface_version);
            } else {
                LOG_WARNING(Audio,
                            "LibRetro microphone interface version mismatch: expected {}, got {}",
                            RETRO_MICROPHONE_INTERFACE_VERSION, interface.interface_version);
            }
        } else {
            LOG_WARNING(Audio, "LibRetro microphone interface not available");
        }

        // Keep this small enough that RetroArch's microphone_driver_read can
        // fill its outgoing FIFO in a single flush iteration. The CoreAudio
        // driver's internal FIFO is ~480 samples (10ms at 48kHz). If we
        // request more than that, the blocking while-loop in
        // microphone_driver_read must wait for the next hardware callback,
        // and on ARM64 without memory barriers in the FIFO, it may never
        // see the new data. 128 samples is conservative enough to succeed
        // in one pass.
        read_buffer.resize(128);
    }

    ~Impl() {
        CloseMicrophone();
    }

    bool EnsureMicrophoneOpen() {
        if (mic_handle) {
            return true;
        }

        if (!mic_interface) {
            return false;
        }

        // Always open at 48000 Hz regardless of what the game requests.
        // RetroArch's microphone_driver_read has a resampler whose while-loop
        // deadlocks when the ratio is < 1 (core rate < device rate). The
        // libretro get_params API only returns the effective (requested) rate,
        // not the device's native rate, so we can't detect the mismatch.
        // Opening at 48000 Hz (the most common hardware rate) keeps the
        // frontend's internal resampling ratio at or near 1.0, avoiding the
        // bug. We resample to the game's requested rate ourselves in Read().
        static constexpr u32 kMicOpenRate = 48000;
        native_sample_rate = kMicOpenRate;

        retro_microphone_params_t params{};
        params.rate = kMicOpenRate;

        mic_handle = mic_interface->open_mic(&params);
        if (!mic_handle) {
            LOG_ERROR(Audio, "Failed to open LibRetro microphone");
            return false;
        }

        // The frontend may start recording immediately in open_mic (e.g.
        // CoreAudio calls AudioOutputUnitStart). Pause it right away so the
        // mic is available but idle until StartSampling enables it.
        mic_interface->set_mic_state(mic_handle, false);

        LOG_INFO(Audio, "LibRetro microphone opened at {} Hz (idle)", native_sample_rate);
        return true;
    }

    void CloseMicrophone() {
        if (mic_interface && mic_handle) {
            mic_interface->close_mic(mic_handle);
            mic_handle = nullptr;
        }
    }

    bool SetMicrophoneActive(bool active) {
        if (!mic_interface || !mic_handle) {
            return false;
        }
        return mic_interface->set_mic_state(mic_handle, active);
    }

    bool IsMicrophoneActive() const {
        if (!mic_interface || !mic_handle) {
            return false;
        }
        return mic_interface->get_mic_state(mic_handle);
    }
};

LibRetroInput::LibRetroInput() : impl(std::make_unique<Impl>()) {
    g_libretro_input = this;
}

LibRetroInput::~LibRetroInput() {
    StopSampling();
    if (g_libretro_input == this) {
        g_libretro_input = nullptr;
    }
}

void LibRetroInput::StartSampling(const InputParameters& params) {
    if (IsSampling()) {
        return;
    }

    // LibRetro only provides signed 16-bit PCM samples
    // We'll convert to the requested format in Read()
    if (params.sign == Signedness::Unsigned) {
        LOG_DEBUG(Audio, "Application requested unsigned PCM format; will convert from signed.");
    }

    parameters = params;
    impl->sample_size_in_bytes = params.sample_size / 8;

    if (!impl->EnsureMicrophoneOpen()) {
        LOG_WARNING(Audio, "Cannot start sampling: microphone not available");
        return;
    }

    // Enable the microphone (transitions from idle to recording)
    if (!impl->SetMicrophoneActive(true)) {
        LOG_ERROR(Audio, "Failed to activate microphone");
        return;
    }

    impl->is_sampling = true;
    // Give the audio hardware a few frames to start delivering data before
    // we attempt a (blocking) read_mic call. Without this, the very first
    // read can hang because the CoreAudio callback hasn't fired yet.
    impl->warmup_frames = 10;
    LOG_INFO(Audio, "LibRetro microphone sampling started at {} Hz, {} bit", params.sample_rate,
             params.sample_size);
}

void LibRetroInput::StopSampling() {
    if (!impl->is_sampling) {
        return;
    }

    impl->SetMicrophoneActive(false);
    impl->is_sampling = false;

    LOG_INFO(Audio, "LibRetro microphone sampling stopped (mic remains idle)");
}

bool LibRetroInput::IsSampling() {
    return impl->is_sampling;
}

void LibRetroInput::AdjustSampleRate(u32 sample_rate) {
    if (!IsSampling()) {
        return;
    }

    // Restart with new sample rate
    auto new_parameters = parameters;
    new_parameters.sample_rate = sample_rate;
    StopSampling();
    StartSampling(new_parameters);
}

void LibRetroInput::PollMicrophone() {
    // This is called from the main thread (retro_run)
    // Read samples from the frontend and push to the ring buffer

    if (!impl->is_sampling || !impl->mic_interface || !impl->mic_handle) {
        return;
    }

    // Wait for the audio hardware to start delivering data before making
    // any blocking read_mic calls.
    if (impl->warmup_frames > 0) {
        impl->warmup_frames--;
        return;
    }

    // Issue a memory fence before reading. RetroArch's CoreAudio mic driver
    // fills its FIFO from a callback thread without memory barriers. On ARM64
    // (weak memory model), the main thread may not see the callback's writes
    // without an explicit barrier.
    std::atomic_thread_fence(std::memory_order_acquire);

    int samples_read = impl->mic_interface->read_mic(impl->mic_handle, impl->read_buffer.data(),
                                                     static_cast<size_t>(impl->read_buffer.size()));

    if (samples_read > 0) {
        impl->sample_buffer.Push(
            std::span<const s16>(impl->read_buffer.data(), static_cast<size_t>(samples_read)));
    }
}

Samples LibRetroInput::Read() {
    // This is called from the CoreTiming scheduler thread
    // Pop samples from the ring buffer (thread-safe)

    if (!impl->is_sampling) {
        return {};
    }

    // Pop available samples from the buffer (at native device rate)
    std::vector<s16> raw_samples = impl->sample_buffer.Pop();

    if (raw_samples.empty()) {
        return {};
    }

    // Resample from native device rate to the rate the 3DS mic service expects
    if (impl->native_sample_rate != 0 && impl->native_sample_rate != parameters.sample_rate) {
        double ratio = static_cast<double>(parameters.sample_rate) / impl->native_sample_rate;
        auto output_count = static_cast<std::size_t>(raw_samples.size() * ratio);
        if (output_count == 0) {
            return {};
        }
        std::vector<s16> resampled(output_count);
        for (std::size_t i = 0; i < output_count; i++) {
            double src_pos = i / ratio;
            auto idx = static_cast<std::size_t>(src_pos);
            double frac = src_pos - idx;
            if (idx + 1 < raw_samples.size()) {
                resampled[i] =
                    static_cast<s16>(raw_samples[idx] * (1.0 - frac) + raw_samples[idx + 1] * frac);
            } else {
                resampled[i] = raw_samples[std::min(idx, raw_samples.size() - 1)];
            }
        }
        raw_samples = std::move(resampled);
    }

    // Convert sample format if needed
    constexpr auto convert_s16_to_u16 = [](s16 sample) -> u16 {
        return static_cast<u16>(sample) ^ 0x8000;
    };

    constexpr auto convert_s16_to_s8 = [](s16 sample) -> s8 {
        return static_cast<s8>(sample >> 8);
    };

    constexpr auto convert_s16_to_u8 = [](s16 sample) -> u8 {
        return static_cast<u8>((static_cast<u16>(sample) ^ 0x8000) >> 8);
    };

    Samples output;
    output.reserve(raw_samples.size() * impl->sample_size_in_bytes);

    if (impl->sample_size_in_bytes == 1) {
        // 8-bit output
        if (parameters.sign == Signedness::Unsigned) {
            for (s16 sample : raw_samples) {
                output.push_back(convert_s16_to_u8(sample));
            }
        } else {
            for (s16 sample : raw_samples) {
                output.push_back(static_cast<u8>(convert_s16_to_s8(sample)));
            }
        }
    } else {
        // 16-bit output
        if (parameters.sign == Signedness::Unsigned) {
            for (s16 sample : raw_samples) {
                u16 converted = convert_s16_to_u16(sample);
                output.push_back(static_cast<u8>(converted & 0xFF));
                output.push_back(static_cast<u8>(converted >> 8));
            }
        } else {
            // Signed 16-bit - just copy the raw bytes
            const u8* data = reinterpret_cast<const u8*>(raw_samples.data());
            output.insert(output.end(), data, data + raw_samples.size() * 2);
        }
    }

    return output;
}

LibRetroInput* GetLibRetroInput() {
    return g_libretro_input;
}

} // namespace AudioCore
