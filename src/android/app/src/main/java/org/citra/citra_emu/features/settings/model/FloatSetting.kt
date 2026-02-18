// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model

enum class FloatSetting(
    override val key: String,
    override val section: String,
    override val defaultValue: Float
) : AbstractFloatSetting {
    LARGE_SCREEN_PROPORTION("large_screen_proportion",Settings.SECTION_LAYOUT,2.25f),
    SECOND_SCREEN_OPACITY("custom_second_layer_opacity", Settings.SECTION_RENDERER, 100f),
    BACKGROUND_RED("bg_red", Settings.SECTION_RENDERER, 0f),
    BACKGROUND_BLUE("bg_blue", Settings.SECTION_RENDERER, 0f),
    BACKGROUND_GREEN("bg_green", Settings.SECTION_RENDERER, 0f),
    EMPTY_SETTING("", "", 0.0f);

    override var float: Float = defaultValue

    override val valueAsString: String
        get() = float.toString()

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

        fun clear() = FloatSetting.values().forEach { it.float = it.defaultValue }
    }
}
