// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

package org.citra.citra_emu.features.settings.model

import androidx.annotation.StringRes
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.features.input.Hotkey
import org.citra.citra_emu.features.input.InputOverlayElement
import org.citra.citra_emu.features.input.InputOverlaySpec
import org.citra.citra_emu.features.settings.SettingKeys

enum class InputOverlaySetting(
    override val key: String,
    override val section: String,
    @StringRes val nameId: Int,
    /** This defines what the element *does* and its appearance, which is not user-editable */
    val spec: InputOverlaySpec,
    /** Defines the defaults for the user-editable parts of the control.*/
    override val defaultValue: InputOverlayElement

) : AbstractSetting<InputOverlayElement?> {
    OVERLAY_BUTTON_A(
        SettingKeys.overlay_button_a(),
        CitraSettings.SECTION_OVERLAY,
        R.string.button_a,
        InputOverlaySpec.Button(
            button = NativeLibrary.ButtonType.BUTTON_A,
            buttonResId = R.drawable.button_a,
            pressedButtonResId = R.drawable.button_a_pressed
        ),
        InputOverlayElement(
            visible = true,
            portraitX = 810,
            portraitY = 850,
            landscapeX = 930,
            landscapeY = 620,
            size = 110
        )
    ),
    OVERLAY_BUTTON_B(
        SettingKeys.overlay_button_b(),
        CitraSettings.SECTION_OVERLAY,
        R.string.button_b,
        InputOverlaySpec.Button(
            button = NativeLibrary.ButtonType.BUTTON_B,
            buttonResId = R.drawable.button_b,
            pressedButtonResId = R.drawable.button_b_pressed
        ),
        InputOverlayElement(
            visible = true,
            portraitX = 710,
            portraitY = 905,
            landscapeX = 870,
            landscapeY = 720,
            size = 110
        )
    ),
    OVERLAY_BUTTON_X(
        SettingKeys.overlay_button_x(),
        CitraSettings.SECTION_OVERLAY,
        R.string.button_x,
        InputOverlaySpec.Button(
            button = NativeLibrary.ButtonType.BUTTON_X,
            buttonResId = R.drawable.button_x,
            pressedButtonResId = R.drawable.button_x_pressed
        ),
        InputOverlayElement(
            visible = true,
            portraitX = 710,
            portraitY = 795,
            landscapeX = 870,
            landscapeY = 520,
            size = 110
        )
    ),
    OVERLAY_BUTTON_Y(
        SettingKeys.overlay_button_y(),
        CitraSettings.SECTION_OVERLAY,
        R.string.button_y,
        InputOverlaySpec.Button(
            button = NativeLibrary.ButtonType.BUTTON_Y,
            buttonResId = R.drawable.button_y,
            pressedButtonResId = R.drawable.button_y_pressed
        ),
        InputOverlayElement(
            visible = true,
            portraitX = 610,
            portraitY = 850,
            landscapeX = 810,
            landscapeY = 620,
            size = 110
        )
    ),
    OVERLAY_BUTTON_L(
        SettingKeys.overlay_button_l(),
        CitraSettings.SECTION_OVERLAY,
        R.string.button_l,
        InputOverlaySpec.Button(
            button = NativeLibrary.ButtonType.TRIGGER_L,
            buttonResId = R.drawable.button_l,
            pressedButtonResId = R.drawable.button_l_pressed
        ),
        InputOverlayElement(
            visible = true,
            portraitX = 10,
            portraitY = 640,
            landscapeX = 13,
            landscapeY = 0,
            size = 180
        )
    ),
    OVERLAY_BUTTON_R(
        SettingKeys.overlay_button_r(),
        CitraSettings.SECTION_OVERLAY,
        R.string.button_r,
        InputOverlaySpec.Button(
            button = NativeLibrary.ButtonType.TRIGGER_R,
            buttonResId = R.drawable.button_r,
            pressedButtonResId = R.drawable.button_r_pressed
        ),
        InputOverlayElement(
            visible = true,
            portraitX = 810,
            portraitY = 640,
            landscapeX = 895,
            landscapeY = 0,
            size = 180
        )
    ),
    OVERLAY_BUTTON_ZL(
        SettingKeys.overlay_button_zl(),
        CitraSettings.SECTION_OVERLAY,
        R.string.button_zl,
        InputOverlaySpec.Button(
            button = NativeLibrary.ButtonType.BUTTON_ZL,
            buttonResId = R.drawable.button_zl,
            pressedButtonResId = R.drawable.button_zl_pressed
        ),
        InputOverlayElement(
            visible = false,
            portraitX = 210,
            portraitY = 640,
            landscapeX = 13,
            landscapeY = 110,
            size = 180
        )
    ),
    OVERLAY_BUTTON_ZR(
        SettingKeys.overlay_button_zr(),
        CitraSettings.SECTION_OVERLAY,
        R.string.button_zr,
        InputOverlaySpec.Button(
            button = NativeLibrary.ButtonType.BUTTON_ZR,
            buttonResId = R.drawable.button_zr,
            pressedButtonResId = R.drawable.button_zr_pressed
        ),
        InputOverlayElement(
            visible = false,
            portraitX = 610,
            portraitY = 640,
            landscapeX = 895,
            landscapeY = 110,
            size = 180
        )
    ),
    OVERLAY_BUTTON_START(
        SettingKeys.overlay_button_start(),
        CitraSettings.SECTION_OVERLAY,
        R.string.button_start,
        InputOverlaySpec.Button(
            button = NativeLibrary.ButtonType.BUTTON_START,
            buttonResId = R.drawable.button_start,
            pressedButtonResId = R.drawable.button_start_pressed
        ),
        InputOverlayElement(
            visible = true,
            portraitX = 520,
            portraitY = 794,
            landscapeX = 550,
            landscapeY = 850,
            size = 80
        )
    ),
    OVERLAY_BUTTON_SELECT(
        SettingKeys.overlay_button_select(),
        CitraSettings.SECTION_OVERLAY,
        R.string.button_select,
        InputOverlaySpec.Button(
            button = NativeLibrary.ButtonType.BUTTON_SELECT,
            buttonResId = R.drawable.button_select,
            pressedButtonResId = R.drawable.button_select_pressed
        ),
        InputOverlayElement(
            visible = true,
            portraitX = 400,
            portraitY = 794,
            landscapeX = 470,
            landscapeY = 850,
            size = 80
        )
    ),
    OVERLAY_DPAD(
        SettingKeys.overlay_dpad(),
        CitraSettings.SECTION_OVERLAY,
        R.string.controller_dpad,
        InputOverlaySpec.Dpad(
            up = NativeLibrary.ButtonType.DPAD_UP,
            down = NativeLibrary.ButtonType.DPAD_DOWN,
            left = NativeLibrary.ButtonType.DPAD_LEFT,
            right = NativeLibrary.ButtonType.DPAD_RIGHT,
            dpadResId = R.drawable.dpad,
            pressedOneDirectionResId = R.drawable.dpad_pressed_one_direction,
            pressedTwoDirectionsResId = R.drawable.dpad_pressed_two_directions
        ),
        InputOverlayElement(
            visible = true,
            portraitX = 10,
            portraitY = 730,
            landscapeX = 15,
            landscapeY = 470,
            size = 220
        )
    ),
    OVERLAY_CIRCLEPAD(
        SettingKeys.overlay_circlepad(),
        CitraSettings.SECTION_OVERLAY,
        R.string.controller_circlepad,
        InputOverlaySpec.Joystick(
            joystick = NativeLibrary.ButtonType.STICK_LEFT,
            outerResId = R.drawable.stick_main_range,
            defaultInnerResId = R.drawable.stick_main,
            pressedInnerResId = R.drawable.stick_main_pressed
        ),
        InputOverlayElement(
            visible = true,
            portraitX = 80,
            portraitY = 850,
            landscapeX = 100,
            landscapeY = 670,
            size = 275
        )
    ),
    OVERLAY_STICK_C(
        SettingKeys.overlay_cstick(),
        CitraSettings.SECTION_OVERLAY,
        R.string.controller_c,
        InputOverlaySpec.Joystick(
            joystick = NativeLibrary.ButtonType.STICK_C,
            outerResId = R.drawable.stick_c_range,
            defaultInnerResId = R.drawable.stick_c,
            pressedInnerResId = R.drawable.stick_c_pressed
        ),
        InputOverlayElement(
            visible = false,
            portraitX = 800,
            portraitY = 720,
            landscapeX = 740,
            landscapeY = 770,
            size = 138
        )
    ),
    OVERLAY_BUTTON_HOME(
        SettingKeys.overlay_button_home(),
        CitraSettings.SECTION_OVERLAY,
        R.string.button_home,
        InputOverlaySpec.Button(
            button = NativeLibrary.ButtonType.BUTTON_HOME,
            buttonResId = R.drawable.button_home,
            pressedButtonResId = R.drawable.button_home_pressed
        ),
        InputOverlayElement(
            visible = false,
            portraitX = 460,
            portraitY = 840,
            landscapeX = 510,
            landscapeY = 850,
            size = 80
        )
    ),

    // TODO: Add other hotkeys to the input overlay
    OVERLAY_HOTKEY_SWAP(
        SettingKeys.overlay_hotkey_swap(),
        CitraSettings.SECTION_OVERLAY,
        R.string.button_swap,
        InputOverlaySpec.Button(
            button = Hotkey.SWAP_SCREEN.button,
            buttonResId = R.drawable.button_swap,
            pressedButtonResId = R.drawable.button_swap_pressed
        ),
        InputOverlayElement(
            visible = false,
            portraitX = 460,
            portraitY = 675,
            landscapeX = 370,
            landscapeY = 850,
            size = 80
        )
    ),
    OVERLAY_HOTKEY_TURBO(
        SettingKeys.overlay_hotkey_turbo_toggle(),
        CitraSettings.SECTION_OVERLAY,
        R.string.button_turbo,
        InputOverlaySpec.Button(
            button = Hotkey.TURBO_TOGGLE.button,
            buttonResId = R.drawable.button_turbo,
            pressedButtonResId = R.drawable.button_turbo_pressed
        ),
        InputOverlayElement(
            visible = false,
            portraitX = 453,
            portraitY = 720,
            landscapeX = 630,
            landscapeY = 850,
            size = 100
        )
    ),
    OVERLAY_HOTKEY_COMBO_BUTTON(
        SettingKeys.overlay_hotkey_combo_button(),
        CitraSettings.SECTION_OVERLAY,
        R.string.button_combo,
        InputOverlaySpec.Button(
            button = Hotkey.COMBO_BUTTON.button,
            buttonResId = R.drawable.button_combo,
            pressedButtonResId = R.drawable.button_combo_pressed
        ),
        InputOverlayElement(
            visible = false,
            portraitX = 450,
            portraitY = 925,
            landscapeX = 740,
            landscapeY = 480,
            size = 110
        )
    );

    /**
     * Parse a configuration string into an input overlay element.
     * Sample config string:
     *   "visible:true,portraitX:620,portraitY:620,landscapeX:750,landscapeY:800,size:140,opacity:50"
     *
     * Only the user-editable fields of [InputOverlayElement] are stored. Everything that
     * identifies the control (button codes, drawables) lives in [spec] and is not saved.
     */
    override fun valueFromString(string: String): InputOverlayElement {
        if (string.isBlank()) return defaultValue

        val params = string.split(",")
            .mapNotNull { part ->
                val split = part.split(":", limit = 2)
                if (split.size < 2) null else split[0] to split[1]
            }.toMap()

        return defaultValue.copy(
            visible = params["visible"]?.toBooleanStrictOrNull() ?: defaultValue.visible,
            portraitX = params["portraitX"]?.toIntOrNull() ?: defaultValue.portraitX,
            portraitY = params["portraitY"]?.toIntOrNull() ?: defaultValue.portraitY,
            landscapeX = params["landscapeX"]?.toIntOrNull() ?: defaultValue.landscapeX,
            landscapeY = params["landscapeY"]?.toIntOrNull() ?: defaultValue.landscapeY,
            size = params["size"]?.toIntOrNull() ?: defaultValue.size,
            opacity = params["opacity"]?.toIntOrNull() ?: defaultValue.opacity
        )
    }

    /** Create a configuration string from an input overlay element */
    override fun valueToString(value: InputOverlayElement?): String {
        value ?: return ""
        if (defaultValue == value) return ""
        return "visible:${value.visible}" +
            ",portraitX:${value.portraitX}" +
            ",portraitY:${value.portraitY}" +
            ",landscapeX:${value.landscapeX}" +
            ",landscapeY:${value.landscapeY}" +
            ",size:${value.size}" +
            ",opacity:${value.opacity}"
    }

    override val isRuntimeEditable: Boolean = true

    companion object {
        fun from(key: String): InputOverlaySetting? = entries.firstOrNull { it.key == key }
    }
}
