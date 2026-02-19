// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "audio_core/libretro_sink.h"
#include "citra_libretro/environment.h"

namespace AudioCore {

LibRetroSink::LibRetroSink(std::string) {}

LibRetroSink::~LibRetroSink() = default;

unsigned int LibRetroSink::GetNativeSampleRate() const {
    return native_sample_rate;
}

void LibRetroSink::PushSamples(const void* data, std::size_t num_samples) {
    // libretro calls stereo pairs "frames", Azahar calls them "samples"
    LibRetro::SubmitAudio(static_cast<const s16*>(data), num_samples);
}

std::vector<std::string> ListLibretroSinkDevices() {
    return std::vector<std::string>{"LibRetro"};
}

} // namespace AudioCore
