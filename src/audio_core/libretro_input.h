// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include "audio_core/input.h"

namespace AudioCore {

class LibRetroInput final : public Input {
public:
    LibRetroInput();
    ~LibRetroInput() override;

    void StartSampling(const InputParameters& params) override;
    void StopSampling() override;
    bool IsSampling() override;
    void AdjustSampleRate(u32 sample_rate) override;
    Samples Read() override;

    /// Called from main thread (retro_run) to read samples from the frontend
    /// and store them in the thread-safe buffer for Read() to consume.
    void PollMicrophone();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

/// Returns the global LibRetroInput instance, or nullptr if not initialized.
/// This is used by citra_libretro.cpp to poll the microphone from the main thread.
LibRetroInput* GetLibRetroInput();

} // namespace AudioCore
