// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include <string>
#include "common/settings.h"
#include "core/hle/service/cfg/cfg.h"

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

} // namespace LibRetro
