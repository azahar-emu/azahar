// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <utility>
#include "core/frontend/emu_window.h"
#include "citra_libretro/input/mouse_tracker.h"

void ResetGLState();

class EmuWindow_LibRetro : public Frontend::EmuWindow {
public:
    EmuWindow_LibRetro();
    ~EmuWindow_LibRetro();

    /// Swap buffers to display the next frame
    void SwapBuffers() override;

    /// Polls window events
    void PollEvents() override;

    /// Makes the graphics context current for the caller thread
    void MakeCurrent() override;

    /// Releases the GL context from the caller thread
    void DoneCurrent() override;

    void SetupFramebuffer() override;

    /// Prepares the window for rendering
    void UpdateLayout();

    /// States whether a frame has been submitted. Resets after call.
    bool HasSubmittedFrame();

    /// Flags that the framebuffer should be cleared.
    bool NeedsClearing() const override;

    /// Creates state for a currently running OpenGL context.
    void CreateContext();

    /// Destroys a currently running OpenGL context.
    void DestroyContext();

private:
    /// Called when a configuration change affects the minimal size of the window
    void OnMinimalClientAreaChangeRequest(std::pair<u32, u32> minimal_size) override;

    int width;
    int height;

    bool submittedFrame = false;

    // Hack to ensure stuff runs on the main thread
    bool doCleanFrame = false;

#ifdef ENABLE_OPENGL
    // For tracking LibRetro state
    bool hasTouched = false;

    // For tracking mouse cursor
    std::unique_ptr<LibRetro::Input::MouseTracker> tracker = nullptr;
#endif

    bool enableEmulatedPointer = false;
};
