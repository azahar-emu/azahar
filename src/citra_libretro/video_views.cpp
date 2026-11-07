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
                Settings::StereoRenderOption own_3d, bool swapped, unsigned status,
                bool renderer_stereo) {
    const bool presents = (status & RETRO_VIDEO_VIEWS_STATUS_PRESENTS) != 0;
    const bool stereo = (status & RETRO_VIDEO_VIEWS_STATUS_STEREO) != 0;
    const bool single = own_layout == Settings::LayoutOption::SingleScreen;
    // In 2D, views only stand in for Azahar's own layout where they look the same.
    const bool own_matches = (own_layout == Settings::LayoutOption::Default || single) &&
                             own_3d == Settings::StereoRenderOption::Off;
    Mode mode;
    mode.active = frontend_layout && presents && (stereo || own_matches);
    mode.single = mode.active && single;
    // Only the top screen has 3D, so the bottom screen alone is drawn once.
    const bool top_shown = !(mode.single && swapped);
    mode.stereo = mode.active && renderer_stereo && stereo && top_shown;
    return mode;
}

const Mode& CurrentMode() {
    return current_mode;
}

void SetCurrentMode(const Mode& mode) {
    current_mode = mode;
}

std::pair<u32, u32> PackedSize(const Mode& mode, bool swapped, u32 scale) {
    u32 width = static_cast<u32>(Core::kScreenTopWidth);
    u32 height = static_cast<u32>(Core::kScreenTopHeight + Core::kScreenBottomHeight);
    if (mode.single) {
        width = static_cast<u32>(swapped ? Core::kScreenBottomWidth : Core::kScreenTopWidth);
        height = static_cast<u32>(swapped ? Core::kScreenBottomHeight : Core::kScreenTopHeight);
    }
    if (mode.stereo) {
        width *= 2;
    }
    return {width * scale, height * scale};
}

std::vector<retro_video_view> BuildMap(const Layout::FramebufferLayout& layout, bool stereo,
                                       bool swapped) {
    // Screens number in the order Prominent 3DS Screen puts them; a screen shown alone is 0.
    const unsigned top = (swapped && layout.bottom_screen_enabled) ? 1 : 0;
    const unsigned bottom = (!swapped && layout.top_screen_enabled) ? 1 : 0;
    std::vector<retro_video_view> views;
    if (layout.top_screen_enabled && stereo) {
        views.push_back(MakeView(layout.top_screen, 0, top, RETRO_VIDEO_VIEW_EYE_LEFT));
        // The right eye is where the full side-by-side draw puts it: see
        // RendererOpenGL::DrawTopScreen() in src/video_core/renderer_opengl/renderer_opengl.cpp and
        // RendererVulkan::DrawTopScreen() in src/video_core/renderer_vulkan/renderer_vulkan.cpp.
        views.push_back(
            MakeView(layout.top_screen, layout.width / 2, top, RETRO_VIDEO_VIEW_EYE_RIGHT));
    } else if (layout.top_screen_enabled) {
        views.push_back(MakeView(layout.top_screen, 0, top, RETRO_VIDEO_VIEW_EYE_NONE));
    }
    if (layout.bottom_screen_enabled) {
        views.push_back(MakeView(layout.bottom_screen, 0, bottom, RETRO_VIDEO_VIEW_EYE_NONE));
    }
    return views;
}

} // namespace LibRetro::VideoViews
