// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#ifdef ENABLE_OPENGL
#include <glad/glad.h>
#endif
#include <libretro.h>

#include "audio_core/audio_types.h"
#include "citra_libretro/citra_libretro.h"
#include "citra_libretro/environment.h"
#include "citra_libretro/input/input_factory.h"
#include "common/settings.h"
#include "core/3ds.h"
#ifdef ENABLE_OPENGL
#include "video_core/renderer_opengl/gl_state.h"
#endif
#include "video_core/gpu.h"
#include "video_core/renderer_software/renderer_software.h"

#ifdef ENABLE_OPENGL
/// LibRetro expects a "default" GL state.
void ResetGLState() {
    // Reset internal state.
    OpenGL::OpenGLState state{};
    state.Apply();

    // Clean up global state.
    if (!Settings::values.use_gles) {
        glLogicOp(GL_COPY);
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    glDisable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, 0, 0xFFFFFFFF);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ZERO);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);
    glBlendColor(0, 0, 0, 0);

    glDisable(GL_COLOR_LOGIC_OP);

    glDisable(GL_DITHER);

    glDisable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    glActiveTexture(GL_TEXTURE0);
}
#endif

EmuWindow_LibRetro::EmuWindow_LibRetro() {
    strict_context_required = true;
    window_info.type = Frontend::WindowSystemType::LibRetro;
}

EmuWindow_LibRetro::~EmuWindow_LibRetro() {}

void EmuWindow_LibRetro::SwapBuffers() {
    submittedFrame = true;

    switch (Settings::values.graphics_api.GetValue()) {
    case Settings::GraphicsAPI::OpenGL: {
#ifdef ENABLE_OPENGL
        auto current_state = OpenGL::OpenGLState::GetCurState();
        ResetGLState();
        if (enableEmulatedPointer && tracker) {
            tracker->Render(width, height);
        }
        LibRetro::UploadVideoFrame(RETRO_HW_FRAME_BUFFER_VALID, static_cast<unsigned>(width),
                                   static_cast<unsigned>(height), 0);
        ResetGLState();
        current_state.Apply();
#endif
        break;
    }
    case Settings::GraphicsAPI::Vulkan: {
#ifdef ENABLE_VULKAN
        if (enableEmulatedPointer && tracker) {
            tracker->Render(width, height);
        }
        LibRetro::UploadVideoFrame(RETRO_HW_FRAME_BUFFER_VALID, static_cast<unsigned>(width),
                                   static_cast<unsigned>(height), 0);
#endif
        break;
    }
    case Settings::GraphicsAPI::Software: {
        retro_framebuffer fb;
        void* data;
        bool did_malloc = false;
        if (LibRetro::GetSoftwareFramebuffer(&fb, width, height)) {
            data = fb.data;
        } else {
            data = calloc(1, width * height * 4);
            did_malloc = true;
        }

        auto& system = Core::System::GetInstance();
        const auto& renderer = static_cast<SwRenderer::RendererSoftware&>(system.GPU().Renderer());

        const auto& tl_info = renderer.Screen(VideoCore::ScreenId::TopLeft);
        // this is not correct, I just did this to see if I could see anything at all
        std::memcpy(data, tl_info.pixels.data(), tl_info.pixels.size());
        const auto& b_info = renderer.Screen(VideoCore::ScreenId::Bottom);
        // this is also not correct
        std::memcpy(((uint8_t*)data) + tl_info.pixels.size(), b_info.pixels.data(),
                    b_info.pixels.size());

        // Software cursor rendering with framebuffer access
        if (enableEmulatedPointer && tracker) {
            tracker->Render(width, height, data);
        }

        LibRetro::UploadVideoFrame(data, static_cast<unsigned>(width),
                                   static_cast<unsigned>(height), 0);
        if (did_malloc)
            free(data);
        break;
    }
    }
}

void EmuWindow_LibRetro::SetupFramebuffer() {
    if (Settings::values.graphics_api.GetValue() != Settings::GraphicsAPI::OpenGL)
        return;

#ifdef ENABLE_OPENGL
    // TODO: Expose interface in renderer_opengl to configure this in it's internal state
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(LibRetro::GetFramebuffer()));

    // glClear can be a slow path - skip clearing if we don't need to.
    if (doCleanFrame) {
        glClear(GL_COLOR_BUFFER_BIT);

        doCleanFrame = false;
    }
#endif
}

void EmuWindow_LibRetro::PollEvents() {
    LibRetro::PollInput();

    // TODO: Poll for right click for motion emu

    if (enableEmulatedPointer && tracker) {
        tracker->Update(width, height, GetFramebufferLayout());

        if (tracker->IsPressed()) {
            auto mousePos = tracker->GetPressedPosition();

            if (hasTouched) {
                TouchMoved(mousePos.first, mousePos.second);
            } else {
                TouchPressed(mousePos.first, mousePos.second);
                hasTouched = true;
            }
        } else if (hasTouched) {
            hasTouched = false;
            TouchReleased();
        }
    }
}

void EmuWindow_LibRetro::MakeCurrent() {
    // They don't get any say in the matter - GL context is always current!
}

void EmuWindow_LibRetro::DoneCurrent() {
    // They don't get any say in the matter - GL context is always current!
}

void EmuWindow_LibRetro::OnMinimalClientAreaChangeRequest(std::pair<u32, u32> _minimal_size) {}

void EmuWindow_LibRetro::UpdateLayout() {
    // TODO: Handle custom layouts
    // TODO: Extract this ugly thing somewhere else
    unsigned baseX;
    unsigned baseY;

    float scaling = Settings::values.resolution_factor.GetValue();

    bool swapped = Settings::values.swap_screen.GetValue();

    enableEmulatedPointer = true;

    switch (Settings::values.layout_option.GetValue()) {
    case Settings::LayoutOption::SingleScreen:
        if (swapped) { // Bottom screen visible
            baseX = Core::kScreenBottomWidth;
            baseY = Core::kScreenBottomHeight;
        } else { // Top screen visible
            baseX = Core::kScreenTopWidth;
            baseY = Core::kScreenTopHeight;
            enableEmulatedPointer = false;
        }
        baseX *= scaling;
        baseY *= scaling;
        break;
    case Settings::LayoutOption::LargeScreen:
        if (swapped) { // Bottom screen biggest
            baseX = Core::kScreenBottomWidth + Core::kScreenTopWidth / 4;
            baseY = Core::kScreenBottomHeight;
        } else { // Top screen biggest
            baseX = Core::kScreenTopWidth + Core::kScreenBottomWidth / 4;
            baseY = Core::kScreenTopHeight;
        }

        if (scaling < 4) {
            // Unfortunately, to get this aspect ratio correct (and have non-blurry 1x scaling),
            //  we have to have a pretty large buffer for the minimum ratio.
            baseX *= 4;
            baseY *= 4;
        } else {
            baseX *= scaling;
            baseY *= scaling;
        }
        break;
    case Settings::LayoutOption::SideScreen:
        baseX = Core::kScreenBottomWidth + Core::kScreenTopWidth;
        baseY = Core::kScreenTopHeight;
        baseX *= scaling;
        baseY *= scaling;
        break;
    case Settings::LayoutOption::Default:
    default:
        baseX = Core::kScreenTopWidth;
        baseY = Core::kScreenTopHeight + Core::kScreenBottomHeight;
        baseX *= scaling;
        baseY *= scaling;
        break;
    }

    // Update Libretro with our status
    struct retro_system_av_info info{};
    info.timing.fps = 60.0;
    info.timing.sample_rate = AudioCore::native_sample_rate;
    info.geometry.aspect_ratio = (float)baseX / (float)baseY;
    info.geometry.base_width = baseX;
    info.geometry.base_height = baseY;
    info.geometry.max_width = baseX;
    info.geometry.max_height = baseY;
    if (!LibRetro::SetGeometry(&info)) {
        LOG_CRITICAL(Frontend, "Failed to update 3DS layout in frontend!");
    }

    width = baseX;
    height = baseY;

    UpdateCurrentFramebufferLayout(baseX, baseY);

    doCleanFrame = true;
}

bool EmuWindow_LibRetro::NeedsClearing() const {
    // We manage this ourselves.
    return false;
}

bool EmuWindow_LibRetro::HasSubmittedFrame() {
    bool state = submittedFrame;
    submittedFrame = false;
    return state;
}

void EmuWindow_LibRetro::CreateContext() {
    tracker = std::make_unique<LibRetro::Input::MouseTracker>();

    doCleanFrame = true;
}

void EmuWindow_LibRetro::DestroyContext() {
    tracker = nullptr;
}
