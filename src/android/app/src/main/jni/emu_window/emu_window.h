// Copyright 2019-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include <chrono>
#include <memory>
#include <vector>
#include <EGL/egl.h>
#include "core/frontend/emu_window.h"
#include "jni/performance_hint.h"

namespace Core {
class System;
}

class EmuWindow_Android : public Frontend::EmuWindow {
public:
    EmuWindow_Android(ANativeWindow* surface, bool is_secondary = false);
    ~EmuWindow_Android();

    /// Called by the onSurfaceChanges() method to change the surface
    bool OnSurfaceChanged(ANativeWindow* surface);

    /// Handles touch event that occur.(Touched or released)
    bool OnTouchEvent(int x, int y, bool pressed);

    /// Handles movement of touch pointer
    void OnTouchMoved(int x, int y);

    void MakeCurrent() override;

    void DoneCurrent() override;

    virtual void TryPresenting() {}
    virtual bool WaitForFrame(std::chrono::milliseconds timeout) {
        return false;
    }
    virtual void DonePresenting() {}
    // EGL Context must be shared
    // could probably use the existing
    // SharedContext for this instead, this is maybe temporary
    virtual EGLContext* GetEGLContext() {
        return nullptr;
    }
    virtual void StopPresenting() {}

protected:
    void OnFramebufferSizeChanged();

    void ReportFrameWork();

    /// Creates the API specific window surface
    virtual bool CreateWindowSurface() {
        return false;
    }

    /// Destroys the API specific window surface
    virtual void DestroyWindowSurface() {}

    /// Destroys the graphics context
    virtual void DestroyContext() {}

protected:
    ANativeWindow* render_window{};
    ANativeWindow* host_window{};

    int window_width{};
    int window_height{};

    std::unique_ptr<Frontend::GraphicsContext> core_context;

private:
    std::unique_ptr<PerformanceHintSession> performance_hint;
};
