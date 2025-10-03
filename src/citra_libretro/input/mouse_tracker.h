// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/math_util.h"
#include "core/frontend/framebuffer_layout.h"

#ifdef ENABLE_OPENGL
#include "video_core/renderer_opengl/gl_resource_manager.h"
#endif

namespace LibRetro {

namespace Input {

class CursorRenderer {
public:
    virtual ~CursorRenderer() = default;
    virtual void Render(int bufferWidth, int bufferHeight, float projectedX, float projectedY,
                        float renderRatio, const Layout::FramebufferLayout& layout,
                        void* framebuffer_data = nullptr) = 0;
};

/// The mouse tracker provides a mechanism to handle relative mouse/joypad input
///  for a touch-screen device.
class MouseTracker {
public:
    MouseTracker();
    ~MouseTracker();

    /// Called whenever a mouse moves.
    void OnMouseMove(int xDelta, int yDelta);

    /// Restricts the mouse cursor to a specified rectangle.
    void Restrict(int minX, int minY, int maxX, int maxY);

    /// Updates the tracker.
    void Update(int bufferWidth, int bufferHeight, const Layout::FramebufferLayout& layout);

    /// Renders the cursor to the screen (delegates to renderer-specific implementation).
    void Render(int bufferWidth, int bufferHeight, void* framebuffer_data = nullptr);

    /// If the touchscreen is being pressed.
    bool IsPressed() {
        return isPressed;
    }

    /// Get the pressed position, relative to the framebuffer.
    std::pair<unsigned, unsigned> GetPressedPosition() {
        return {static_cast<const unsigned int&>(projectedX),
                static_cast<const unsigned int&>(projectedY)};
    }

private:
    int x;
    int y;

    float lastMouseX;
    float lastMouseY;

    float projectedX;
    float projectedY;
    float renderRatio;

    bool isPressed;

    Layout::FramebufferLayout framebuffer_layout;
    std::unique_ptr<CursorRenderer> cursor_renderer;
};

#ifdef ENABLE_OPENGL
class OpenGLCursorRenderer : public CursorRenderer {
public:
    OpenGLCursorRenderer();
    ~OpenGLCursorRenderer();
    void Render(int bufferWidth, int bufferHeight, float projectedX, float projectedY,
                float renderRatio, const Layout::FramebufferLayout& layout,
                void* framebuffer_data = nullptr) override;

private:
    OpenGL::OGLProgram shader;
    OpenGL::OGLVertexArray vao;
    OpenGL::OGLBuffer vbo;
};
#endif

#ifdef ENABLE_VULKAN
class VulkanCursorRenderer : public CursorRenderer {
public:
    VulkanCursorRenderer();
    ~VulkanCursorRenderer();
    void Render(int bufferWidth, int bufferHeight, float projectedX, float projectedY,
                float renderRatio, const Layout::FramebufferLayout& layout,
                void* framebuffer_data = nullptr) override;
};
#endif

class SoftwareCursorRenderer : public CursorRenderer {
public:
    SoftwareCursorRenderer();
    ~SoftwareCursorRenderer();
    void Render(int bufferWidth, int bufferHeight, float projectedX, float projectedY,
                float renderRatio, const Layout::FramebufferLayout& layout,
                void* framebuffer_data = nullptr) override;
};

} // namespace Input

} // namespace LibRetro
