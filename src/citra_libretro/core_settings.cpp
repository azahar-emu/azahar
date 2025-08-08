// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "citra_libretro/core_settings.h"
#include "citra_libretro/environment.h"

#include "common/file_util.h"
#include "common/settings.h"
#include "core/hle/service/cfg/cfg.h"

namespace LibRetro {

CoreSettings settings = {};

namespace config {

static constexpr const char* enabled = "enabled";
static constexpr const char* disabled = "disabled";

namespace category {
static constexpr const char* cpu = "cpu";
static constexpr const char* system = "system";
static constexpr const char* audio = "audio";
static constexpr const char* graphics = "graphics";
static constexpr const char* layout = "layout";
static constexpr const char* storage = "storage";
static constexpr const char* input = "input";
} // namespace category

namespace cpu {
static constexpr const char* use_cpu_jit = "citra_use_cpu_jit";
static constexpr const char* cpu_clock_percentage = "citra_cpu_scale";
} // namespace cpu

namespace system {
static constexpr const char* is_new_3ds = "citra_is_new_3ds";
static constexpr const char* region = "citra_region_value";
static constexpr const char* language = "citra_language";
} // namespace system

namespace audio {
static constexpr const char* audio_emulation = "citra_audio_emulation";
static constexpr const char* input_type = "citra_input_type";
} // namespace audio

namespace graphics {
static constexpr const char* graphics_api = "citra_graphics_api";
static constexpr const char* use_hw_shader = "citra_use_hw_shaders";
static constexpr const char* use_shader_jit = "citra_use_shader_jit";
static constexpr const char* shaders_accurate_mul = "citra_use_acc_mul";
static constexpr const char* use_disk_shader_cache = "citra_use_hw_shader_cache";
static constexpr const char* resolution_factor = "citra_resolution_factor";
static constexpr const char* texture_filter = "citra_texture_filter";
static constexpr const char* texture_sampling = "citra_texture_sampling";
static constexpr const char* custom_textures = "citra_custom_textures";
static constexpr const char* dump_textures = "citra_dump_textures";
} // namespace graphics

namespace layout {
static constexpr const char* layout_option = "citra_layout_option";
static constexpr const char* swap_screen = "citra_swap_screen";
static constexpr const char* toggle_swap_screen = "citra_swap_screen_mode";
} // namespace layout

namespace storage {
static constexpr const char* use_virtual_sd = "citra_use_virtual_sd";
static constexpr const char* use_libretro_save_path = "citra_use_libretro_save_path";
} // namespace storage

namespace input {
static constexpr const char* analog_function = "citra_analog_function";
static constexpr const char* deadzone = "citra_deadzone";
static constexpr const char* mouse_touchscreen = "citra_mouse_touchscreen";
static constexpr const char* touch_touchscreen = "citra_touch_touchscreen";
static constexpr const char* render_touchscreen = "citra_render_touchscreen";
} // namespace input

} // namespace config

// clang-format off
static constexpr retro_core_option_v2_category option_categories[] = {
    {
        config::category::cpu,
        "CPU",
        "Settings related to CPU emulation performance and accuracy."
    },
    {
        config::category::system,
        "System",
        "Nintendo 3DS system configuration and region settings."
    },
    {
        config::category::audio,
        "Audio",
        "Audio emulation and microphone settings."
    },
    {
        config::category::graphics,
        "Graphics",
        "Graphics API, rendering, and visual enhancement settings."
    },
    {
        config::category::layout,
        "Layout",
        "Screen layout and display positioning options."
    },
    {
        config::category::storage,
        "Storage",
        "Save data and virtual SD card settings."
    },
    {
        config::category::input,
        "Input",
        "Controller and touchscreen input configuration."
    },
    { nullptr, nullptr, nullptr }
};

// ============================================================================
// Option Definitions
// ============================================================================

static constexpr retro_core_option_v2_definition option_definitions[] = {
    // CPU Category
    {
        config::cpu::use_cpu_jit,
        "Enable CPU JIT",
        "CPU JIT",
        "Enable Just-In-Time compilation for ARM CPU emulation. "
        "Significantly improves performance but may reduce accuracy. "
        "Restart required.",
        nullptr,
        config::category::cpu,
        {
            { config::enabled, "Enabled" },
            { config::disabled, "Disabled" },
            { nullptr, nullptr }
        },
        config::enabled
    },
    {
        config::cpu::cpu_clock_percentage,
        "CPU Clock Speed",
        "CPU Clock Speed",
        "Adjust the emulated 3DS CPU clock speed as a percentage of normal speed. "
        "Higher values may improve performance in some games but can cause issues. "
        "Lower values can help with games that run too fast.",
        nullptr,
        config::category::cpu,
        {
            {  "25",  "25%" }, {  "50",  "50%" }, {  "75",  "75%" }, { "100", "100% (Default)" },
            { "125", "125%" }, { "150", "150%" }, { "175", "175%" }, { "200", "200%" },
            { "225", "225%" }, { "250", "250%" }, { "275", "275%" }, { "300", "300%" },
            { "325", "325%" }, { "350", "350%" }, { "375", "375%" }, { "400", "400%" },
            { nullptr, nullptr }
        },
        "100"
    },

    // System Category
    {
        config::system::is_new_3ds,
        "3DS System Model",
        "System Model",
        "Select whether to emulate the original 3DS or New 3DS. "
        "New 3DS has additional CPU power and memory, required for some games. "
        "Restart required.",
        nullptr,
        config::category::system,
        {
            { "Old 3DS", "Original 3DS" },
            { "New 3DS", "New 3DS" },
            { nullptr, nullptr }
        },
        "Old 3DS"
    },
    {
        config::system::region,
        "3DS System Region",
        "System Region",
        "Set the 3DS system region. Auto-select will choose based on the game. "
        "Some games are region-locked and require matching regions.",
        nullptr,
        config::category::system,
        {
            { "Auto", "Auto" },
            { "Japan", "Japan" },
            { "USA", "USA" },
            { "Europe", "Europe" },
            { "Australia", "Australia" },
            { "China", "China" },
            { "Korea", "Korea" },
            { "Taiwan", "Taiwan" },
            { nullptr, nullptr }
        },
        "Auto"
    },
    {
        config::system::language,
        "3DS System Language",
        "System Language",
        "Set the system language for the emulated 3DS. "
        "This affects in-game text language when supported.",
        nullptr,
        config::category::system,
        {
            { "English", "English" },
            { "Japanese", "Japanese" },
            { "French", "French" },
            { "Spanish", "Spanish" },
            { "German", "German" },
            { "Italian", "Italian" },
            { "Dutch", "Dutch" },
            { "Portuguese", "Portuguese" },
            { "Russian", "Russian" },
            { "Korean", "Korean" },
            { "Traditional Chinese", "Traditional Chinese" },
            { "Simplified Chinese", "Simplified Chinese" },
            { nullptr, nullptr }
        },
        "english"
    },

    // Audio Category
    {
        config::audio::audio_emulation,
        "Audio Emulation",
        "Audio Emulation",
        "Select audio emulation method. HLE is faster, LLE is more accurate.",
        nullptr,
        config::category::audio,
        {
            { "hle", "HLE (Fast)" },
            { "lle", "LLE (Accurate)" },
            { "lle_multithread", "LLE Multithreaded" },
            { nullptr, nullptr }
        },
        "hle"
    },
    {
        config::audio::input_type,
        "Microphone Input Type",
        "Microphone Input",
        "Select how microphone input is handled for games that support it.",
        nullptr,
        config::category::audio,
        {
            { "auto", "Auto" },
            { "none", "None" },
            { "static_noise", "Static Noise" },
            { "frontend", "Frontend" },
            { nullptr, nullptr }
        },
        "auto"
    },

    // Graphics Category
    {
        config::graphics::graphics_api,
        "Graphics API",
        "Graphics API",
        "Select the graphics rendering API. Auto will choose the best available option. "
        "Restart required.",
        nullptr,
        config::category::graphics,
        {
            { "Auto", "Auto" },
#ifdef ENABLE_VULKAN
            { "Vulkan", "Vulkan" },
#endif
#ifdef ENABLE_OPENGL
            { "OpenGL", "OpenGL" },
#endif
            { "Software", "Software" },
            { nullptr, nullptr }
        },
        "auto"
    },
    {
        config::graphics::use_hw_shader,
        "Enable Hardware Shaders",
        "Hardware Shaders",
        "Use GPU hardware to accelerate shader processing. "
        "Significantly improves performance but may reduce accuracy.",
        nullptr,
        config::category::graphics,
        {
            { config::enabled, "Enabled" },
            { config::disabled, "Disabled" },
            { nullptr, nullptr }
        },
        config::enabled
    },
    {
        config::graphics::use_shader_jit,
        "Enable Shader JIT",
        "Shader JIT",
        "Use Just-In-Time compilation for shaders. "
        "Improves performance but may cause graphical issues in some games.",
        nullptr,
        config::category::graphics,
        {
            { config::enabled, "Enabled" },
            { config::disabled, "Disabled" },
            { nullptr, nullptr }
        },
        config::enabled
    },
    {
        config::graphics::shaders_accurate_mul,
        "Accurate Shader Multiplication",
        "Accurate Multiplication",
        "Use accurate multiplication in shaders. "
        "More accurate but can reduce performance. Only works with hardware shaders.",
        nullptr,
        config::category::graphics,
        {
            { config::enabled, "Enabled" },
            { config::disabled, "Disabled" },
            { nullptr, nullptr }
        },
        config::enabled
    },
    {
        config::graphics::use_disk_shader_cache,
        "Hardware Shader Cache",
        "Shader Cache",
        "Save compiled shaders to disk to reduce loading times on subsequent runs.",
        nullptr,
        config::category::graphics,
        {
            { config::enabled, "Enabled" },
            { config::disabled, "Disabled" },
            { nullptr, nullptr }
        },
        config::enabled
    },
    {
        config::graphics::resolution_factor,
        "Internal Resolution",
        "Internal Resolution",
        "Render the 3DS screens at a higher resolution. "
        "Higher values improve visual quality but significantly impact performance.",
        nullptr,
        config::category::graphics,
        {
            { "1", "1x (Native 400x240)" },
            { "2", "2x (800x480)" },
            { "3", "3x (1200x720)" },
            { "4", "4x (1600x960)" },
            { "5", "5x (2000x1200)" },
            { "6", "6x (2400x1440)" },
            { "7", "7x (2800x1680)" },
            { "8", "8x (3200x1920)" },
            { "9", "9x (3600x2160)" },
            { "10", "10x (4000x2400)" },
            { nullptr, nullptr }
        },
        "1"
    },
    {
        config::graphics::texture_filter,
        "Texture Filter",
        "Texture Filter",
        "Apply texture filtering to enhance visual quality. "
        "Some filters may significantly impact performance.",
        nullptr,
        config::category::graphics,
        {
            { "none", "None" },
            { "Anime4K Ultrafast", "Anime4K Ultrafast" },
            { "Bicubic", "Bicubic" },
            { "ScaleForce", "ScaleForce" },
            { "xBRZ", "xBRZ" },
            { "MMPX", "MMPX" },
            { nullptr, nullptr }
        },
        "none"
    },
    {
        config::graphics::texture_sampling,
        "Texture Sampling",
        "Texture Sampling",
        "Control how textures are sampled and filtered.",
        nullptr,
        config::category::graphics,
        {
            { "GameControlled", "Game Controlled" },
            { "NearestNeighbor", "Nearest Neighbor" },
            { "Linear", "Linear" },
            { nullptr, nullptr }
        },
        "GameControlled"
    },
    {
        config::graphics::custom_textures,
        "Custom Textures",
        "Custom Textures",
        "Enable loading of custom texture packs to replace original game textures.",
        nullptr,
        config::category::graphics,
        {
            { config::enabled, "Enabled" },
            { config::disabled, "Disabled" },
            { nullptr, nullptr }
        },
        config::disabled
    },
    {
        config::graphics::dump_textures,
        "Dump Game Textures",
        "Dump Textures",
        "Save original game textures to disk for creating custom texture packs. "
        "May impact performance.",
        nullptr,
        config::category::graphics,
        {
            { config::enabled, "Enabled" },
            { config::disabled, "Disabled" },
            { nullptr, nullptr }
        },
        config::disabled
    },

    // Layout Category
    {
        config::layout::layout_option,
        "Screen Layout",
        "Screen Layout",
        "Choose how the 3DS screens are arranged in the display.",
        nullptr,
        config::category::layout,
        {
            { "default", "Default Top-Bottom" },
            { "single_screen", "Single Screen Only" },
            { "large_screen", "Large Screen, Small Screen" },
            { "side_by_side", "Side by Side" },
            { nullptr, nullptr }
        },
        "default"
    },
    {
        config::layout::swap_screen,
        "Prominent 3DS Screen",
        "Prominent Screen",
        "Choose which screen is displayed prominently in single screen or large screen layouts.",
        nullptr,
        config::category::layout,
        {
            { "Top", "Top Screen" },
            { "Bottom", "Bottom Screen" },
            { nullptr, nullptr }
        },
        "Top"
    },
    {
        config::layout::toggle_swap_screen,
        "Screen Swap Mode",
        "Swap Mode",
        "How screen swapping behaves when using the screen swap hotkey.",
        nullptr,
        config::category::layout,
        {
            { "Toggle", "Toggle" },
            { "Hold", "Hold" },
            { nullptr, nullptr }
        },
        "Toggle"
    },

    // Storage Category
    {
        config::storage::use_virtual_sd,
        "Enable Virtual SD Card",
        "Virtual SD Card",
        "Enable virtual SD card support for homebrew and some commercial games.",
        nullptr,
        config::category::storage,
        {
            { config::enabled, "Enabled" },
            { config::disabled, "Disabled" },
            { nullptr, nullptr }
        },
        config::enabled
    },
    {
        config::storage::use_libretro_save_path,
        "Save Data Location",
        "Save Location",
        "Choose where save data and system files are stored.",
        nullptr,
        config::category::storage,
        {
            { "LibRetro Default", "LibRetro Default" },
            { "Azahar Default", "Azahar Default" },
            { nullptr, nullptr }
        },
        "LibRetro Default"
    },

    // Input Category
    {
        config::input::analog_function,
        "Right Analog Function",
        "Right Analog Function",
        "Configure what the right analog stick controls.",
        nullptr,
        config::category::input,
        {
            { "c_stick_and_touchscreen", "C-Stick and Touchscreen Pointer" },
            { "touchscreen_pointer", "Touchscreen Pointer" },
            { "c_stick", "C-Stick" },
            { nullptr, nullptr }
        },
        "c_stick_and_touchscreen"
    },
    {
        config::input::deadzone,
        "Analog Deadzone",
        "Analog Deadzone",
        "Set the deadzone percentage for analog input to reduce drift.",
        nullptr,
        config::category::input,
        {
            {  "0",  "0%" }, {  "5",  "5%" }, { "10", "10%" }, { "15", "15%" },
            { "20", "20%" }, { "25", "25%" }, { "30", "30%" }, { "35", "35%" },
            { nullptr, nullptr }
        },
        "15"
    },
    {
        config::input::mouse_touchscreen,
        "Mouse Touchscreen Support",
        "Mouse Touchscreen",
        "Enable mouse input for touchscreen interactions.",
        nullptr,
        config::category::input,
        {
            { config::enabled, "Enabled" },
            { config::disabled, "Disabled" },
            { nullptr, nullptr }
        },
        config::enabled
    },
    {
        config::input::touch_touchscreen,
        "Touch Device Support",
        "Touch Support",
        "Enable touch device input for touchscreen interactions.",
        nullptr,
        config::category::input,
        {
            { config::enabled, "Enabled" },
            { config::disabled, "Disabled" },
            { nullptr, nullptr }
        },
        config::disabled
    },
    {
        config::input::render_touchscreen,
        "Show Touch Interactions",
        "Show Touch",
        "Visually indicate touchscreen interactions on screen.",
        nullptr,
        config::category::input,
        {
            { config::enabled, "Enabled" },
            { config::disabled, "Disabled" },
            { nullptr, nullptr }
        },
        config::disabled
    },

    // Terminator
    { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, { { nullptr, nullptr } }, nullptr }
};
// clang-format on

static const retro_core_options_v2 options_v2 = {
    const_cast<retro_core_option_v2_category*>(option_categories),
    const_cast<retro_core_option_v2_definition*>(option_definitions)};

void RegisterCoreOptions(void) {
    // Try v2 first, then fallback to v1 and v0 if needed
    unsigned version = 0;
    if (!LibRetro::GetCoreOptionsVersion(&version)) {
        version = 0;
    }

    LOG_INFO(Frontend, "Frontend reports core options version: {}", version);

    if (version >= 2) {
        if (LibRetro::SetCoreOptionsV2(&options_v2)) {
            LOG_INFO(Frontend, "V2 core options set successfully");
            return;
        }
    }

    LOG_WARNING(Frontend, "V2 core options not supported, trying V1");

    // Count number of options
    unsigned num_options = 0;
    while (option_definitions[num_options].key != nullptr) {
        num_options++;
    }

    if (version >= 1) {
        // Create V1 options array
        std::vector<retro_core_option_definition> options_v1(num_options + 1);

        // Copy parameters from V2 to V1
        for (unsigned i = 0; i < num_options; i++) {
            const auto& v2_option = option_definitions[i];
            auto& v1_option = options_v1[i];

            v1_option.key = v2_option.key;
            v1_option.desc = v2_option.desc;
            v1_option.info = v2_option.info;
            v1_option.default_value = v2_option.default_value;
            std::memcpy(v1_option.values, v2_option.values, sizeof(v1_option.values));
        }

        // Null terminator
        std::memset(&options_v1.back(), 0, sizeof(retro_core_option_definition));

        if (LibRetro::SetCoreOptionsV1(options_v1.data())) {
            LOG_INFO(Frontend, "V1 core options set successfully");
            return;
        }
    }

    LOG_WARNING(Frontend, "V1 core options not supported, trying V0");

    // Create V0 variables array
    std::vector<retro_variable> variables(num_options + 1);
    std::vector<std::string> values_buffer(num_options);

    for (unsigned i = 0; i < num_options; i++) {
        const auto& option = option_definitions[i];
        std::string desc = option.desc ? option.desc : "";
        std::string default_value = option.default_value ? option.default_value : "";

        values_buffer[i] = "";

        if (!desc.empty()) {
            // Count number of values
            size_t num_values = 0;
            size_t default_index = 0;

            while (option.values[num_values].value != nullptr) {
                if (!default_value.empty() &&
                    std::string(option.values[num_values].value) == default_value) {
                    default_index = num_values;
                }
                num_values++;
            }

            // Build values string: "Description; default_value|other_value1|other_value2"
            if (num_values > 0) {
                values_buffer[i] = desc + "; " + option.values[default_index].value;

                // Add remaining values
                for (size_t j = 0; j < num_values; j++) {
                    if (j != default_index) {
                        values_buffer[i] += "|" + std::string(option.values[j].value);
                    }
                }
            }
        }

        variables[i].key = option.key;
        variables[i].value = values_buffer[i].c_str();
    }

    // Null terminator
    std::memset(&variables.back(), 0, sizeof(retro_variable));

    // Set V0 variables
    if (LibRetro::SetVariables(variables.data())) {
        LOG_INFO(Frontend, "V0 core options set successfully");
    } else {
        LOG_ERROR(Frontend, "Failed to set core options with any version");
    }
}

static void ParseCpuOptions(void) {
    Settings::values.use_cpu_jit =
        LibRetro::FetchVariable(config::cpu::use_cpu_jit, config::enabled) == config::enabled;
#if defined(IOS)
    if (!LibRetro::CanUseJIT())
        Settings::values.use_cpu_jit = false;
#endif

    auto cpu_clock = LibRetro::FetchVariable(config::cpu::cpu_clock_percentage, "100");
    Settings::values.cpu_clock_percentage = std::stoi(cpu_clock);
}

static int GetRegionValue(const std::string& name) {
    if (name == "Japan")
        return 0;
    if (name == "USA")
        return 1;
    if (name == "Europe")
        return 2;
    if (name == "Australia")
        return 3;
    if (name == "China")
        return 4;
    if (name == "Korea")
        return 5;
    if (name == "Taiwan")
        return 6;
    return -1; // Auto
}

static Service::CFG::SystemLanguage GetLanguageValue(const std::string& name) {
    if (name == "Japanese")
        return Service::CFG::LANGUAGE_JP;
    if (name == "French")
        return Service::CFG::LANGUAGE_FR;
    if (name == "Spanish")
        return Service::CFG::LANGUAGE_ES;
    if (name == "German")
        return Service::CFG::LANGUAGE_DE;
    if (name == "Italian")
        return Service::CFG::LANGUAGE_IT;
    if (name == "Dutch")
        return Service::CFG::LANGUAGE_NL;
    if (name == "Portuguese")
        return Service::CFG::LANGUAGE_PT;
    if (name == "Russian")
        return Service::CFG::LANGUAGE_RU;
    if (name == "Korean")
        return Service::CFG::LANGUAGE_KO;
    if (name == "Traditional Chinese")
        return Service::CFG::LANGUAGE_TW;
    if (name == "Simplified Chinese")
        return Service::CFG::LANGUAGE_ZH;
    return Service::CFG::LANGUAGE_EN; // English default
}

static void ParseSystemOptions(void) {
    Settings::values.is_new_3ds =
        LibRetro::FetchVariable(config::system::is_new_3ds, "Old 3DS") == "New 3DS";

    Settings::values.region_value =
        GetRegionValue(LibRetro::FetchVariable("citra_region_value", "Auto"));

    LibRetro::settings.language_value =
        GetLanguageValue(LibRetro::FetchVariable(config::system::language, "English"));
}

static Settings::AudioEmulation GetAudioEmulation(const std::string& name) {
    if (name == "lle")
        return Settings::AudioEmulation::LLE;
    if (name == "lle_multithread")
        return Settings::AudioEmulation::LLEMultithreaded;
    return Settings::AudioEmulation::HLE; // Default
}

static void ParseAudioOptions(void) {
    Settings::values.audio_emulation =
        GetAudioEmulation(LibRetro::FetchVariable(config::audio::audio_emulation, "hle"));

    auto input_type = LibRetro::FetchVariable(config::audio::input_type, "auto");
    if (input_type == "none") {
        Settings::values.input_type = AudioCore::InputType::Null;
    } else if (input_type == "static_noise") {
        Settings::values.input_type = AudioCore::InputType::Static;
    } else if (input_type == "frontend") {
        Settings::values.input_type = AudioCore::InputType::Cubeb; // Use Cubeb as frontend input
    } else {
        Settings::values.input_type = AudioCore::InputType::Auto;
    }
}

static Settings::TextureFilter GetTextureFilter(const std::string& name) {
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

static Settings::TextureSampling GetTextureSampling(const std::string& name) {
    if (name == "NearestNeighbor")
        return Settings::TextureSampling::NearestNeighbor;
    if (name == "Linear")
        return Settings::TextureSampling::Linear;

    return Settings::TextureSampling::GameControlled;
}

static Settings::GraphicsAPI GetGraphicsAPI(const std::string& name) {
    if (name == "Software")
        return Settings::GraphicsAPI::Software;
#ifdef ENABLE_VULKAN
    if (name == "Vulkan")
        return Settings::GraphicsAPI::Vulkan;
#endif
#ifdef ENABLE_OPENGL
    if (name == "OpenGL")
        return Settings::GraphicsAPI::OpenGL;
#endif
    // Auto selection
    return LibRetro::GetPreferredRenderer();
}

static void ParseGraphicsOptions(void) {
    Settings::values.graphics_api =
        GetGraphicsAPI(LibRetro::FetchVariable(config::graphics::graphics_api, "auto"));

    Settings::values.use_hw_shader = LibRetro::FetchVariable(config::graphics::use_hw_shader,
                                                             config::enabled) == config::enabled;

    Settings::values.use_shader_jit = LibRetro::FetchVariable(config::graphics::use_shader_jit,
                                                              config::enabled) == config::enabled;

    Settings::values.shaders_accurate_mul =
        LibRetro::FetchVariable(config::graphics::shaders_accurate_mul, config::enabled) ==
        config::enabled;

    Settings::values.use_disk_shader_cache =
        LibRetro::FetchVariable(config::graphics::use_disk_shader_cache, config::enabled) ==
        config::enabled;

    auto resolution = LibRetro::FetchVariable(config::graphics::resolution_factor, "1");
    Settings::values.resolution_factor = std::stoi(resolution);

    Settings::values.texture_filter =
        GetTextureFilter(LibRetro::FetchVariable(config::graphics::texture_filter, "none"));

    Settings::values.texture_sampling = GetTextureSampling(
        LibRetro::FetchVariable(config::graphics::texture_sampling, "GameControlled"));

    Settings::values.custom_textures = LibRetro::FetchVariable(config::graphics::custom_textures,
                                                               config::disabled) == config::enabled;

    Settings::values.dump_textures = LibRetro::FetchVariable(config::graphics::dump_textures,
                                                             config::disabled) == config::enabled;
}

static Settings::LayoutOption GetLayoutOption(const std::string& name) {
    if (name == "single_screen" || name == "Single Screen Only")
        return Settings::LayoutOption::SingleScreen;
    if (name == "large_screen" || name == "Large Screen, Small Screen")
        return Settings::LayoutOption::LargeScreen;
    if (name == "side_by_side" || name == "Side by Side")
        return Settings::LayoutOption::SideScreen;
    return Settings::LayoutOption::Default;
}

static void ParseLayoutOptions(void) {
    Settings::values.layout_option =
        GetLayoutOption(LibRetro::FetchVariable(config::layout::layout_option, "default"));

    Settings::values.swap_screen =
        LibRetro::FetchVariable(config::layout::swap_screen, "Top") == "Bottom";

    LibRetro::settings.toggle_swap_screen =
        LibRetro::FetchVariable(config::layout::toggle_swap_screen, "Toggle") == "Toggle";
}

static void ParseStorageOptions(void) {
    Settings::values.use_virtual_sd = LibRetro::FetchVariable(config::storage::use_virtual_sd,
                                                              config::enabled) == config::enabled;

    // Configure the file storage location
    auto use_libretro_saves = LibRetro::FetchVariable(config::storage::use_libretro_save_path,
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
}

static LibRetro::CStickFunction GetAnalogFunction(const std::string& name) {
    if (name == "c_stick" || name == "C-Stick")
        return LibRetro::CStickFunction::CStick;
    if (name == "touchscreen_pointer" || name == "Touchscreen Pointer")
        return LibRetro::CStickFunction::Touchscreen;
    return LibRetro::CStickFunction::Both; // Default
}

static void ParseInputOptions(void) {
    LibRetro::settings.analog_function = GetAnalogFunction(
        LibRetro::FetchVariable(config::input::analog_function, "c_stick_and_touchscreen"));

    if (LibRetro::settings.analog_function != LibRetro::CStickFunction::Touchscreen) {
        Settings::values.current_input_profile.analogs[1] = "axis:1,joystick:0,engine:libretro";
    } else {
        Settings::values.current_input_profile.analogs[1] = "";
    }

    auto deadzone = LibRetro::FetchVariable(config::input::deadzone, "15");
    LibRetro::settings.deadzone = static_cast<float>(std::stoi(deadzone)) / 100.0f;

    LibRetro::settings.mouse_touchscreen =
        LibRetro::FetchVariable(config::input::mouse_touchscreen, config::enabled) ==
        config::enabled;

    LibRetro::settings.touch_touchscreen =
        LibRetro::FetchVariable(config::input::touch_touchscreen, config::disabled) ==
        config::enabled;

    LibRetro::settings.render_touchscreen =
        LibRetro::FetchVariable(config::input::render_touchscreen, config::disabled) ==
        config::enabled;
}

void ParseCoreOptions(void) {
    // Override default values that aren't user-selectable and aren't correct for the core
    Settings::values.enable_audio_stretching = false;
    Settings::values.frame_limit = 10000;
#if defined(USING_GLES)
    Settings::values.use_gles = true;
#else
    Settings::values.use_gles = false;
#endif
    Settings::values.filter_mode = false;

    ParseCpuOptions();
    ParseSystemOptions();
    ParseAudioOptions();
    ParseGraphicsOptions();
    ParseLayoutOptions();
    ParseStorageOptions();
    ParseInputOptions();
}

} // namespace LibRetro
