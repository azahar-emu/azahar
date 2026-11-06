// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model

import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.features.input.Hotkey
import org.citra.citra_emu.features.input.InputOverlayElement
import org.citra.citra_emu.features.input.InputOverlayType
import org.citra.citra_emu.features.settings.SettingKeys

enum class InputOverlaySetting(
    override val key: String,
    override val section: String,
    val type: InputOverlayType,
    override val defaultValue: InputOverlayElement
) : AbstractSetting<InputOverlayElement?> {
    OVERLAY_BUTTON_A(
        SettingKeys.overlay_button_a(),
        CitraSettings.SECTION_OVERLAY,
        InputOverlayType.BUTTON,
        InputOverlayElement(
            visible = true,
            portraitX = 810, portraitY = 850,
            landscapeX = 930, landscapeY = 620,
            size = 110,
            button = NativeLibrary.ButtonType.BUTTON_A,
            buttonResID = R.drawable.button_a,
            pressedButtonResID = R.drawable.button_a_pressed
        )
    ),
    OVERLAY_BUTTON_B(
        SettingKeys.overlay_button_b(),
        CitraSettings.SECTION_OVERLAY,
        InputOverlayType.BUTTON,
        InputOverlayElement(
            visible = true,
            portraitX = 710, portraitY = 905,
            landscapeX = 870, landscapeY = 720,
            size = 110,
            button = NativeLibrary.ButtonType.BUTTON_B,
            buttonResID = R.drawable.button_b,
            pressedButtonResID = R.drawable.button_b_pressed
        )
    ),
    OVERLAY_BUTTON_X(
        SettingKeys.overlay_button_x(),
        CitraSettings.SECTION_OVERLAY,
        InputOverlayType.BUTTON,
        InputOverlayElement(
            visible = true,
            portraitX = 710, portraitY = 795,
            landscapeX = 870, landscapeY = 520,
            size = 110,
            button = NativeLibrary.ButtonType.BUTTON_X,
            buttonResID = R.drawable.button_x,
            pressedButtonResID = R.drawable.button_x_pressed
        )
    ),
    OVERLAY_BUTTON_Y(
        SettingKeys.overlay_button_y(),
        CitraSettings.SECTION_OVERLAY,
        InputOverlayType.BUTTON,
        InputOverlayElement(
            visible = true,
            portraitX = 610, portraitY = 850,
            landscapeX = 810, landscapeY = 620,
            size = 110,
            button = NativeLibrary.ButtonType.BUTTON_Y,
            buttonResID = R.drawable.button_y,
            pressedButtonResID = R.drawable.button_y_pressed
        )
    ),
    OVERLAY_BUTTON_L(
        SettingKeys.overlay_button_l(),
        CitraSettings.SECTION_OVERLAY,
        InputOverlayType.BUTTON,
        InputOverlayElement(
            visible = true,
            portraitX = 10, portraitY = 640,
            landscapeX = 13, landscapeY = 0,
            size = 180,
            button = NativeLibrary.ButtonType.TRIGGER_L,
            buttonResID = R.drawable.button_l,
            pressedButtonResID = R.drawable.button_l_pressed
        )
    ),
    OVERLAY_BUTTON_R(
        SettingKeys.overlay_button_r(),
        CitraSettings.SECTION_OVERLAY,
        InputOverlayType.BUTTON,
        InputOverlayElement(
            visible = true,
            portraitX = 810, portraitY = 640,
            landscapeX = 895, landscapeY = 0,
            size = 180,
            button = NativeLibrary.ButtonType.TRIGGER_R,
            buttonResID = R.drawable.button_r,
            pressedButtonResID = R.drawable.button_r_pressed
        )
    ),
    OVERLAY_BUTTON_ZL(
        SettingKeys.overlay_button_zl(),
        CitraSettings.SECTION_OVERLAY,
        InputOverlayType.BUTTON,
        InputOverlayElement(
            visible = false,
            portraitX = 210, portraitY = 640,
            landscapeX = 13, landscapeY = 110,
            size = 180,
            button = NativeLibrary.ButtonType.BUTTON_ZL,
            buttonResID = R.drawable.button_zl,
            pressedButtonResID = R.drawable.button_zl_pressed
        )
    ),
    OVERLAY_BUTTON_ZR(
        SettingKeys.overlay_button_zr(),
        CitraSettings.SECTION_OVERLAY,
        InputOverlayType.BUTTON,
        InputOverlayElement(
            visible = false,
            portraitX = 610, portraitY = 640,
            landscapeX = 895, landscapeY = 110,
            size = 180,
            button = NativeLibrary.ButtonType.BUTTON_ZR,
            buttonResID = R.drawable.button_zr,
            pressedButtonResID = R.drawable.button_zr_pressed
        )
    ),
    OVERLAY_BUTTON_START(
        SettingKeys.overlay_button_start(),
        CitraSettings.SECTION_OVERLAY,
        InputOverlayType.BUTTON,
        InputOverlayElement(
            visible = true,
            portraitX = 520, portraitY = 794,
            landscapeX = 550, landscapeY = 850,
            size = 80,
            button = NativeLibrary.ButtonType.BUTTON_START,
            buttonResID = R.drawable.button_start,
            pressedButtonResID = R.drawable.button_start_pressed
        )
    ),
    OVERLAY_BUTTON_SELECT(
        SettingKeys.overlay_button_select(),
        CitraSettings.SECTION_OVERLAY,
        InputOverlayType.BUTTON,
        InputOverlayElement(
            visible = true,
            portraitX = 400, portraitY = 794,
            landscapeX = 470, landscapeY = 850,
            size = 80,
            button = NativeLibrary.ButtonType.BUTTON_SELECT,
            buttonResID = R.drawable.button_select,
            pressedButtonResID = R.drawable.button_select_pressed
        )
    ),
    OVERLAY_DPAD(
        SettingKeys.overlay_dpad(),
        CitraSettings.SECTION_OVERLAY,
        InputOverlayType.DPAD,
        InputOverlayElement(
            visible = true,
            portraitX = 10, portraitY = 730,
            landscapeX = 15, landscapeY = 470,
            size = 220,
            upButton = NativeLibrary.ButtonType.DPAD_UP,
            downButton = NativeLibrary.ButtonType.DPAD_DOWN,
            leftButton = NativeLibrary.ButtonType.DPAD_LEFT,
            rightButton = NativeLibrary.ButtonType.DPAD_RIGHT,
            dpadResId = R.drawable.dpad,
            dpadPressedOneDirectionResId = R.drawable.dpad_pressed_one_direction,
            dpadPressedTwoDirectionsRedId = R.drawable.dpad_pressed_two_directions
        )
    ),
    OVERLAY_CIRCLEPAD(
        SettingKeys.overlay_circlepad(),
        CitraSettings.SECTION_OVERLAY,
        InputOverlayType.JOYSTICK,
        InputOverlayElement(
            visible = true,
            portraitX = 80, portraitY = 850,
            landscapeX = 100, landscapeY = 670,
            size = 275,
            joystick = NativeLibrary.ButtonType.STICK_LEFT,
            outerResId = R.drawable.stick_main_range,
            defaultInnerResId = R.drawable.stick_main,
            pressedInnerResId = R.drawable.stick_main_pressed
        )
    ),
    OVERLAY_STICK_C(
        SettingKeys.overlay_cstick(),
        CitraSettings.SECTION_OVERLAY,
        InputOverlayType.JOYSTICK,
        InputOverlayElement(
            visible = false,
            portraitX = 800, portraitY = 720,
            landscapeX = 740, landscapeY = 770,
            size = 138,
            joystick = NativeLibrary.ButtonType.STICK_C,
            outerResId = R.drawable.stick_c_range,
            defaultInnerResId = R.drawable.stick_c,
            pressedInnerResId = R.drawable.stick_c_pressed
        )
    ),
    OVERLAY_BUTTON_HOME(
        SettingKeys.overlay_button_home(),
        CitraSettings.SECTION_OVERLAY,
        InputOverlayType.BUTTON,
        InputOverlayElement(
            visible = false,
            portraitX = 460, portraitY = 840,
            landscapeX = 510, landscapeY = 850,
            size = 80,
            button = NativeLibrary.ButtonType.BUTTON_HOME,
            buttonResID = R.drawable.button_home,
            pressedButtonResID = R.drawable.button_home_pressed
        )
    ),

    // TODO: Add other hotkeys to the input overlay
    OVERLAY_HOTKEY_SWAP(
        SettingKeys.overlay_hotkey_swap(),
        CitraSettings.SECTION_OVERLAY,
        InputOverlayType.BUTTON,
        InputOverlayElement(
            visible = false,
            portraitX = 460, portraitY = 675,
            landscapeX = 370, landscapeY = 850,
            size = 80,
            button = NativeLibrary.ButtonType.BUTTON_SWAP,
            buttonResID = R.drawable.button_swap,
            pressedButtonResID = R.drawable.button_swap_pressed
        )
    ),
    OVERLAY_HOTKEY_TURBO(
        SettingKeys.overlay_hotkey_turbo_toggle(),
        CitraSettings.SECTION_OVERLAY,
        InputOverlayType.BUTTON,
        InputOverlayElement(
            visible = false,
            portraitX = 453, portraitY = 720,
            landscapeX = 630, landscapeY = 850,
            size = 100,
            button = NativeLibrary.ButtonType.BUTTON_TURBO,
            buttonResID = R.drawable.button_turbo,
            pressedButtonResID = R.drawable.button_turbo_pressed
        )
    ),
    OVERLAY_HOTKEY_COMBO_BUTTON(
        SettingKeys.overlay_hotkey_combo_button(),
        CitraSettings.SECTION_OVERLAY,
        InputOverlayType.BUTTON,
        InputOverlayElement(
            visible = false,
            portraitX = 450, portraitY = 925,
            landscapeX = 740, landscapeY = 480,
            size = 110,
            button = Hotkey.COMBO_BUTTON.button,
            buttonResID = R.drawable.button_combo,
            pressedButtonResID = R.drawable.button_combo_pressed
        )
    );

    /** Parse a configuration string into an input binding
     *  Sample config string for an input overlay element:
     *    "visible:true,portraitX:620,portraitY:620,landscapeX:750,landscapeY:800,size:140"
     *
     *   For now, none of the other element of InputOverlayElement are editiable in the config
     *   and are established by the defaults above. This could change if we ever implement
     *   overlay themes, for example.
     *     */
    override fun valueFromString(string: String): InputOverlayElement {
        if (string.isBlank()) return defaultValue

        val params = string.split(",")
            .mapNotNull { part ->
                val split = part.split(":", limit = 2)
                if (split.size < 2) null else split[0] to split[1]
            }.toMap()
        val px = params["portraitX"]?.toIntOrNull() ?: defaultValue.portraitX
        val py = params["portraitY"]?.toIntOrNull() ?: defaultValue.portraitY
        val lx = params["landscapeX"]?.toIntOrNull() ?: defaultValue.landscapeX
        val ly = params["landscapeY"]?.toIntOrNull() ?: defaultValue.landscapeX
        val sz = params["size"]?.toIntOrNull() ?: defaultValue.size
        val vis = params["visible"]?.toBooleanStrictOrNull() ?: defaultValue.visible
        return defaultValue.copy(vis, px, py, lx, ly, sz)
    }

    /** Create a configuration string from an input binding */
    override fun valueToString(binding: InputOverlayElement?): String {
        binding ?: return ""
        if (defaultValue == binding) return ""
        return "portraitX:${binding.portraitX}" +
            ",portraitY:${binding.portraitY}" +
            ",landscapeX:${binding.landscapeX}" +
            ",landscapeY:${binding.landscapeY}" +
            ",size:${binding.size}"
    }

    override val isRuntimeEditable: Boolean = true

    companion object {

        fun from(key: String): InputOverlaySetting? = values().firstOrNull { it.key == key }
    }
}
