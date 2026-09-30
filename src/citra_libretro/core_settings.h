// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include <array>
#include <string>
#include "common/settings.h"
#include "core/hle/service/cfg/cfg.h"
#include "libretro.h"

namespace LibRetro {

enum CStickFunction { Both, CStick, Touchscreen };

struct CoreSettings {

    std::string file_path;

    float analog_deadzone = 1.f;

    LibRetro::CStickFunction analog_function;

    bool enable_mouse_touchscreen;

    Service::CFG::SystemLanguage language_value;

    bool enable_touch_touchscreen;

    bool enable_touch_pointer_timeout;

    std::string swap_screen_mode;

    bool enable_motion;

    float motion_sensitivity;

    /// "Frontend Layout and 3D" is Auto: the frontend lays out the screens when it can.
    bool frontend_layout;

    /// Azahar's own Screen Layout, Stereoscopic 3D Mode and Depth, as the user chose them.
    Settings::LayoutOption layout_option;

    Settings::StereoRenderOption render_3d;

    u32 factor_3d;

} extern settings;

void RegisterCoreOptions(void);
void ParseCoreOptions(void);

/// Sets Azahar's layout, 3D mode and 3D slider from the options and the video views mode.
void ApplyLayoutSettings(void);

/// Whether to show the Layout options the frontend's views can override, for the "Frontend
/// Layout and 3D" option (true for Auto) and the frontend's RETRO_VIDEO_VIEWS_STATUS_ flags.
std::array<retro_core_option_display, 3> LayoutOptionsDisplay(bool frontend_layout,
                                                              unsigned status);

/// Shows or hides those options in the frontend's menu; returns whether any changed.
bool UpdateLayoutOptionsDisplay(bool frontend_layout, unsigned status);

} // namespace LibRetro
