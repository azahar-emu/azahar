// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include <utility>
#include <vector>
#include "citra_libretro/libretro_views.h"
#include "common/common_types.h"
#include "common/settings.h"
#include "core/frontend/framebuffer_layout.h"

namespace LibRetro::VideoViews {

/// Whether the frame is packed for the frontend's views, and in stereo.
struct Mode {
    bool active = false; ///< The frontend lays out the screens.
    bool stereo = false; ///< Both eyes are drawn and shown.

    bool operator==(const Mode&) const = default;
};

/// The mode for the "Frontend Layout and 3D" option (true for Auto), the user's own Screen
/// Layout and Stereoscopic 3D Mode, the frontend's RETRO_VIDEO_VIEWS_STATUS_ flags, and whether
/// the renderer draws both eyes.
Mode SelectMode(bool frontend_layout, Settings::LayoutOption own_layout,
                Settings::StereoRenderOption own_3d, unsigned status, bool renderer_stereo);

/// The mode Azahar's layout currently follows.
const Mode& CurrentMode();
void SetCurrentMode(const Mode& mode);

/// The packed frame's size at a resolution factor: Azahar's default layout,
/// twice as wide in stereo for the full side-by-side draw.
std::pair<u32, u32> PackedSize(bool stereo, u32 scale);

/// The view map for a layout computed for the views packing. The top screen
/// is screen 0, or screen 1 when the screens are swapped.
std::vector<retro_video_view> BuildMap(const Layout::FramebufferLayout& layout, bool stereo,
                                       bool swapped);

} // namespace LibRetro::VideoViews
