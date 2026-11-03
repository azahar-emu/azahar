// Copyright 2019-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <vector>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "jni/emu_window/emu_window.h"

namespace Core {
class System;
}

struct ANativeWindow;

class EmuWindow_Android_OpenGL : public EmuWindow_Android {
public:
    EmuWindow_Android_OpenGL(Core::System& system, ANativeWindow* surface, bool is_secondary,
                             EGLContext* sharedContext = NULL);
    ~EmuWindow_Android_OpenGL() override = default;

    void TryPresenting() override;
    bool WaitForFrame(std::chrono::milliseconds timeout) override;
    void DonePresenting() override;
    void StopPresenting() override;
    void PollEvents() override;
    EGLContext* GetEGLContext() override;
    std::unique_ptr<GraphicsContext> CreateSharedContext() const override;

private:
    bool CreateWindowSurface() override;
    void DestroyWindowSurface() override;
    void DestroyContext() override;

private:
    Core::System& system;
    EGLConfig egl_config;
    EGLSurface egl_surface{};
    EGLContext egl_context{};
    EGLDisplay egl_display{};

    enum class PresentingState {
        Initial,
        Running,
        Stopped,
    };
    std::atomic<PresentingState> presenting_state{};

    bool surface_current{};

    std::mutex frame_mutex;
    std::condition_variable frame_cv;
    bool frame_ready{};
};
