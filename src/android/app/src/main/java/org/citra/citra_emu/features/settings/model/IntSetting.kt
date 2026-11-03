// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

package org.citra.citra_emu.features.settings.model

import org.citra.citra_emu.features.settings.SettingKeys

enum class IntSetting(
    override val key: String,
    override val section: String,
    override val defaultValue: Int
) : AbstractSetting<Int> {
    FRAME_LIMIT(SettingKeys.frame_limit(), CitraSettings.SECTION_RENDERER, 100),
    EMULATED_REGION(SettingKeys.region_value(), CitraSettings.SECTION_SYSTEM, -1),
    INIT_CLOCK(SettingKeys.init_clock(), CitraSettings.SECTION_SYSTEM, 0),
    CAMERA_INNER_FLIP(SettingKeys.camera_inner_flip(), CitraSettings.SECTION_CAMERA, 0),
    CAMERA_OUTER_LEFT_FLIP(SettingKeys.camera_outer_left_flip(), CitraSettings.SECTION_CAMERA, 0),
    CAMERA_OUTER_RIGHT_FLIP(SettingKeys.camera_outer_right_flip(), CitraSettings.SECTION_CAMERA, 0),
    GRAPHICS_API(SettingKeys.graphics_api(), CitraSettings.SECTION_RENDERER, 2),
    RESOLUTION_FACTOR(SettingKeys.resolution_factor(), CitraSettings.SECTION_RENDERER, 1),
    STEREOSCOPIC_3D_MODE(SettingKeys.render_3d(), CitraSettings.SECTION_RENDERER, 2),
    STEREOSCOPIC_3D_DEPTH(SettingKeys.factor_3d(), CitraSettings.SECTION_RENDERER, 0),
    STEPS_PER_HOUR(SettingKeys.steps_per_hour(), CitraSettings.SECTION_SYSTEM, 0),
    CARDBOARD_SCREEN_SIZE(SettingKeys.cardboard_screen_size(), CitraSettings.SECTION_LAYOUT, 85),
    CARDBOARD_X_SHIFT(SettingKeys.cardboard_x_shift(), CitraSettings.SECTION_LAYOUT, 0),
    CARDBOARD_Y_SHIFT(SettingKeys.cardboard_y_shift(), CitraSettings.SECTION_LAYOUT, 0),
    SCREEN_LAYOUT(SettingKeys.layout_option(), CitraSettings.SECTION_LAYOUT, 0),
    SMALL_SCREEN_POSITION(SettingKeys.small_screen_position(), CitraSettings.SECTION_LAYOUT, 0),
    LANDSCAPE_TOP_X(SettingKeys.custom_top_x(), CitraSettings.SECTION_LAYOUT, 0),
    LANDSCAPE_TOP_Y(SettingKeys.custom_top_y(), CitraSettings.SECTION_LAYOUT, 0),
    LANDSCAPE_TOP_WIDTH(SettingKeys.custom_top_width(), CitraSettings.SECTION_LAYOUT, 800),
    LANDSCAPE_TOP_HEIGHT(SettingKeys.custom_top_height(), CitraSettings.SECTION_LAYOUT, 480),
    LANDSCAPE_BOTTOM_X(SettingKeys.custom_bottom_x(), CitraSettings.SECTION_LAYOUT, 80),
    LANDSCAPE_BOTTOM_Y(SettingKeys.custom_bottom_y(), CitraSettings.SECTION_LAYOUT, 480),
    LANDSCAPE_BOTTOM_WIDTH(SettingKeys.custom_bottom_width(), CitraSettings.SECTION_LAYOUT, 640),
    LANDSCAPE_BOTTOM_HEIGHT(SettingKeys.custom_bottom_height(), CitraSettings.SECTION_LAYOUT, 480),
    SCREEN_GAP(SettingKeys.screen_gap(), CitraSettings.SECTION_LAYOUT, 0),
    PORTRAIT_SCREEN_LAYOUT(SettingKeys.portrait_layout_option(), CitraSettings.SECTION_LAYOUT, 0),
    SECONDARY_DISPLAY_LAYOUT(
        SettingKeys.secondary_display_layout(),
        CitraSettings.SECTION_LAYOUT,
        4
    ),
    PORTRAIT_TOP_X(SettingKeys.custom_portrait_top_x(), CitraSettings.SECTION_LAYOUT, 0),
    PORTRAIT_TOP_Y(SettingKeys.custom_portrait_top_y(), CitraSettings.SECTION_LAYOUT, 0),
    PORTRAIT_TOP_WIDTH(SettingKeys.custom_portrait_top_width(), CitraSettings.SECTION_LAYOUT, 800),
    PORTRAIT_TOP_HEIGHT(
        SettingKeys.custom_portrait_top_height(),
        CitraSettings.SECTION_LAYOUT,
        480
    ),
    PORTRAIT_BOTTOM_X(SettingKeys.custom_portrait_bottom_x(), CitraSettings.SECTION_LAYOUT, 80),
    PORTRAIT_BOTTOM_Y(SettingKeys.custom_portrait_bottom_y(), CitraSettings.SECTION_LAYOUT, 480),
    PORTRAIT_BOTTOM_WIDTH(
        SettingKeys.custom_portrait_bottom_width(),
        CitraSettings.SECTION_LAYOUT,
        640
    ),
    PORTRAIT_BOTTOM_HEIGHT(
        SettingKeys.custom_portrait_bottom_height(),
        CitraSettings.SECTION_LAYOUT,
        480
    ),
    AUDIO_INPUT_TYPE(SettingKeys.input_type(), CitraSettings.SECTION_AUDIO, 0),
    CPU_CLOCK_SPEED(SettingKeys.cpu_clock_percentage(), CitraSettings.SECTION_CORE, 100),
    TEXTURE_FILTER(SettingKeys.texture_filter(), CitraSettings.SECTION_RENDERER, 0),
    TEXTURE_SAMPLING(SettingKeys.texture_sampling(), CitraSettings.SECTION_RENDERER, 0),
    DELAY_RENDER_THREAD_US(
        SettingKeys.delay_game_render_thread_us(),
        CitraSettings.SECTION_RENDERER,
        0
    ),
    ORIENTATION_OPTION(SettingKeys.screen_orientation(), CitraSettings.SECTION_LAYOUT, 2),
    TURBO_LIMIT(SettingKeys.turbo_limit(), CitraSettings.SECTION_CORE, 200),
    PERFORMANCE_OVERLAY_POSITION(
        SettingKeys.performance_overlay_position(),
        CitraSettings.SECTION_LAYOUT,
        0
    ),
    RENDER_3D_WHICH_DISPLAY(
        SettingKeys.render_3d_which_display(),
        CitraSettings.SECTION_RENDERER,
        0
    ),
    ASPECT_RATIO(SettingKeys.aspect_ratio(), CitraSettings.SECTION_LAYOUT, 0),
    UPDATE_CHECK_CHANNEL(SettingKeys.update_check_channel(), CitraSettings.SECTION_MISC, 0);

    override fun valueFromString(string: String): Int? {
        var value = string.toIntOrNull()
        if (value == null) {
            value = when (string.trim().lowercase()) {
                "true" -> 1
                "false" -> 0
                else -> null
            }
        }

        return value
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
            EMULATED_REGION,
            INIT_CLOCK,
            GRAPHICS_API,
            AUDIO_INPUT_TYPE
        )

        fun from(key: String): IntSetting? = IntSetting.values().firstOrNull { it.key == key }
    }
}

// The following must match what is defined in src/common/settings.h

enum class GraphicsAPI(val int: Int) {
    SOFTWARE(0),
    OPENGL(1),
    VULKAN(2)
}

enum class TextureFilter(val int: Int) {
    NOFILTER(0),
    ANIME4K(1),
    BICUBIC(2),
    SCALEFORCE(3),
    XBRZ(4),
    MMPX(5)
}
