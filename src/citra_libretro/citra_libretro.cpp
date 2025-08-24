// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <list>
#include <numeric>
#include <vector>
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

    LOG_DEBUG(Frontend, "Initializing core...");

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

    LibRetro::RegisterCoreOptions();

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

/**
 * Updates Citra's settings with Libretro's.
 */
static void UpdateSettings() {
    LibRetro::ParseCoreOptions();

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
        LibRetro::ParseCoreOptions();
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

    if (!emu_instance->game_loaded) {
        emu_instance->game_loaded = do_load_game();
    } else {
        // Game is already loaded, just recreate the renderer for the new GL context
        if (Settings::values.graphics_api.GetValue() == Settings::GraphicsAPI::OpenGL) {
            Core::System::GetInstance().GPU().RecreateRenderer(*emu_instance->emu_window, nullptr);
            if (Settings::values.use_disk_shader_cache) {
                Core::System::GetInstance().GPU().Renderer().Rasterizer()->LoadDefaultDiskResources(
                    false, nullptr);
            }
        }
    }
}

static void context_destroy() {
    LOG_DEBUG(Frontend, "context_destroy");
    if (emu_instance->game_loaded &&
        Settings::values.graphics_api.GetValue() == Settings::GraphicsAPI::OpenGL) {
        // Release the renderer's OpenGL resources
        Core::System::GetInstance().GPU().ReleaseRenderer();
    }
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
