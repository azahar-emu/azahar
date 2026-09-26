// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#include "citra_libretro/video_views.h"
#include "core/3ds.h"

namespace LibRetro::VideoViews {

namespace {

Mode current_mode;

retro_video_view MakeView(const Common::Rectangle<u32>& rect, u32 x_offset, unsigned screen,
                          unsigned eye) {
    retro_video_view view{};
    view.x = rect.left + x_offset;
    view.y = rect.top;
    view.width = rect.GetWidth();
    view.height = rect.GetHeight();
    view.screen = screen;
    view.eye = eye;
    view.aspect_ratio = 0.0f;
    return view;
}

} // namespace

Mode SelectMode(bool frontend_layout, Settings::LayoutOption own_layout,
                Settings::StereoRenderOption own_3d, unsigned status, bool renderer_stereo) {
    const bool presents = (status & RETRO_VIDEO_VIEWS_STATUS_PRESENTS) != 0;
    const bool stereo = (status & RETRO_VIDEO_VIEWS_STATUS_STEREO) != 0;
    // In 2D, views only stand in for Azahar's own layout where they look the same.
    const bool own_is_default = own_layout == Settings::LayoutOption::Default &&
                                own_3d == Settings::StereoRenderOption::Off;
    Mode mode;
    mode.active = frontend_layout && presents && (stereo || own_is_default);
    mode.stereo = mode.active && renderer_stereo && stereo;
    return mode;
}

const Mode& CurrentMode() {
    return current_mode;
}

void SetCurrentMode(const Mode& mode) {
    current_mode = mode;
}

std::pair<u32, u32> PackedSize(bool stereo, u32 scale) {
    const u32 width = static_cast<u32>(Core::kScreenTopWidth) * (stereo ? 2 : 1);
    const u32 height = static_cast<u32>(Core::kScreenTopHeight + Core::kScreenBottomHeight);
    return {width * scale, height * scale};
}

std::vector<retro_video_view> BuildMap(const Layout::FramebufferLayout& layout, bool stereo,
                                       bool swapped) {
    const unsigned top = swapped ? 1 : 0;
    const unsigned bottom = swapped ? 0 : 1;
    std::vector<retro_video_view> views;
    if (stereo) {
        views.push_back(MakeView(layout.top_screen, 0, top, RETRO_VIDEO_VIEW_EYE_LEFT));
        // The right eye is where the full side-by-side draw puts it: see
        // RendererOpenGL::DrawTopScreen() in src/video_core/renderer_opengl/renderer_opengl.cpp and
        // RendererVulkan::DrawTopScreen() in src/video_core/renderer_vulkan/renderer_vulkan.cpp.
        views.push_back(
            MakeView(layout.top_screen, layout.width / 2, top, RETRO_VIDEO_VIEW_EYE_RIGHT));
    } else {
        views.push_back(MakeView(layout.top_screen, 0, top, RETRO_VIDEO_VIEW_EYE_NONE));
    }
    views.push_back(MakeView(layout.bottom_screen, 0, bottom, RETRO_VIDEO_VIEW_EYE_NONE));
    return views;
}

} // namespace LibRetro::VideoViews
