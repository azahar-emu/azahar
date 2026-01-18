// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>
#include "core/hle/service/cfg/cfg.h"

namespace LibRetro {

enum CStickFunction { Both, CStick, Touchscreen };

struct CoreSettings {

    ::std::string file_path;

    float deadzone = 1.f;

    LibRetro::CStickFunction analog_function;

    bool mouse_touchscreen;

    Service::CFG::SystemLanguage language_value;

    bool touch_touchscreen;

    bool render_touchscreen;

    bool toggle_swap_screen;

    bool motion_enabled;

    float motion_sensitivity;

} extern settings;

void RegisterCoreOptions(void);
void ParseCoreOptions(void);

} // namespace LibRetro
