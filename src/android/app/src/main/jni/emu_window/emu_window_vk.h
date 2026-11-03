// Copyright 2022-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include "jni/emu_window/emu_window.h"

struct ANativeWindow;

class EmuWindow_Android_Vulkan : public EmuWindow_Android {
public:
    EmuWindow_Android_Vulkan(ANativeWindow* surface,
                             std::shared_ptr<Common::DynamicLibrary> driver_library,
                             bool is_secondary);
    ~EmuWindow_Android_Vulkan() override = default;

    void PollEvents() override {
        ReportFrameWork();
    }

    std::unique_ptr<GraphicsContext> CreateSharedContext() const override;

    std::shared_ptr<Common::DynamicLibrary> GetDriverLibrary() override;

private:
    bool CreateWindowSurface() override;

private:
    std::shared_ptr<Common::DynamicLibrary> driver_library;
};
