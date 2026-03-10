// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model

import org.citra.citra_emu.features.settings.SettingKeys

enum class FloatSetting(
    override val key: String,
    override val section: String,
    override val defaultValue: Float,
    val scale:Int = 1
) : AbstractSetting<Float> {
    LARGE_SCREEN_PROPORTION(SettingKeys.large_screen_proportion(),Settings.SECTION_LAYOUT,2.25f),
    SECOND_SCREEN_OPACITY(SettingKeys.custom_second_layer_opacity(), Settings.SECTION_RENDERER, 100f),
    BACKGROUND_RED(SettingKeys.bg_red(), Settings.SECTION_RENDERER, 0f, 255),
    BACKGROUND_BLUE(SettingKeys.bg_blue(), Settings.SECTION_RENDERER, 0f, 255),
    BACKGROUND_GREEN(SettingKeys.bg_green(), Settings.SECTION_RENDERER, 0f, 255),
    AUDIO_VOLUME(SettingKeys.volume(), Settings.SECTION_AUDIO, 1.0f, 100);

    // valueFromString reads raw setting from file, scales up for UI
    override fun valueFromString(string: String): Float? {
        return string.toFloatOrNull()?.times(scale)
    }

    // valueToString scales back down to raw for file
    override fun valueToString(value: Float): String {
        return (value / scale).toString()
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
        private val NOT_RUNTIME_EDITABLE = emptyList<FloatSetting>()

        fun from(key: String): FloatSetting? = FloatSetting.values().firstOrNull { it.key == key }
    }
}
