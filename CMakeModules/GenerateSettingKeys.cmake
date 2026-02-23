## This file should be the *only place* where setting keys exist as strings.
# All references to setting strings should be derived from the
# `setting_keys.h` and `jni_setting_keys.cpp` files generated here.

# !!! Changes made here should be mirrored to SettingKeys.kt if applicable

# Shared setting keys (multi-platform)
foreach(KEY IN ITEMS
    "use_artic_base_controller"
    "enable_gamemode"
    "use_cpu_jit"
    "cpu_clock_percentage"
    "is_new_3ds"
    "lle_applets"
    "deterministic_async_operations"
    "enable_required_online_lle_modules"
    "use_virtual_sd"
    "use_custom_storage"
    "compress_cia_installs"
    "region_value"
    "init_clock"
    "init_time"
    "init_time_offset"
    "init_ticks_type"
    "init_ticks_override"
    "plugin_loader"
    "allow_plugin_loader"
    "steps_per_hour"
    "apply_region_free_patch"
    "graphics_api"
    "physical_device"
    "use_gles"
    "renderer_debug"
    "dump_command_buffers"
    "spirv_shader_gen"
    "disable_spirv_optimizer"
    "async_shader_compilation"
    "async_presentation"
    "use_hw_shader"
    "use_disk_shader_cache"
    "shaders_accurate_mul"
    "use_vsync"
    "use_display_refresh_rate_detection"
    "use_shader_jit"
    "resolution_factor"
    "frame_limit"
    "turbo_limit"
    "texture_filter"
    "texture_sampling"
    "delay_game_render_thread_us"
    "layout_option"
    "swap_screen"
    "upright_screen"
    "secondary_display_layout"
    "large_screen_proportion"
    "screen_gap"
    "small_screen_position"
    "custom_top_x"
    "custom_top_y"
    "custom_top_width"
    "custom_top_height"
    "custom_bottom_x"
    "custom_bottom_y"
    "custom_bottom_width"
    "custom_bottom_height"
    "custom_second_layer_opacity"
    "aspect_ratio"
    "screen_top_stretch"
    "screen_top_leftright_padding"
    "screen_top_topbottom_padding"
    "screen_bottom_stretch"
    "screen_bottom_leftright_padding"
    "screen_bottom_topbottom_padding"
    "portrait_layout_option"
    "custom_portrait_top_x"
    "custom_portrait_top_y"
    "custom_portrait_top_width"
    "custom_portrait_top_height"
    "custom_portrait_bottom_x"
    "custom_portrait_bottom_y"
    "custom_portrait_bottom_width"
    "custom_portrait_bottom_height"
    "bg_red"
    "bg_green"
    "bg_blue"
    "render_3d"
    "factor_3d"
    "swap_eyes_3d"
    "render_3d_which_display"
    "mono_render_option"
    "cardboard_screen_size"
    "cardboard_x_shift"
    "cardboard_y_shift"
    "filter_mode"
    "pp_shader_name"
    "anaglyph_shader_name"
    "dump_textures"
    "custom_textures"
    "preload_textures"
    "async_custom_loading"
    "disable_right_eye_render"
    "audio_emulation"
    "enable_audio_stretching"
    "enable_realtime_audio"
    "volume"
    "output_type"
    "output_device"
    "input_type"
    "input_device"
    "delay_start_for_lle_modules"
    "use_gdbstub"
    "gdbstub_port"
    "instant_debug_log"
    "enable_rpc_server"
    "log_filter"
    "log_regex_filter"
)
    set(SETTING_KEY_DEFINITIONS "${SETTING_KEY_DEFINITIONS}
        DEFINE_KEY(${KEY})")
    if (ANDROID)
        string(REPLACE "_" "_1" KEY_JNI_ESCAPED ${KEY})
        set(JNI_SETTING_KEY_DEFINITIONS "${JNI_SETTING_KEY_DEFINITIONS}
            JNI_DEFINE_KEY(${KEY}, ${KEY_JNI_ESCAPED})")
    endif()
endforeach()

# Libretro exclusive setting keys
if (ENABLE_LIBRETRO)
    foreach(KEY IN ITEMS
        "language_value"
        "swap_screen_mode"
        "use_libretro_save_path"
        "analog_function"
        "analog_deadzone"
        "enable_mouse_touchscreen"
        "enable_touch_touchscreen"
        "render_touchscreen"
        "enable_motion"
        "motion_sensitivity"
    )
        string(REPLACE "_" "_1" KEY_JNI_ESCAPED ${KEY})
        set(SETTING_KEY_DEFINITIONS "${SETTING_KEY_DEFINITIONS}
            DEFINE_KEY(${KEY})")
    endforeach()
endif()

# Android exclusive setting keys (standalone app only, not Android libretro)
if (ANDROID)
    foreach(KEY IN ITEMS
        "expand_to_cutout_area"
        "custom_layout"
        "performance_overlay_enable"
        "performance_overlay_show_fps"
        "performance_overlay_show_frame_time"
        "performance_overlay_show_speed"
        "performance_overlay_show_app_ram_usage"
        "performance_overlay_show_available_ram"
        "performance_overlay_show_battery_temp"
        "performance_overlay_background"
        "use_frame_limit" # FIXME: DUPLICATE KEY (shared equivalent: frame_limit)
        "android_hide_images"
        "camera_inner_flip"
        "camera_outer_left_flip"
        "camera_outer_right_flip"
        "screen_orientation"
        "performance_overlay_position"
        "camera_inner_name"
        "camera_inner_config"
        "camera_outer_left_name"
        "camera_outer_left_config"
        "camera_outer_right_name"
        "camera_outer_right_config"
    )
        string(REPLACE "_" "_1" KEY_JNI_ESCAPED ${KEY})
        set(SETTING_KEY_DEFINITIONS "${SETTING_KEY_DEFINITIONS}
            DEFINE_KEY(${KEY})")
        set(JNI_SETTING_KEY_DEFINITIONS "${JNI_SETTING_KEY_DEFINITIONS}
            JNI_DEFINE_KEY(${KEY}, ${KEY_JNI_ESCAPED})")
    endforeach()
endif()

configure_file("common/setting_keys.h.in" "common/setting_keys.h" @ONLY)
if (ANDROID AND NOT ENABLE_LIBRETRO)
    configure_file("android/app/src/main/jni/jni_setting_keys.cpp.in" "android/app/src/main/jni/jni_setting_keys.cpp" @ONLY)
endif()
