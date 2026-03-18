// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model

import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.features.input.GamepadHelper
import org.citra.citra_emu.features.input.Hotkey
import org.citra.citra_emu.features.input.Input
import org.citra.citra_emu.features.settings.SettingKeys

enum class InputMappingSetting(
    override val key: String,
    override val section: String,
    override val defaultValue: Input = Input(),
    val outKey: Int? = null,
    val outAxis: Int? = null,
    val outDirection: Int? = null
) : AbstractSetting<Input?> {
    BUTTON_A(
        SettingKeys.button_a(),
        CitraSettings.SECTION_CONTROLS,
        outKey = NativeLibrary.ButtonType.BUTTON_A
    ),
    BUTTON_B(
        SettingKeys.button_b(),
        CitraSettings.SECTION_CONTROLS,
        outKey = NativeLibrary.ButtonType.BUTTON_B
    ),
    BUTTON_X(
        SettingKeys.button_x(),
        CitraSettings.SECTION_CONTROLS,
        outKey = NativeLibrary.ButtonType.BUTTON_X
    ),
    BUTTON_Y(
        SettingKeys.button_y(),
        CitraSettings.SECTION_CONTROLS,
        outKey = NativeLibrary.ButtonType.BUTTON_Y
    ),
    BUTTON_HOME(
        SettingKeys.button_home(),
        CitraSettings.SECTION_CONTROLS,
        outKey = NativeLibrary.ButtonType.BUTTON_HOME
    ),
    BUTTON_L(
        SettingKeys.button_l(),
        CitraSettings.SECTION_CONTROLS,
        outKey = NativeLibrary.ButtonType.TRIGGER_L
    ),
    BUTTON_R(
        SettingKeys.button_r(),
        CitraSettings.SECTION_CONTROLS,
        outKey = NativeLibrary.ButtonType.TRIGGER_R
    ),
    BUTTON_SELECT(
        SettingKeys.button_select(),
        CitraSettings.SECTION_CONTROLS,
        outKey = NativeLibrary.ButtonType.BUTTON_SELECT
    ),
    BUTTON_START(
        SettingKeys.button_start(),
        CitraSettings.SECTION_CONTROLS,
        outKey = NativeLibrary.ButtonType.BUTTON_START
    ),
    BUTTON_ZL(
        SettingKeys.button_zl(),
        CitraSettings.SECTION_CONTROLS,
        outKey = NativeLibrary.ButtonType.BUTTON_ZL
    ),
    BUTTON_ZR(
        SettingKeys.button_zr(),
        CitraSettings.SECTION_CONTROLS,
        outKey = NativeLibrary.ButtonType.BUTTON_ZR
    ),
    DPAD_UP(
        SettingKeys.dpad_up(),
        CitraSettings.SECTION_CONTROLS,
        outKey = NativeLibrary.ButtonType.DPAD_UP
    ),
    DPAD_DOWN(
        SettingKeys.dpad_down(),
        CitraSettings.SECTION_CONTROLS,
        outKey = NativeLibrary.ButtonType.DPAD_DOWN
    ),
    DPAD_LEFT(
        SettingKeys.dpad_left(),
        CitraSettings.SECTION_CONTROLS,
        outKey = NativeLibrary.ButtonType.DPAD_LEFT
    ),
    DPAD_RIGHT(
        SettingKeys.dpad_right(),
        CitraSettings.SECTION_CONTROLS,
        outKey = NativeLibrary.ButtonType.DPAD_RIGHT
    ),

    CIRCLEPAD_UP(
        SettingKeys.circlepad_up(),
        CitraSettings.SECTION_CONTROLS,
        outAxis = NativeLibrary.ButtonType.STICK_LEFT_UP,
        outDirection = -1
    ),
    CIRCLEPAD_DOWN(
        SettingKeys.circlepad_down(),
        CitraSettings.SECTION_CONTROLS,
        outAxis = NativeLibrary.ButtonType.STICK_LEFT_DOWN,
        outDirection = 1
    ),
    CIRCLEPAD_LEFT(
        SettingKeys.circlepad_left(),
        CitraSettings.SECTION_CONTROLS,
        outAxis = NativeLibrary.ButtonType.STICK_LEFT_LEFT,
        outDirection = -1
    ),
    CIRCLEPAD_RIGHT(
        SettingKeys.circlepad_right(),
        CitraSettings.SECTION_CONTROLS,
        outAxis = NativeLibrary.ButtonType.STICK_LEFT_RIGHT,
        outDirection = 1
    ),
    CSTICK_UP(
        SettingKeys.cstick_up(),
        CitraSettings.SECTION_CONTROLS,
        outAxis = NativeLibrary.ButtonType.STICK_C_UP,
        outDirection = -1
    ),
    CSTICK_DOWN(
        SettingKeys.cstick_down(),
        CitraSettings.SECTION_CONTROLS,
        outAxis = NativeLibrary.ButtonType.STICK_C_DOWN,
        outDirection = 1
    ),
    CSTICK_LEFT(
        SettingKeys.cstick_left(),
        CitraSettings.SECTION_CONTROLS,
        outAxis = NativeLibrary.ButtonType.STICK_C_LEFT,
        outDirection = -1
    ),
    CSTICK_RIGHT(
        SettingKeys.cstick_right(),
        CitraSettings.SECTION_CONTROLS,
        outAxis = NativeLibrary.ButtonType.STICK_C_RIGHT,
        outDirection = 1
    ),
    HOTKEY_CYCLE_LAYOUT(
        SettingKeys.hotkey_cycle_layout(),
        CitraSettings.SECTION_CONTROLS,
        outKey = Hotkey.CYCLE_LAYOUT.button
    ),
    HOTKEY_CLOSE_GAME(
        SettingKeys.hotkey_close(),
        CitraSettings.SECTION_CONTROLS,
        outKey = Hotkey.CLOSE_GAME.button
    ),
    HOTKEY_SWAP(
        SettingKeys.hotkey_swap(),
        CitraSettings.SECTION_CONTROLS,
        outKey = Hotkey.SWAP_SCREEN.button
    ),
    HOTKEY_PAUSE_OR_RESUME(
        SettingKeys.hotkey_pause_resume(),
        CitraSettings.SECTION_CONTROLS,
        outKey = Hotkey.PAUSE_OR_RESUME.button
    ),
    HOTKEY_QUICKSAVE(
        SettingKeys.hotkey_quicksave(),
        CitraSettings.SECTION_CONTROLS,
        outKey = Hotkey.QUICKSAVE.button
    ),
    HOTKEY_TURBO_LIMIT(
        SettingKeys.hotkey_turbo_limit(),
        CitraSettings.SECTION_CONTROLS,
        outKey = Hotkey.TURBO_LIMIT.button
    ),
    HOTKEY_QUICKLOAD(
        SettingKeys.hotkey_quickload(),
        CitraSettings.SECTION_CONTROLS,
        outKey = Hotkey.QUICKLOAD.button
    ),
    HOTKEY_ENABLE(
        SettingKeys.hotkey_enable(),
        CitraSettings.SECTION_CONTROLS,
        outKey = Hotkey.ENABLE.button
    ),
    HOTKEY_COMBO_BUTTON(
        key = SettingKeys.hotkey_combo_button(),
        CitraSettings.SECTION_CONTROLS,
        outKey = Hotkey.COMBO_BUTTON.button
    );

    /** Parse a configuration string into an input binding */
    override fun valueFromString(string: String): Input {
        if (string.isBlank()) return defaultValue

        val params = string.split(",")
            .mapNotNull { part ->
                val split = part.split(":", limit = 2)
                if (split.size < 2) null else split[0] to split[1]
            }.toMap()
        if (params["engine"] != "gamepad") return defaultValue
        return Input(
            key = params["code"]?.toIntOrNull(),
            axis = params["axis"]?.toIntOrNull(),
            direction = params["direction"]?.toIntOrNull(),
            threshold = params["threshold"]?.toFloatOrNull()
        ).takeIf { it.key != null || it.axis != null } ?: defaultValue
    }

    /** Create a configuration string from an input binding */
    override fun valueToString(binding: Input?): String {
        binding ?: return ""
        if (binding.empty) return ""
        val ret = "engine:gamepad"
        return ret + when {
            binding.key != null -> ",code:${binding.key}"

            binding.axis != null -> buildString {
                append(",axis:${binding.axis}")
                binding.threshold?.let { append(",threshold:$it") }
                binding.direction?.let { append(",direction:$it") }
            }

            else -> ""
        }
    }

    /** What will display is different from what is saved in this case */
    fun displayValue(binding: Input?): String {
        if (binding?.key != null) {
            return GamepadHelper.getButtonName(binding.key)
        } else if (binding?.axis != null) {
            return GamepadHelper.getAxisName(binding.axis, binding.direction)
        } else {
            return ""
        }
    }

    override val isRuntimeEditable: Boolean = true

    companion object {

        fun from(key: String): InputMappingSetting? = values().firstOrNull { it.key == key }
    }
}
