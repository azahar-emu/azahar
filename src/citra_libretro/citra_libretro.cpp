// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <list>
#include <numeric>
#include <vector>
#include <common/file_util.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef ENABLE_OPENGL
#include "glad/glad.h"
#endif
#include "libretro.h"

#include "audio_core/libretro_sink.h"
#include "video_core/gpu.h"
#ifdef ENABLE_OPENGL
#include "video_core/renderer_opengl/renderer_opengl.h"
#endif
#ifdef ENABLE_VULKAN
#include "citra_libretro/libretro_vk.h"
#endif
#include "video_core/renderer_software/renderer_software.h"
#include "video_core/video_core.h"

#include "citra_libretro/citra_libretro.h"
#include "citra_libretro/core_settings.h"
#include "citra_libretro/environment.h"
#include "citra_libretro/input/input_factory.h"

#include "common/logging/backend.h"
#include "common/logging/filter.h"
#include "common/settings.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/frontend/applets/default_applets.h"
#include "core/frontend/image_interface.h"
#include "core/hle/kernel/memory.h"
#include "core/loader/loader.h"
#include "core/memory.h"

#ifdef HAVE_LIBRETRO_VFS
#include <streams/file_stream_transforms.h>
#endif

class CitraLibRetro {
public:
    CitraLibRetro() : log_filter(Common::Log::Level::Info) {}

    Common::Log::Filter log_filter;
    std::unique_ptr<EmuWindow_LibRetro> emu_window;
    bool game_loaded = false;
    struct retro_hw_render_callback hw_render{};
};

CitraLibRetro* emu_instance;

void retro_init() {
    emu_instance = new CitraLibRetro();
    Common::Log::LibRetroStart(LibRetro::GetLoggingBackend());
    Common::Log::SetGlobalFilter(emu_instance->log_filter);

    LOG_DEBUG(Frontend, "Initialising core...");

    // Set up LLE cores
    for (const auto& service_module : Service::service_module_map) {
        Settings::values.lle_modules.emplace(service_module.name, false);
    }

    // Setup default, stub handlers for HLE applets
    Frontend::RegisterDefaultApplets(Core::System::GetInstance());

    // Register generic image interface
    Core::System::GetInstance().RegisterImageInterface(
        std::make_shared<Frontend::ImageInterface>());

    LibRetro::Input::Init();
}

void retro_deinit() {
    LOG_DEBUG(Frontend, "Shutting down core...");
    if (Core::System::GetInstance().IsPoweredOn()) {
        Core::System::GetInstance().Shutdown();
    }

    LibRetro::Input::Shutdown();

    delete emu_instance;

    Common::Log::Stop();
}

unsigned retro_api_version() {
    return RETRO_API_VERSION;
}

void LibRetro::OnConfigureEnvironment() {

#ifdef HAVE_LIBRETRO_VFS
    struct retro_vfs_interface_info vfs_iface_info{1, nullptr};
    LibRetro::SetVFSCallback(&vfs_iface_info);
#endif

    std::string cpuScale = "CPU Clock scale; ";
    static const int MAX_CPU_SCALE = 400;
    static const int MIN_CPU_SCALE = 5;
    static const int DEFAULT_CPU_SCALE = 100;

    cpuScale.append(std::to_string(DEFAULT_CPU_SCALE) + "% (Default)|");

    for (int i = MIN_CPU_SCALE; i <= MAX_CPU_SCALE; i += 5) {
        if (i == DEFAULT_CPU_SCALE)
            continue;

        cpuScale.append(std::to_string(i) + "%");

        if (i != MAX_CPU_SCALE)
            cpuScale.append("|");
    }

    retro_variable values[] = {
        {"citra_graphics_api", "Graphics API; Auto"
#ifdef ENABLE_VULKAN
                               "|Vulkan"
#endif
#ifdef ENABLE_OPENGL
                               "|OpenGL"
#endif
                               "|Software"},
        {"citra_use_cpu_jit", "Enable CPU JIT; enabled|disabled"},
        {"citra_cpu_scale", cpuScale.c_str()},
        {"citra_use_shader_jit", "Enable shader JIT; enabled|disabled"},
        {"citra_use_hw_shaders", "Enable hardware shaders; enabled|disabled"},
        {"citra_use_hw_shader_cache", "Save hardware shader cache to disk; enabled|disabled"},
        {"citra_use_acc_geo_shaders",
         "Enable accurate geometry shaders (only for H/W shaders); enabled|disabled"},
        {"citra_use_acc_mul",
         "Enable accurate shaders multiplication (only for H/W shaders); enabled|disabled"},
        {"citra_texture_filter",
         "Texture filter type; none|Anime4K Ultrafast|Bicubic|NearestNeighbor|ScaleForce|xBRZ "
         "freescale|MMPX"},
        {"citra_texture_sampling", "Texture sampling type; GameControlled|NearestNeighbor|Linear"},
        {"citra_custom_textures", "Enable custom textures; disabled|enabled"},
        {"citra_dump_textures", "Dump textures; disabled|enabled"},
        {"citra_resolution_factor",
         "Resolution scale factor; 1x (Native)|2x|3x|4x|5x|6x|7x|8x|9x|10x"},
        {"citra_layout_option", "Screen layout positioning; Default Top-Bottom Screen|Single "
                                "Screen Only|Large Screen, Small Screen|Side by Side"},
        {"citra_swap_screen", "Prominent 3DS screen; Top|Bottom"},
        {"citra_swap_screen_mode", "Swap Screen Mode; Toggle|Hold"},
        {"citra_analog_function",
         "Right analog function; C-Stick and Touchscreen Pointer|Touchscreen Pointer|C-Stick"},
        {"citra_deadzone", "Emulated pointer deadzone (%); 15|20|25|30|35|0|5|10"},
        {"citra_mouse_touchscreen",
         "Simulate touchscreen interactions with mouse; enabled|disabled"},
        {"citra_touch_touchscreen",
         "Simulate touchscreen interactions with touchscreen; disabled|enabled"},
        {"citra_render_touchscreen", "Render simulated touchscreen interactions; disabled|enabled"},
        {"citra_use_virtual_sd", "Enable virtual SD card; enabled|disabled"},
        {"citra_use_libretro_save_path", "Savegame location; LibRetro Default|Azahar Default"},
        {"citra_is_new_3ds", "3DS system model; Old 3DS|New 3DS"},
        {"citra_region_value",
         "3DS system region; Auto|Japan|USA|Europe|Australia|China|Korea|Taiwan"},
        {"citra_language",
         "3DS system language; English|Japanese|French|Spanish|German|Italian|Dutch|Portuguese|"
         "Russian|Korean|Traditional Chinese|Simplified Chinese"},
        {"citra_use_gdbstub", "Enable GDB stub; disabled|enabled"},
        {nullptr, nullptr}};

    LibRetro::SetVariables(values);

    static const struct retro_controller_description controllers[] = {
        {"Nintendo 3DS", RETRO_DEVICE_JOYPAD},
    };

    static const struct retro_controller_info ports[] = {
        {controllers, 1},
        {nullptr, 0},
    };

    LibRetro::SetControllerInfo(ports);
}

uintptr_t LibRetro::GetFramebuffer() {
    return emu_instance->hw_render.get_current_framebuffer();
}

Settings::TextureFilter GetTextureFilter(std::string name) {
    if (name == "Anime4K Ultrafast")
        return Settings::TextureFilter::Anime4K;
    if (name == "Bicubic")
        return Settings::TextureFilter::Bicubic;
    if (name == "ScaleForce")
        return Settings::TextureFilter::ScaleForce;
    if (name == "xBRZ freescale")
        return Settings::TextureFilter::xBRZ;
    if (name == "MMPX")
        return Settings::TextureFilter::MMPX;

    return Settings::TextureFilter::NoFilter;
}

Settings::TextureSampling GetTextureSampling(std::string name) {
    if (name == "NearestNeighbor")
        return Settings::TextureSampling::NearestNeighbor;
    if (name == "Linear")
        return Settings::TextureSampling::Linear;

    return Settings::TextureSampling::GameControlled;
}

/**
 * Updates Citra's settings with Libretro's.
 */
void UpdateSettings() {
    struct retro_input_descriptor desc[] = {
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Up"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Down"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "X"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "Y"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "B"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "A"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "L"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, "ZL"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "R"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, "ZR"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3, "Home/Swap screens"},
        {0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3, "Touch Screen Touch"},
        {0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X,
         "Circle Pad X"},
        {0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y,
         "Circle Pad Y"},
        {0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X,
         "C-Stick / Pointer X"},
        {0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y,
         "C-Stick / Pointer Y"},
        {0, 0},
    };

    LibRetro::SetInputDescriptors(desc);

    // Some settings cannot be set by LibRetro frontends - options have to be
    // finite. Make assumptions.
    Settings::values.log_filter = "*:Info";
    Settings::values.output_type = AudioCore::SinkType::LibRetro;
    Settings::values.volume = 1.0f;

    // We don't need these, as this is the frontend's responsibility.
    Settings::values.enable_audio_stretching = false;
    Settings::values.frame_limit = 10000;

    // For our other settings, import them from LibRetro.
    Settings::values.use_cpu_jit =
        LibRetro::FetchVariable("citra_use_cpu_jit", "enabled") == "enabled";

    auto cpuScaling = LibRetro::FetchVariable("citra_cpu_scale", "100%");
    auto cpuScalingIndex = cpuScaling.find('%');
    if (cpuScalingIndex == std::string::npos) {
        LOG_ERROR(Frontend, "Failed to parse cpu scale!");
        Settings::values.cpu_clock_percentage = 100;
    } else {
        int scale = stoi(cpuScaling.substr(0, cpuScalingIndex));
        Settings::values.cpu_clock_percentage = scale;
    }

    auto graphicsApi = LibRetro::FetchVariable("citra_graphics_api", "Auto");
    if (graphicsApi == "Software") {
        Settings::values.graphics_api = Settings::GraphicsAPI::Software;
#ifdef ENABLE_VULKAN
    } else if (graphicsApi == "Vulkan") {
        Settings::values.graphics_api = Settings::GraphicsAPI::Vulkan;
#endif
#ifdef ENABLE_OPENGL
    } else if (graphicsApi == "OpenGL") {
        Settings::values.graphics_api = Settings::GraphicsAPI::OpenGL;
#endif
    } else {
        Settings::values.graphics_api = LibRetro::GetPreferredRenderer();
    }

    Settings::values.use_hw_shader =
        LibRetro::FetchVariable("citra_use_hw_shaders", "enabled") == "enabled";
    Settings::values.use_shader_jit =
        LibRetro::FetchVariable("citra_use_shader_jit", "enabled") == "enabled";
    Settings::values.shaders_accurate_mul =
        LibRetro::FetchVariable("citra_use_acc_mul", "enabled") == "enabled";
    Settings::values.use_virtual_sd =
        LibRetro::FetchVariable("citra_use_virtual_sd", "enabled") == "enabled";
    Settings::values.is_new_3ds =
        LibRetro::FetchVariable("citra_is_new_3ds", "Old 3DS") == "New 3DS";
    Settings::values.swap_screen = LibRetro::FetchVariable("citra_swap_screen", "Top") == "Bottom";
    LibRetro::settings.toggle_swap_screen =
        LibRetro::FetchVariable("citra_swap_screen_mode", "Toggle") == "Toggle";
    Settings::values.use_gdbstub =
        LibRetro::FetchVariable("citra_use_gdbstub", "disabled") == "enabled";
#if defined(USING_GLES)
    Settings::values.use_gles = true;
#else
    Settings::values.use_gles = false;
#endif
    Settings::values.texture_filter =
        GetTextureFilter(LibRetro::FetchVariable("citra_texture_filter", "none"));
    Settings::values.texture_sampling =
        GetTextureSampling(LibRetro::FetchVariable("citra_texture_sampling", "GameControlled"));
    Settings::values.dump_textures =
        LibRetro::FetchVariable("citra_dump_textures", "disabled") == "enabled";
    Settings::values.custom_textures =
        LibRetro::FetchVariable("citra_custom_textures", "disabled") == "enabled";
    Settings::values.filter_mode = false;
    Settings::values.pp_shader_name = "none (builtin)";
    Settings::values.use_disk_shader_cache =
        LibRetro::FetchVariable("citra_use_hw_shader_cache", "enabled") == "enabled";
    Settings::values.use_vsync_new = 1;
    Settings::values.render_3d = Settings::StereoRenderOption::Off;
    Settings::values.factor_3d = 0;
    Settings::values.bg_red = 0;
    Settings::values.bg_green = 0;
    Settings::values.bg_blue = 0;
    LibRetro::settings.mouse_touchscreen =
        LibRetro::FetchVariable("citra_mouse_touchscreen", "enabled") == "enabled";
    LibRetro::settings.touch_touchscreen =
        LibRetro::FetchVariable("citra_touch_touchscreen", "disabled") == "enabled";
    LibRetro::settings.render_touchscreen =
        LibRetro::FetchVariable("citra_render_touchscreen", "disabled") == "enabled";

    // These values are a bit more hard to define, unfortunately.
    auto scaling = LibRetro::FetchVariable("citra_resolution_factor", "1x (Native)");
    auto endOfScale = scaling.find('x'); // All before 'x' in "_x ...", e.g "1x (Native)"
    if (endOfScale == std::string::npos) {
        LOG_ERROR(Frontend, "Failed to parse resolution scale!");
        Settings::values.resolution_factor = 1;
    } else {
        int scale = stoi(scaling.substr(0, endOfScale));
        Settings::values.resolution_factor = scale;
    }

    auto layout = LibRetro::FetchVariable("citra_layout_option", "Default Top-Bottom Screen");

    if (layout == "Default Top-Bottom Screen") {
        Settings::values.layout_option = Settings::LayoutOption::Default;
    } else if (layout == "Single Screen Only") {
        Settings::values.layout_option = Settings::LayoutOption::SingleScreen;
    } else if (layout == "Large Screen, Small Screen") {
        Settings::values.layout_option = Settings::LayoutOption::LargeScreen;
    } else if (layout == "Side by Side") {
        Settings::values.layout_option = Settings::LayoutOption::SideScreen;
    } else {
        LOG_ERROR(Frontend, "Unknown layout type: {}.", layout);
        Settings::values.layout_option = Settings::LayoutOption::Default;
    }

    auto deadzone = LibRetro::FetchVariable("citra_deadzone", "15");
    LibRetro::settings.deadzone = (float)std::stoi(deadzone) / 100;

    auto analog_function =
        LibRetro::FetchVariable("citra_analog_function", "C-Stick and Touchscreen Pointer");

    if (analog_function == "C-Stick and Touchscreen Pointer") {
        LibRetro::settings.analog_function = LibRetro::CStickFunction::Both;
    } else if (analog_function == "C-Stick") {
        LibRetro::settings.analog_function = LibRetro::CStickFunction::CStick;
    } else if (analog_function == "Touchscreen Pointer") {
        LibRetro::settings.analog_function = LibRetro::CStickFunction::Touchscreen;
    } else {
        LOG_ERROR(Frontend, "Unknown right analog function: {}.", analog_function);
        LibRetro::settings.analog_function = LibRetro::CStickFunction::Both;
    }

    auto region = LibRetro::FetchVariable("citra_region_value", "Auto");
    std::map<std::string, int> region_values;
    region_values["Auto"] = -1;
    region_values["Japan"] = 0;
    region_values["USA"] = 1;
    region_values["Europe"] = 2;
    region_values["Australia"] = 3;
    region_values["China"] = 4;
    region_values["Korea"] = 5;
    region_values["Taiwan"] = 6;

    auto result = region_values.find(region);
    if (result == region_values.end()) {
        LOG_ERROR(Frontend, "Invalid region: {}.", region);
        Settings::values.region_value = -1;
    } else {
        Settings::values.region_value = result->second;
    }

    auto language = LibRetro::FetchVariable("citra_language", "English");
    if (language == "English") {
        LibRetro::settings.language_value = Service::CFG::LANGUAGE_EN;
    } else if (language == "Japanese") {
        LibRetro::settings.language_value = Service::CFG::LANGUAGE_JP;
    } else if (language == "French") {
        LibRetro::settings.language_value = Service::CFG::LANGUAGE_FR;
    } else if (language == "Spanish") {
        LibRetro::settings.language_value = Service::CFG::LANGUAGE_ES;
    } else if (language == "German") {
        LibRetro::settings.language_value = Service::CFG::LANGUAGE_DE;
    } else if (language == "Italian") {
        LibRetro::settings.language_value = Service::CFG::LANGUAGE_IT;
    } else if (language == "Dutch") {
        LibRetro::settings.language_value = Service::CFG::LANGUAGE_NL;
    } else if (language == "Portuguese") {
        LibRetro::settings.language_value = Service::CFG::LANGUAGE_PT;
    } else if (language == "Russian") {
        LibRetro::settings.language_value = Service::CFG::LANGUAGE_RU;
    } else if (language == "Korean") {
        LibRetro::settings.language_value = Service::CFG::LANGUAGE_KO;
    } else if (language == "Traditional Chinese") {
        LibRetro::settings.language_value = Service::CFG::LANGUAGE_TW;
    } else if (language == "Simplified Chinese") {
        LibRetro::settings.language_value = Service::CFG::LANGUAGE_ZH;
    } else {
        LOG_ERROR(Frontend, "Invalid language: {}.", language);
        LibRetro::settings.language_value = Service::CFG::LANGUAGE_EN;
    }

    Settings::values.current_input_profile.touch_device = "engine:emu_window";

    // Hardcode buttons to bind to libretro - it is entirely redundant to have
    //  two methods of rebinding controls.
    // Citra: A = RETRO_DEVICE_ID_JOYPAD_A (8)
    Settings::values.current_input_profile.buttons[Settings::NativeButton::Values::A] =
        "button:8,joystick:0,engine:libretro";
    // Citra: B = RETRO_DEVICE_ID_JOYPAD_B (0)
    Settings::values.current_input_profile.buttons[Settings::NativeButton::Values::B] =
        "button:0,joystick:0,engine:libretro";
    // Citra: X = RETRO_DEVICE_ID_JOYPAD_X (9)
    Settings::values.current_input_profile.buttons[Settings::NativeButton::Values::X] =
        "button:9,joystick:0,engine:libretro";
    // Citra: Y = RETRO_DEVICE_ID_JOYPAD_Y (1)
    Settings::values.current_input_profile.buttons[Settings::NativeButton::Values::Y] =
        "button:1,joystick:0,engine:libretro";
    // Citra: UP = RETRO_DEVICE_ID_JOYPAD_UP (4)
    Settings::values.current_input_profile.buttons[Settings::NativeButton::Values::Up] =
        "button:4,joystick:0,engine:libretro";
    // Citra: DOWN = RETRO_DEVICE_ID_JOYPAD_DOWN (5)
    Settings::values.current_input_profile.buttons[Settings::NativeButton::Values::Down] =
        "button:5,joystick:0,engine:libretro";
    // Citra: LEFT = RETRO_DEVICE_ID_JOYPAD_LEFT (6)
    Settings::values.current_input_profile.buttons[Settings::NativeButton::Values::Left] =
        "button:6,joystick:0,engine:libretro";
    // Citra: RIGHT = RETRO_DEVICE_ID_JOYPAD_RIGHT (7)
    Settings::values.current_input_profile.buttons[Settings::NativeButton::Values::Right] =
        "button:7,joystick:0,engine:libretro";
    // Citra: L = RETRO_DEVICE_ID_JOYPAD_L (10)
    Settings::values.current_input_profile.buttons[Settings::NativeButton::Values::L] =
        "button:10,joystick:0,engine:libretro";
    // Citra: R = RETRO_DEVICE_ID_JOYPAD_R (11)
    Settings::values.current_input_profile.buttons[Settings::NativeButton::Values::R] =
        "button:11,joystick:0,engine:libretro";
    // Citra: START = RETRO_DEVICE_ID_JOYPAD_START (3)
    Settings::values.current_input_profile.buttons[Settings::NativeButton::Values::Start] =
        "button:3,joystick:0,engine:libretro";
    // Citra: SELECT = RETRO_DEVICE_ID_JOYPAD_SELECT (2)
    Settings::values.current_input_profile.buttons[Settings::NativeButton::Values::Select] =
        "button:2,joystick:0,engine:libretro";
    // Citra: ZL = RETRO_DEVICE_ID_JOYPAD_L2 (12)
    Settings::values.current_input_profile.buttons[Settings::NativeButton::Values::ZL] =
        "button:12,joystick:0,engine:libretro";
    // Citra: ZR = RETRO_DEVICE_ID_JOYPAD_R2 (13)
    Settings::values.current_input_profile.buttons[Settings::NativeButton::Values::ZR] =
        "button:13,joystick:0,engine:libretro";
    // Citra: HOME = RETRO_DEVICE_ID_JOYPAD_L3 (as per above bindings) (14)
    Settings::values.current_input_profile.buttons[Settings::NativeButton::Values::Home] =
        "button:14,joystick:0,engine:libretro";

    // Circle Pad
    Settings::values.current_input_profile.analogs[0] = "axis:0,joystick:0,engine:libretro";
    // C-Stick
    if (LibRetro::settings.analog_function != LibRetro::CStickFunction::Touchscreen) {
        Settings::values.current_input_profile.analogs[1] = "axis:1,joystick:0,engine:libretro";
    } else {
        Settings::values.current_input_profile.analogs[1] = "";
    }

    // Configure the file storage location
    auto use_libretro_saves = LibRetro::FetchVariable("citra_use_libretro_save_path",
                                                      "LibRetro Default") == "LibRetro Default";

    if (use_libretro_saves) {
        auto target_dir = LibRetro::GetSaveDir();
        if (target_dir.empty()) {
            LOG_INFO(Frontend, "No save dir provided; trying system dir...");
            target_dir = LibRetro::GetSystemDir();
        }

        if (!target_dir.empty()) {
            if (!target_dir.ends_with("/"))
                target_dir += "/";

            target_dir += "Azahar/";

            // Ensure that this new dir exists
            if (!FileUtil::CreateDir(target_dir)) {
                LOG_ERROR(Frontend, "Failed to create \"{}\". Using Azahar's default paths.",
                          target_dir);
            } else {
                FileUtil::SetUserPath(target_dir);
                const auto& target_dir_result = FileUtil::GetUserPath(FileUtil::UserPath::UserDir);
                LOG_INFO(Frontend, "User dir set to \"{}\".", target_dir_result);
            }
        }
    }

    if (!emu_instance->emu_window) {
        emu_instance->emu_window = std::make_unique<EmuWindow_LibRetro>();
    }

    // Update the framebuffer sizing.
    emu_instance->emu_window->UpdateLayout();

    Core::System::GetInstance().ApplySettings();
}

/**
 * libretro callback; Called every game tick.
 */
void retro_run() {
    // Check to see if we actually have any config updates to process.
    if (LibRetro::HasUpdatedConfig()) {
        UpdateSettings();
    }

    // Check if the screen swap button is pressed
    static bool screen_swap_btn_state = false;
    static bool screen_swap_toggled = false;
    bool screen_swap_btn =
        !!LibRetro::CheckInput(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3);
    if (screen_swap_btn != screen_swap_btn_state) {
        if (LibRetro::settings.toggle_swap_screen) {
            if (!screen_swap_btn_state)
                screen_swap_toggled = !screen_swap_toggled;

            if (screen_swap_toggled)
                Settings::values.swap_screen =
                    LibRetro::FetchVariable("citra_swap_screen", "Top") != "Bottom";
            else
                Settings::values.swap_screen =
                    LibRetro::FetchVariable("citra_swap_screen", "Top") == "Bottom";
        } else {
            if (screen_swap_btn)
                Settings::values.swap_screen =
                    LibRetro::FetchVariable("citra_swap_screen", "Top") != "Bottom";
            else
                Settings::values.swap_screen =
                    LibRetro::FetchVariable("citra_swap_screen", "Top") == "Bottom";
        }

        Core::System::GetInstance().ApplySettings();

        // Update the framebuffer sizing.
        emu_instance->emu_window->UpdateLayout();

        screen_swap_btn_state = screen_swap_btn;
    }

#ifdef ENABLE_OPENGL
    if (Settings::values.graphics_api.GetValue() == Settings::GraphicsAPI::OpenGL) {
        // We can't assume that the frontend has been nice and preserved all OpenGL settings. Reset.
        auto last_state = OpenGL::OpenGLState::GetCurState();
        ResetGLState();
        last_state.Apply();
    }
#endif

    while (!emu_instance->emu_window->HasSubmittedFrame()) {
        auto result = Core::System::GetInstance().RunLoop();

        if (result != Core::System::ResultStatus::Success) {
            std::string errorContent = Core::System::GetInstance().GetStatusDetails();
            std::string msg;

            switch (result) {
            case Core::System::ResultStatus::ErrorSystemFiles:
                msg = "Azahar was unable to locate a 3DS system archive: " + errorContent;
                break;
            default:
                msg = "Fatal Error encountered: " + errorContent;
                break;
            }

            LibRetro::DisplayMessage(msg.c_str());
        }
    }
}

static bool do_load_game() {
    const Core::System::ResultStatus load_result{
        Core::System::GetInstance().Load(*emu_instance->emu_window, LibRetro::settings.file_path)};

    switch (load_result) {
    case Core::System::ResultStatus::Success:
        break; // Expected case
    case Core::System::ResultStatus::ErrorGetLoader:
        LibRetro::DisplayMessage("Failed to obtain loader for specified ROM!");
        return false;
    case Core::System::ResultStatus::ErrorLoader:
        LibRetro::DisplayMessage("Failed to load ROM!");
        return false;
    case Core::System::ResultStatus::ErrorLoader_ErrorEncrypted:
        LibRetro::DisplayMessage("The game that you are trying to load must be decrypted before "
                                 "being used with Azahar.");
        return false;
    case Core::System::ResultStatus::ErrorLoader_ErrorInvalidFormat:
        LibRetro::DisplayMessage("Error while loading ROM: The ROM format is not supported.");
        return false;
    case Core::System::ResultStatus::ErrorLoader_ErrorGbaTitle:
        LibRetro::DisplayMessage(
            "Error loading the specified application as it is GBA Virtual Console");
        return false;
    case Core::System::ResultStatus::ErrorNotInitialized:
        LibRetro::DisplayMessage("CPUCore not initialized");
        return false;
    case Core::System::ResultStatus::ErrorSystemMode:
        LibRetro::DisplayMessage("Failed to determine system mode!");
        return false;
    default:
        LibRetro::DisplayMessage("Unknown error");
        return false;
    }

    if (Settings::values.use_disk_shader_cache) {
        Core::System::GetInstance().GPU().Renderer().Rasterizer()->LoadDefaultDiskResources(
            false, nullptr);
    }

    return true;
}

#ifdef ENABLE_OPENGL
static void* load_opengl_func(const char* name) {
    return (void*)emu_instance->hw_render.get_proc_address(name);
}
#endif

static void context_reset() {
    LOG_DEBUG(Frontend, "context_reset");
    switch (Settings::values.graphics_api.GetValue()) {
#ifdef ENABLE_OPENGL
    case Settings::GraphicsAPI::OpenGL:
        // Check to see if the frontend provides us with OpenGL symbols
        if (emu_instance->hw_render.get_proc_address != nullptr) {
            bool loaded = Settings::values.use_gles
                              ? gladLoadGLES2Loader((GLADloadproc)load_opengl_func)
                              : gladLoadGLLoader((GLADloadproc)load_opengl_func);

            if (!loaded) {
                LOG_CRITICAL(Frontend, "Glad failed to load (frontend-provided symbols)!");
                return;
            }
        } else {
            // Else, try to load them on our own
            if (!gladLoadGL()) {
                LOG_CRITICAL(Frontend, "Glad failed to load (internal symbols)!");
                return;
            }
        }
        break;
#endif
#ifdef ENABLE_VULKAN
    case Settings::GraphicsAPI::Vulkan:
        LibRetro::VulkanResetContext();
        break;
#endif
    default:
        // software renderer never gets here
        break;
    }

    emu_instance->emu_window->CreateContext();

    if (!emu_instance->game_loaded)
        emu_instance->game_loaded = do_load_game();
}

static void context_destroy() {
    LOG_DEBUG(Frontend, "context_destroy");
    emu_instance->emu_window->DestroyContext();
}

void retro_reset() {
    LOG_DEBUG(Frontend, "retro_reset");
    Core::System::GetInstance().Shutdown();
    if (Core::System::GetInstance().Load(*emu_instance->emu_window, LibRetro::settings.file_path) !=
        Core::System::ResultStatus::Success) {
        LOG_ERROR(Frontend, "Unable lo load on retro_reset");
    }
}

/**
 * libretro callback; Called when a game is to be loaded.
 */
bool retro_load_game(const struct retro_game_info* info) {
    LOG_INFO(Frontend, "Starting Azahar RetroArch game...");

    UpdateSettings();

    // If using HW rendering, don't actually load the game here. azahar wants
    // the graphics context ready and available before calling System::Load.
    LibRetro::settings.file_path = info->path;

    if (!LibRetro::SetPixelFormat(RETRO_PIXEL_FORMAT_XRGB8888)) {
        LibRetro::DisplayMessage("XRGB8888 is not supported.");
        return false;
    }

    emu_instance->emu_window->UpdateLayout();

    switch (Settings::values.graphics_api.GetValue()) {
    case Settings::GraphicsAPI::OpenGL:
#ifdef ENABLE_OPENGL
        LOG_INFO(Frontend, "Using OpenGL hw renderer");
        LibRetro::SetHWSharedContext();
#if defined(USING_GLES)
        emu_instance->hw_render.context_type = RETRO_HW_CONTEXT_OPENGLES3;
        emu_instance->hw_render.version_major = 3;
        emu_instance->hw_render.version_minor = 2;
#else
        emu_instance->hw_render.context_type = RETRO_HW_CONTEXT_OPENGL_CORE;
        emu_instance->hw_render.version_major = 4;
        emu_instance->hw_render.version_minor = 2;
#endif
        emu_instance->hw_render.context_reset = context_reset;
        emu_instance->hw_render.context_destroy = context_destroy;
        emu_instance->hw_render.cache_context = false;
        emu_instance->hw_render.bottom_left_origin = true;
        if (!LibRetro::SetHWRenderer(&emu_instance->hw_render)) {
            LibRetro::DisplayMessage("Failed to set HW renderer");
            return false;
        }
#endif
        break;
    case Settings::GraphicsAPI::Vulkan:
#ifdef ENABLE_VULKAN
        LOG_INFO(Frontend, "Using Vulkan hw renderer");
        emu_instance->hw_render.context_type = RETRO_HW_CONTEXT_VULKAN;
        emu_instance->hw_render.version_major = VK_MAKE_VERSION(1, 1, 0);
        emu_instance->hw_render.version_minor = 0;
        emu_instance->hw_render.context_reset = context_reset;
        emu_instance->hw_render.context_destroy = context_destroy;
        emu_instance->hw_render.cache_context = true;
        if (!LibRetro::SetHWRenderer(&emu_instance->hw_render)) {
            LibRetro::DisplayMessage("Failed to set HW renderer");
            return false;
        }
#endif
        break;
    case Settings::GraphicsAPI::Software:
        emu_instance->game_loaded = do_load_game();
        return emu_instance->game_loaded;
    }

    return true;
}

void retro_unload_game() {
    LOG_DEBUG(Frontend, "Unloading game...");
    Core::System::GetInstance().Shutdown();
}

unsigned retro_get_region() {
    return RETRO_REGION_NTSC;
}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info* info,
                             size_t num_info) {
    return retro_load_game(info);
}

std::optional<std::vector<u8>> savestate = {};

size_t retro_serialize_size() {
    try {
        savestate = Core::System::GetInstance().SaveStateBuffer();
        return savestate.value().size();
    } catch (const std::exception& e) {
        LOG_ERROR(Core, "Error saving savestate: {}", e.what());
        savestate.reset();
        return 0;
    }
}

bool retro_serialize(void* data, size_t size) {
    if (!savestate.has_value())
        return false;

    memcpy(data, (*savestate).data(), size);
    savestate.reset();

    return true;
}

bool retro_unserialize(const void* data, size_t size) {
    try {
        const std::vector<u8> buffer((const u8*)data, (const u8*)data + size);

        return Core::System::GetInstance().LoadStateBuffer(buffer);
    } catch (const std::exception& e) {
        LOG_ERROR(Core, "Error loading savestate: {}", e.what());
        return false;
    }
}

void* retro_get_memory_data(unsigned id) {
    if (id == RETRO_MEMORY_SYSTEM_RAM)
        return Core::System::GetInstance().Memory().GetFCRAMPointer(
            Core::System::GetInstance().Kernel().memory_regions[0]->base);

    return NULL;
}

size_t retro_get_memory_size(unsigned id) {
    if (id == RETRO_MEMORY_SYSTEM_RAM)
        return Core::System::GetInstance().Kernel().memory_regions[0]->size;

    return 0;
}

void retro_cheat_reset() {}

void retro_cheat_set(unsigned index, bool enabled, const char* code) {}
