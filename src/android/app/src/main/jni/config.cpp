// Copyright 2014-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#include <iomanip>
#include <memory>
#include <ranges>
#include <sstream>
#include <unordered_map>
#include <INIReader.h>
#include "common/file_util.h"
#include "common/logging/backend.h"
#include "common/logging/log.h"
#include "common/param_package.h"
#include "common/setting_keys.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/hle/service/cfg/cfg.h"
#include "core/hle/service/service.h"
#include "input_common/main.h"
#include "input_common/udp/client.h"
#include "jni/camera/ndk_camera.h"
#include "jni/config.h"
#include "jni/default_ini.h"
#include "jni/input_manager.h"

Config::Config(u64 title_id) {
    // TODO: Don't hardcode the path; let the frontend decide where to put the config files.
    android_config_loc = FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir) + "config.ini";
    std::string ini_buffer;
    FileUtil::ReadFileToString(true, android_config_loc, ini_buffer);
    if (!ini_buffer.empty()) {
        android_config = std::make_unique<INIReader>(ini_buffer.c_str(), ini_buffer.size());
    }
    LoadPerGameConfig(title_id);
}

Config::~Config() = default;

bool Config::LoadINI(const std::string& default_contents, bool retry) {
    const std::string& location = this->android_config_loc;
    if (android_config == nullptr || android_config->ParseError() < 0) {
        if (retry) {
            LOG_WARNING(Config, "Failed to load {}. Creating file from defaults...", location);
            FileUtil::CreateFullPath(location);
            FileUtil::WriteStringToFile(true, location, default_contents);
            std::string ini_buffer;
            FileUtil::ReadFileToString(true, location, ini_buffer);
            android_config =
                std::make_unique<INIReader>(ini_buffer.c_str(), ini_buffer.size()); // Reopen file

            return LoadINI(default_contents, false);
        }
        LOG_ERROR(Config, "Failed.");
        return false;
    }
    LOG_INFO(Config, "Successfully loaded {}", location);
    return true;
}

static const std::array<int, Settings::NativeButton::NumButtons> default_buttons = {
    InputManager::N3DS_BUTTON_A,     InputManager::N3DS_BUTTON_B,
    InputManager::N3DS_BUTTON_X,     InputManager::N3DS_BUTTON_Y,
    InputManager::N3DS_DPAD_UP,      InputManager::N3DS_DPAD_DOWN,
    InputManager::N3DS_DPAD_LEFT,    InputManager::N3DS_DPAD_RIGHT,
    InputManager::N3DS_TRIGGER_L,    InputManager::N3DS_TRIGGER_R,
    InputManager::N3DS_BUTTON_START, InputManager::N3DS_BUTTON_SELECT,
    InputManager::N3DS_BUTTON_DEBUG, InputManager::N3DS_BUTTON_GPIO14,
    InputManager::N3DS_BUTTON_ZL,    InputManager::N3DS_BUTTON_ZR,
    InputManager::N3DS_BUTTON_HOME,
};

static const std::array<int, Settings::NativeAnalog::NumAnalogs> default_analogs{{
    InputManager::N3DS_CIRCLEPAD,
    InputManager::N3DS_STICK_C,
}};

void Config::AddDefaultSettingValue(const std::string& key, const std::string& value) {
    default_android_values.insert_or_assign(key, value);
}

template <typename Type, bool ranged>
Type Config::GetSetting(const std::string& group, Settings::Setting<Type, ranged>& setting) {
    std::string key = setting.GetLabel();

    if constexpr (std::is_enum_v<Type>) {
        return static_cast<Type>(
            GetSettingFromKey<int>(group, key, static_cast<int>(setting.GetDefault())));
    } else {
        return GetSettingFromKey<Type>(group, key, setting.GetDefault());
    }
}

template <typename Type, bool ranged>
void Config::ReadSetting(const std::string& group, Settings::Setting<Type, ranged>& setting) {
    setting = GetSetting(group, setting);
}

/**
 * Use this to get a setting value using a key rather than a Setting object. Specify the type.
 * Note: default_value is only used as a fallback if there is no matching Kotlin setting that maps
 * the default value. Kotlin is the main source of truth for all exposed android settings.
 */

template <typename Type>
Type Config::GetSettingFromKey(const std::string& group, const std::string& key,
                               std::optional<Type> default_value) {

    const std::string default_str =
        default_android_values.contains(key) ? default_android_values.at(key) : "";

    const bool has_per_game = per_game_config && per_game_config->HasValue(group, key);
    const bool has_android = android_config && android_config->HasValue(group, key);

    if constexpr (std::is_same_v<Type, bool>) {
        bool value = (default_value && default_str == "")
                         ? *default_value
                         : (default_str == "1" || default_str == "true");
        if (has_per_game) {
            value = per_game_config->GetBoolean(group, key, value);
        } else if (has_android) {
            value = android_config->GetBoolean(group, key, value);
        }
        return value;
    } else if constexpr (std::is_floating_point_v<Type>) {
        double value = (default_value && default_str == "")
                           ? static_cast<double>(*default_value)
                           : (default_str.empty() ? 0.0 : std::stod(default_str));
        if (has_per_game) {
            value = per_game_config->GetReal(group, key, value);
        } else if (has_android) {
            value = android_config->GetReal(group, key, value);
        }
        return static_cast<Type>(value);
    } else if constexpr (std::is_integral_v<Type>) {
        long value = (default_value && default_str == "")
                         ? static_cast<long>(*default_value)
                         : (default_str.empty() ? 0L : std::stol(default_str));
        if (has_per_game) {
            value = per_game_config->GetInteger(group, key, value);
        } else if (has_android) {
            value = android_config->GetInteger(group, key, value);
        }
        return static_cast<Type>(value);
    } else if constexpr (std::is_same_v<Type, std::string>) {
        std::string value = (default_value && default_str == "") ? *default_value : default_str;
        if (has_per_game) {
            value = per_game_config->Get(group, key, value);
        } else if (has_android) {
            value = android_config->Get(group, key, value);
        }
        return value;
    } else {
        static_assert(!sizeof(Type), "Unsupported type for GetSettingFromKey");
    }
}

void Config::ReadValues() {
    // Controls
    for (int i = 0; i < Settings::NativeButton::NumButtons; ++i) {
        std::string default_param = InputManager::GenerateButtonParamPackage(default_buttons[i]);
        Settings::values.current_input_profile.buttons[i] = GetSettingFromKey<std::string>(
            "Controls", Settings::NativeButton::mapping[i], default_param);
        if (Settings::values.current_input_profile.buttons[i].empty())
            Settings::values.current_input_profile.buttons[i] = default_param;
    }

    for (int i = 0; i < Settings::NativeAnalog::NumAnalogs; ++i) {
        std::string default_param = InputManager::GenerateAnalogParamPackage(default_analogs[i]);
        Settings::values.current_input_profile.analogs[i] = GetSettingFromKey<std::string>(
            "Controls", Settings::NativeAnalog::mapping[i], default_param);
        if (Settings::values.current_input_profile.analogs[i].empty())
            Settings::values.current_input_profile.analogs[i] = default_param;
    }

    Settings::values.current_input_profile.motion_device = GetSettingFromKey<std::string>(
        "Controls", "motion_device",
        "engine:motion_emu,update_period:100,sensitivity:0.01,tilt_clamp:90.0");
    Settings::values.current_input_profile.touch_device =
        GetSettingFromKey<std::string>("Controls", "touch_device", "engine:emu_window");
    Settings::values.current_input_profile.udp_input_address = GetSettingFromKey<std::string>(
        "Controls", "udp_input_address", InputCommon::CemuhookUDP::DEFAULT_ADDR);
    Settings::values.current_input_profile.udp_input_port = GetSettingFromKey<u16>(
        "Controls", "udp_input_port", InputCommon::CemuhookUDP::DEFAULT_PORT);

    ReadSetting("Controls", Settings::values.use_artic_base_controller);

    // Core
    ReadSetting("Core", Settings::values.use_cpu_jit);
    ReadSetting("Core", Settings::values.use_fastinterp);
    ReadSetting("Core", Settings::values.cpu_clock_percentage);

    // Renderer
    ReadSetting("Renderer", Settings::values.use_gles);
    ReadSetting("Renderer", Settings::values.shaders_accurate_mul);
    ReadSetting("Renderer", Settings::values.graphics_api);
    ReadSetting("Renderer", Settings::values.async_presentation);
    ReadSetting("Renderer", Settings::values.async_shader_compilation);
    ReadSetting("Renderer", Settings::values.spirv_shader_gen);
    ReadSetting("Renderer", Settings::values.disable_spirv_optimizer);
    ReadSetting("Renderer", Settings::values.use_hw_shader);
    ReadSetting("Renderer", Settings::values.use_shader_jit);
    ReadSetting("Renderer", Settings::values.resolution_factor);
    ReadSetting("Renderer", Settings::values.use_disk_shader_cache);
    ReadSetting("Renderer", Settings::values.use_vsync);
    ReadSetting("Renderer", Settings::values.use_skip_duplicate_frames);
    ReadSetting("Renderer", Settings::values.texture_filter);
    ReadSetting("Renderer", Settings::values.texture_sampling);
    ReadSetting("Renderer", Settings::values.turbo_limit);
    bool use_frame_limit =
        GetSettingFromKey<bool>("Renderer", Settings::Keys::use_frame_limit, true);
    if (use_frame_limit) {
        ReadSetting("Renderer", Settings::values.frame_limit);
    } else {
        Settings::values.frame_limit = 0;
    }

    ReadSetting("Renderer", Settings::values.render_3d);
    ReadSetting("Renderer", Settings::values.factor_3d);
    std::string default_shader = "None (builtin)";
    if (Settings::values.render_3d.GetValue() == Settings::StereoRenderOption::Anaglyph)
        default_shader = "Dubois (builtin)";
    else if (Settings::values.render_3d.GetValue() == Settings::StereoRenderOption::Interlaced)
        default_shader = "Horizontal (builtin)";
    Settings::values.pp_shader_name =
        GetSettingFromKey<std::string>("Renderer", "pp_shader_name", default_shader);
    ReadSetting("Renderer", Settings::values.filter_mode);
    ReadSetting("Renderer", Settings::values.use_integer_scaling);

    ReadSetting("Renderer", Settings::values.bg_red);
    ReadSetting("Renderer", Settings::values.bg_green);
    ReadSetting("Renderer", Settings::values.bg_blue);
    ReadSetting("Renderer", Settings::values.custom_second_layer_opacity);
    ReadSetting("Renderer", Settings::values.delay_game_render_thread_us);
    ReadSetting("Renderer", Settings::values.simulate_3ds_gpu_timings);
    ReadSetting("Renderer", Settings::values.disable_right_eye_render);
    ReadSetting("Renderer", Settings::values.swap_eyes_3d);
    ReadSetting("Renderer", Settings::values.render_3d_which_display);
    // Layout

    ReadSetting("Layout", Settings::values.layout_option);
    ReadSetting("Layout", Settings::values.screen_gap);
    ReadSetting("Layout", Settings::values.small_screen_position);
    ReadSetting("Layout", Settings::values.large_screen_proportion);
    ReadSetting("Layout", Settings::values.custom_top_x);
    ReadSetting("Layout", Settings::values.custom_top_y);
    ReadSetting("Layout", Settings::values.custom_top_width);
    ReadSetting("Layout", Settings::values.custom_top_height);
    ReadSetting("Layout", Settings::values.custom_bottom_x);
    ReadSetting("Layout", Settings::values.custom_bottom_y);
    ReadSetting("Layout", Settings::values.custom_bottom_width);
    ReadSetting("Layout", Settings::values.aspect_ratio);
    ReadSetting("Layout", Settings::values.custom_bottom_height);
    ReadSetting("Layout", Settings::values.cardboard_screen_size);
    ReadSetting("Layout", Settings::values.cardboard_x_shift);
    ReadSetting("Layout", Settings::values.cardboard_y_shift);
    ReadSetting("Layout", Settings::values.upright_screen);
    ReadSetting("Layout", Settings::values.portrait_layout_option);
    ReadSetting("Layout", Settings::values.secondary_display_layout);
    ReadSetting("Layout", Settings::values.custom_portrait_top_x);
    ReadSetting("Layout", Settings::values.custom_portrait_top_y);
    ReadSetting("Layout", Settings::values.custom_portrait_top_width);
    ReadSetting("Layout", Settings::values.custom_portrait_top_height);
    ReadSetting("Layout", Settings::values.custom_portrait_bottom_x);
    ReadSetting("Layout", Settings::values.custom_portrait_bottom_y);
    ReadSetting("Layout", Settings::values.custom_portrait_bottom_width);
    ReadSetting("Layout", Settings::values.custom_portrait_bottom_height);

    // Storage
    ReadSetting("Storage", Settings::values.compress_cia_installs);
    ReadSetting("Storage", Settings::values.async_fs_operations);

    // Utility
    ReadSetting("Utility", Settings::values.dump_textures);
    ReadSetting("Utility", Settings::values.custom_textures);
    ReadSetting("Utility", Settings::values.preload_textures);
    ReadSetting("Utility", Settings::values.async_custom_loading);

    // Audio
    ReadSetting("Audio", Settings::values.audio_emulation);
    ReadSetting("Audio", Settings::values.enable_audio_stretching);
    ReadSetting("Audio", Settings::values.enable_realtime_audio);
    ReadSetting("Audio", Settings::values.simulate_headphones_plugged);
    ReadSetting("Audio", Settings::values.volume);
    ReadSetting("Audio", Settings::values.output_type);
    ReadSetting("Audio", Settings::values.output_device);
    ReadSetting("Audio", Settings::values.input_type);
    ReadSetting("Audio", Settings::values.input_device);

    // Data Storage
    ReadSetting("Data Storage", Settings::values.use_virtual_sd);

    // System
    ReadSetting("System", Settings::values.is_new_3ds);
    ReadSetting("System", Settings::values.lle_applets);
    ReadSetting("System", Settings::values.enable_required_online_lle_modules);
    ReadSetting("System", Settings::values.region_value);
    ReadSetting("System", Settings::values.init_clock);
    {
        std::string time =
            GetSettingFromKey<std::string>("System", Settings::Keys::init_time, "946681277");
        try {
            Settings::values.init_time = std::stoll(time);
        } catch (...) {
        }
    }
    ReadSetting("System", Settings::values.init_ticks_type);
    ReadSetting("System", Settings::values.init_ticks_override);
    ReadSetting("System", Settings::values.plugin_loader_enabled);
    ReadSetting("System", Settings::values.allow_plugin_loader);
    ReadSetting("System", Settings::values.steps_per_hour);
    ReadSetting("System", Settings::values.apply_region_free_patch);

    // Camera
    using namespace Service::CAM;
    Settings::values.camera_name[OuterRightCamera] =
        GetSettingFromKey<std::string>("Camera", Settings::Keys::camera_outer_right_name, "ndk");
    Settings::values.camera_config[OuterRightCamera] =
        GetSettingFromKey<std::string>("Camera", Settings::Keys::camera_outer_right_config,
                                       std::string{Camera::NDK::BackCameraPlaceholder});
    Settings::values.camera_flip[OuterRightCamera] =
        GetSettingFromKey<int>("Camera", Settings::Keys::camera_outer_right_flip, 0);
    Settings::values.camera_name[InnerCamera] =
        GetSettingFromKey<std::string>("Camera", Settings::Keys::camera_inner_name, "ndk");
    Settings::values.camera_config[InnerCamera] =
        GetSettingFromKey<std::string>("Camera", Settings::Keys::camera_inner_config,
                                       std::string{Camera::NDK::FrontCameraPlaceholder});
    Settings::values.camera_flip[InnerCamera] =
        GetSettingFromKey<int>("Camera", Settings::Keys::camera_inner_flip, 0);
    Settings::values.camera_name[OuterLeftCamera] =
        GetSettingFromKey<std::string>("Camera", Settings::Keys::camera_outer_left_name, "ndk");
    Settings::values.camera_config[OuterLeftCamera] =
        GetSettingFromKey<std::string>("Camera", Settings::Keys::camera_outer_left_config,
                                       std::string{Camera::NDK::BackCameraPlaceholder});
    Settings::values.camera_flip[OuterLeftCamera] =
        GetSettingFromKey<int>("Camera", Settings::Keys::camera_outer_left_flip, 0);

    // Miscellaneous
    ReadSetting("Miscellaneous", Settings::values.log_filter);
    ReadSetting("Miscellaneous", Settings::values.log_regex_filter);

    // Apply the log_filter setting as the logger has already been initialized
    // and doesn't pick up the filter on its own.
    Common::Log::Filter filter;
    filter.ParseFilterString(Settings::values.log_filter.GetValue());
    Common::Log::SetGlobalFilter(filter);
    Common::Log::SetRegexFilter(Settings::values.log_regex_filter.GetValue());

    // Debugging
    Settings::values.record_frame_times =
        GetSettingFromKey<bool>("Debugging", Settings::Keys::record_frame_times, false);
    ReadSetting("Debugging", Settings::values.renderer_debug);
    ReadSetting("Debugging", Settings::values.pica_debugging);
    ReadSetting("Debugging", Settings::values.use_gdbstub);
    ReadSetting("Debugging", Settings::values.gdbstub_port);
    ReadSetting("Debugging", Settings::values.instant_debug_log);
    ReadSetting("Debugging", Settings::values.enable_rpc_server);
    ReadSetting("Debugging", Settings::values.toggle_unique_data_console_type);
    ReadSetting("Debugging", Settings::values.enable_exception_handler);

    for (const auto& service_module : Service::service_module_map) {
        bool use_lle = GetSettingFromKey<bool>("Debugging", "LLE\\" + service_module.name, false);
        Settings::values.lle_modules.emplace(service_module.name, use_lle);
    }

    // Web Service
    ReadSetting("WebService", Settings::values.web_api_url);
    ReadSetting("WebService", Settings::values.network_token);
}

void Config::Reload() {
    for (auto key = Settings::Keys::keys_array.begin(); key != Settings::Keys::keys_array.end();
         ++key) {
        const auto key_declaration_string = std::string(*key) + " =";
        if ((std::ranges::find(DefaultINI::android_config_omitted_keys, std::string_view(*key)) ==
             std::end(DefaultINI::android_config_omitted_keys)) &&
            (std::string_view(DefaultINI::android_config_default_file_content)
                 .find(key_declaration_string) == std::string_view::npos)) {
            ASSERT_MSG(false,
                       "Validation of default config content (jni/default_ini.h) failed: Missing "
                       "declaration for key '{}'",
                       *key);
        }
    }
    LoadINI(DefaultINI::android_config_default_file_content);
    ReadValues();
}

void Config::LoadPerGameConfig(u64 title_id) {
    // Determine file name
    std::string name;
    if (title_id != 0) {
        std::ostringstream ss;
        ss << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << title_id;
        name = ss.str();
    }
    if (name.empty()) {
        per_game_config.reset();
        per_game_config_loc.clear();
        ReadValues();
        return;
    }

    const auto base = FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir);
    per_game_config_loc = base + "custom/" + name + ".ini";

    std::string ini_buffer;
    FileUtil::ReadFileToString(true, per_game_config_loc, ini_buffer);
    if (!ini_buffer.empty()) {
        per_game_config = std::make_unique<INIReader>(ini_buffer.c_str(), ini_buffer.size());
        if (per_game_config->ParseError() < 0) {
            per_game_config.reset();
        }
    } else {
        per_game_config.reset();
    }

    // Re-apply values so that per-game overrides (if any) take effect immediately.
    ReadValues();
}