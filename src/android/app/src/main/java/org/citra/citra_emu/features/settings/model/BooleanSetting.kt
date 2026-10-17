// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

package org.citra.citra_emu.features.settings.model

import org.citra.citra_emu.features.settings.SettingKeys

enum class BooleanSetting(
    override val key: String,
    override val section: String,
    override val defaultValue: Boolean
) : AbstractSetting<Boolean> {
    EXPAND_TO_CUTOUT_AREA(SettingKeys.expand_to_cutout_area(), CitraSettings.SECTION_LAYOUT, false),
    SPIRV_SHADER_GEN(SettingKeys.spirv_shader_gen(), CitraSettings.SECTION_RENDERER, true),
    ASYNC_SHADERS(SettingKeys.async_shader_compilation(), CitraSettings.SECTION_RENDERER, false),
    DISABLE_SPIRV_OPTIMIZER(
        SettingKeys.disable_spirv_optimizer(),
        CitraSettings.SECTION_RENDERER,
        true
    ),
    PLUGIN_LOADER(SettingKeys.plugin_loader(), CitraSettings.SECTION_SYSTEM, false),
    ALLOW_PLUGIN_LOADER(SettingKeys.allow_plugin_loader(), CitraSettings.SECTION_SYSTEM, true),
    SWAP_SCREEN(SettingKeys.swap_screen(), CitraSettings.SECTION_LAYOUT, false),
    INSTANT_DEBUG_LOG(SettingKeys.instant_debug_log(), CitraSettings.SECTION_DEBUG, false),
    ENABLE_RPC_SERVER(SettingKeys.enable_rpc_server(), CitraSettings.SECTION_DEBUG, false),
    TOGGLE_UNIQUE_DATA_CONSOLE_TYPE(
        SettingKeys.toggle_unique_data_console_type(),
        CitraSettings.SECTION_DEBUG,
        false
    ),
    SWAP_EYES_3D(SettingKeys.swap_eyes_3d(), CitraSettings.SECTION_RENDERER, false),
    PERF_OVERLAY_ENABLE(
        SettingKeys.performance_overlay_enable(),
        CitraSettings.SECTION_LAYOUT,
        false
    ),
    PERF_OVERLAY_SHOW_FPS(
        SettingKeys.performance_overlay_show_fps(),
        CitraSettings.SECTION_LAYOUT,
        true
    ),
    PERF_OVERLAY_SHOW_FRAMETIME(
        SettingKeys.performance_overlay_show_frame_time(),
        CitraSettings.SECTION_LAYOUT,
        false
    ),
    PERF_OVERLAY_SHOW_SPEED(
        SettingKeys.performance_overlay_show_speed(),
        CitraSettings.SECTION_LAYOUT,
        false
    ),
    PERF_OVERLAY_SHOW_APP_RAM_USAGE(
        SettingKeys.performance_overlay_show_app_ram_usage(),
        CitraSettings.SECTION_LAYOUT,
        false
    ),
    PERF_OVERLAY_SHOW_AVAILABLE_RAM(
        SettingKeys.performance_overlay_show_available_ram(),
        CitraSettings.SECTION_LAYOUT,
        false
    ),
    PERF_OVERLAY_SHOW_BATTERY_TEMP(
        SettingKeys.performance_overlay_show_battery_temp(),
        CitraSettings.SECTION_LAYOUT,
        false
    ),
    PERF_OVERLAY_BACKGROUND(
        SettingKeys.performance_overlay_background(),
        CitraSettings.SECTION_LAYOUT,
        false
    ),
    DELAY_START_LLE_MODULES(
        SettingKeys.delay_start_for_lle_modules(),
        CitraSettings.SECTION_DEBUG,
        true
    ),
    DETERMINISTIC_ASYNC_OPERATIONS(
        SettingKeys.deterministic_async_operations(),
        CitraSettings.SECTION_DEBUG,
        false
    ),
    REQUIRED_ONLINE_LLE_MODULES(
        SettingKeys.enable_required_online_lle_modules(),
        CitraSettings.SECTION_SYSTEM,
        false
    ),
    LLE_APPLETS(SettingKeys.lle_applets(), CitraSettings.SECTION_SYSTEM, false),
    NEW_3DS(SettingKeys.is_new_3ds(), CitraSettings.SECTION_SYSTEM, true),
    LINEAR_FILTERING(SettingKeys.filter_mode(), CitraSettings.SECTION_RENDERER, true),
    SHADERS_ACCURATE_MUL(SettingKeys.shaders_accurate_mul(), CitraSettings.SECTION_RENDERER, false),
    DISK_SHADER_CACHE(SettingKeys.use_disk_shader_cache(), CitraSettings.SECTION_RENDERER, true),
    DUMP_TEXTURES(SettingKeys.dump_textures(), CitraSettings.SECTION_UTILITY, false),
    CUSTOM_TEXTURES(SettingKeys.custom_textures(), CitraSettings.SECTION_UTILITY, false),
    ASYNC_CUSTOM_LOADING(SettingKeys.async_custom_loading(), CitraSettings.SECTION_UTILITY, true),
    PRELOAD_TEXTURES(SettingKeys.preload_textures(), CitraSettings.SECTION_UTILITY, false),
    ENABLE_AUDIO_STRETCHING(
        SettingKeys.enable_audio_stretching(),
        CitraSettings.SECTION_AUDIO,
        true
    ),
    ENABLE_REALTIME_AUDIO(SettingKeys.enable_realtime_audio(), CitraSettings.SECTION_AUDIO, false),
    SIMULATE_HEADPHONES_PLUGGED(
        SettingKeys.simulate_headphones_plugged(),
        CitraSettings.SECTION_AUDIO,
        false
    ),
    CPU_JIT(SettingKeys.use_cpu_jit(), CitraSettings.SECTION_CORE, true),
    USE_FASTINTERP(SettingKeys.use_fastinterp(), CitraSettings.SECTION_CORE, true),
    HW_SHADER(SettingKeys.use_hw_shader(), CitraSettings.SECTION_RENDERER, true),
    SHADER_JIT(SettingKeys.use_shader_jit(), CitraSettings.SECTION_RENDERER, true),
    VSYNC(SettingKeys.use_vsync(), CitraSettings.SECTION_RENDERER, false),
    USE_SKIP_DUPLICATE_FRAMES(
        SettingKeys.use_skip_duplicate_frames(),
        CitraSettings.SECTION_RENDERER,
        false
    ),
    USE_FRAME_LIMIT(SettingKeys.use_frame_limit(), CitraSettings.SECTION_RENDERER, true),
    DEBUG_RENDERER(SettingKeys.renderer_debug(), CitraSettings.SECTION_DEBUG, false),
    DISABLE_RIGHT_EYE_RENDER(
        SettingKeys.disable_right_eye_render(),
        CitraSettings.SECTION_RENDERER,
        false
    ),
    USE_ARTIC_BASE_CONTROLLER(
        SettingKeys.use_artic_base_controller(),
        CitraSettings.SECTION_CONTROLS,
        false
    ),
    UPRIGHT_SCREEN(SettingKeys.upright_screen(), CitraSettings.SECTION_LAYOUT, false),
    COMPRESS_INSTALLED_CIA_CONTENT(
        SettingKeys.compress_cia_installs(),
        CitraSettings.SECTION_STORAGE,
        false
    ),
    ASYNC_FS_OPERATIONS(SettingKeys.async_fs_operations(), CitraSettings.SECTION_STORAGE, true),
    ANDROID_HIDE_IMAGES(SettingKeys.android_hide_images(), CitraSettings.SECTION_MISC, false),
    APPLY_REGION_FREE_PATCH(
        SettingKeys.apply_region_free_patch(),
        CitraSettings.SECTION_SYSTEM,
        true
    ),
    USE_INTEGER_SCALING(SettingKeys.use_integer_scaling(), CitraSettings.SECTION_RENDERER, false),
    ENABLE_SECONDARY_DISPLAY(
        SettingKeys.enable_secondary_display(),
        CitraSettings.SECTION_LAYOUT,
        true
    ),
    SIMULATE_3DS_GPU_TIMINGS(
        SettingKeys.simulate_3ds_gpu_timings(),
        CitraSettings.SECTION_RENDERER,
        false
    ),
    CHECK_FOR_UPDATES(SettingKeys.check_for_update_on_start(), CitraSettings.SECTION_MISC, true);

    override fun valueFromString(string: String): Boolean? = when (string.trim().lowercase()) {
        "1", "true" -> true
        "0", "false" -> false
        else -> null
    }

    override val isRuntimeEditable: Boolean
        get() {
            for (setting in NOT_RUNTIME_EDITABLE) {
                if (setting == this) {
                    return false
                }
            }
            return true
        }

    companion object {
        private val NOT_RUNTIME_EDITABLE = listOf(
            PLUGIN_LOADER,
            ALLOW_PLUGIN_LOADER,
            ASYNC_SHADERS,
            DELAY_START_LLE_MODULES,
            DETERMINISTIC_ASYNC_OPERATIONS,
            REQUIRED_ONLINE_LLE_MODULES,
            NEW_3DS,
            LLE_APPLETS,
            TOGGLE_UNIQUE_DATA_CONSOLE_TYPE,
            VSYNC,
            DEBUG_RENDERER,
            CPU_JIT,
            USE_FASTINTERP,
            ASYNC_CUSTOM_LOADING,
            SHADERS_ACCURATE_MUL,
            USE_ARTIC_BASE_CONTROLLER,
            COMPRESS_INSTALLED_CIA_CONTENT,
            ASYNC_FS_OPERATIONS,
            ANDROID_HIDE_IMAGES,
            PERF_OVERLAY_ENABLE, // Works in overlay options, but not from the settings menu
            APPLY_REGION_FREE_PATCH,
            EXPAND_TO_CUTOUT_AREA
        )

        fun from(key: String): BooleanSetting? =
            BooleanSetting.values().firstOrNull { it.key == key }
    }
}
