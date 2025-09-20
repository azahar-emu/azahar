// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <list>
#include <numeric>
#include <libretro.h>
#include "audio_core/libretro_sink.h"
#include "audio_types.h"
#include "common/settings.h"

namespace LibRetro {
static retro_audio_sample_batch_t audio_batch_cb;
}

namespace AudioCore {

struct LibRetroSink::Impl {
    std::function<void(s16*, std::size_t)> cb;
};

LibRetroSink::LibRetroSink(std::string target_device_name) : impl(std::make_unique<Impl>()) {}

LibRetroSink::~LibRetroSink() {}

unsigned int LibRetroSink::GetNativeSampleRate() const {
    return native_sample_rate; // We specify this.
}

void LibRetroSink::SetCallback(std::function<void(s16*, std::size_t)> cb) {
    this->impl->cb = cb;
}

void LibRetroSink::OnAudioSubmission(std::size_t frames) {
    std::vector<s16> buffer(frames * 2);

    this->impl->cb(buffer.data(), buffer.size() / 2);

    LibRetro::SubmitAudio(buffer.data(), buffer.size() / 2);
}

std::vector<std::string> ListLibretroSinkDevices() {
    return std::vector<std::string>{"LibRetro"};
}

} // namespace AudioCore

void LibRetro::SubmitAudio(const int16_t* data, size_t frames) {
    LibRetro::audio_batch_cb(data, frames);
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
    LibRetro::audio_batch_cb = cb;
}
