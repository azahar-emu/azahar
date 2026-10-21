// Copyright 2019-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>
#include <android/native_window_jni.h>
#include "common/android_utils.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "input_common/main.h"
#include "jni/emu_window/emu_window.h"
#include "jni/id_cache.h"
#include "jni/input_manager.h"
#include "network/network.h"
#include "video_core/renderer_base.h"

bool EmuWindow_Android::OnSurfaceChanged(ANativeWindow* surface) {
    int temp_width = (surface == nullptr) ? 0 : ANativeWindow_getWidth(surface);
    int temp_height = (surface == nullptr) ? 0 : ANativeWindow_getHeight(surface);
    if (render_window == surface && temp_width == window_width && temp_height == window_height) {
        return false;
    }
    StopPresenting();
    window_width = temp_width;
    window_height = temp_height;
    render_window = surface;
    window_info.type = Frontend::WindowSystemType::Android;
    window_info.render_surface = surface;
    OnFramebufferSizeChanged();
    return true;
}

bool EmuWindow_Android::OnTouchEvent(int x, int y, bool pressed) {
    if (pressed) {
        return TouchPressed((unsigned)std::max(x, 0), (unsigned)std::max(y, 0));
    }

    TouchReleased();
    return true;
}

void EmuWindow_Android::OnTouchMoved(int x, int y) {
    TouchMoved((unsigned)std::max(x, 0), (unsigned)std::max(y, 0));
}

void EmuWindow_Android::ReportFrameWork() {
    if (is_secondary) {
        return;
    }
    auto& system = Core::System::GetInstance();
    if (!system.perf_stats) {
        return;
    }

    // The way the emulator works causes Android to see the thread do a big
    // CPU burst followed by a sleep (due to the frame limiter). This makes
    // the OS see the thread sleeping for a long time, and puts it on slow
    // cores with reduced frequency, then when the next burst happens it
    // may do so in the slow core.
    // To prevent this we hint the OS that we are targetting half of what
    // the current frame time took so that it puts us in the beefy cores.
    constexpr double TARGET_RATIO = 0.5;

    const double frame_limit = Settings::GetFrameLimit();
    const double speed = frame_limit > 0 ? frame_limit / 100.0 : 1.0;
    const std::chrono::duration<double> frame_time{1.0 / (SCREEN_REFRESH_RATE * speed)};
    const auto target =
        std::chrono::duration_cast<std::chrono::nanoseconds>(frame_time * TARGET_RATIO);
    if (!performance_hint) {
        performance_hint = std::make_unique<PerformanceHintSession>(target);
    }
    performance_hint->ReportWork(system.perf_stats->GetLastSystemFrameTime(), target);
}

void EmuWindow_Android::OnFramebufferSizeChanged() {
    const bool is_portrait_mode = (AndroidUtils::IsPortraitMode() && !is_secondary);
    UpdateCurrentFramebufferLayout(window_width, window_height, is_portrait_mode);
}

EmuWindow_Android::EmuWindow_Android(ANativeWindow* surface, bool is_secondary)
    : EmuWindow{is_secondary}, host_window(surface) {
    LOG_DEBUG(Frontend, "Initializing EmuWindow_Android");
    if (!surface) {
        LOG_CRITICAL(Frontend, "surface is nullptr");
        return;
    }

    window_width = ANativeWindow_getWidth(surface);
    window_height = ANativeWindow_getHeight(surface);
}

EmuWindow_Android::~EmuWindow_Android() {
    DestroyWindowSurface();
    DestroyContext();
}

void EmuWindow_Android::MakeCurrent() {
    core_context->MakeCurrent();
}

void EmuWindow_Android::DoneCurrent() {
    core_context->DoneCurrent();
}
