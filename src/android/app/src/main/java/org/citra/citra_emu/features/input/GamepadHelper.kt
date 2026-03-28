package org.citra.citra_emu.features.input

import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.features.settings.model.InputMappingSetting
import org.citra.citra_emu.features.settings.model.Settings

object GamepadHelper {
    private const val BUTTON_NAME_L3 = "Button L3"
    private const val BUTTON_NAME_R3 = "Button R3"
    private const val NINTENDO_VENDOR_ID = 0x057e

    // Linux BTN_DPAD_* values (0x220-0x223). Joy-Con D-pad buttons arrive as
    // KEYCODE_UNKNOWN with these scan codes because Android's input layer doesn't
    // translate them to KEYCODE_DPAD_*. translateEventToKeyId() falls back to
    // the scan code in that case.
    private const val LINUX_BTN_DPAD_UP = 0x220    // 544
    private const val LINUX_BTN_DPAD_DOWN = 0x221  // 545
    private const val LINUX_BTN_DPAD_LEFT = 0x222  // 546
    private const val LINUX_BTN_DPAD_RIGHT = 0x223 // 547


    private val buttonNameOverrides = mapOf(
        KeyEvent.KEYCODE_BUTTON_THUMBL to BUTTON_NAME_L3,
        KeyEvent.KEYCODE_BUTTON_THUMBR to BUTTON_NAME_R3,
        LINUX_BTN_DPAD_UP to "Dpad Up",
        LINUX_BTN_DPAD_DOWN to "Dpad Down",
        LINUX_BTN_DPAD_LEFT to "Dpad Left",
        LINUX_BTN_DPAD_RIGHT to "Dpad Right"
    )

    fun getButtonName(keyCode: Int): String =
        buttonNameOverrides[keyCode]
            ?: toTitleCase(KeyEvent.keyCodeToString(keyCode).removePrefix("KEYCODE_"))

    fun getAxisName(axis: Int, direction: Int?): String = "Axis " + axis + direction?.let { if(it > 0) "+" else "-" }

    private fun toTitleCase(raw: String): String =
        raw.replace("_", " ").lowercase()
            .split(" ").joinToString(" ") { it.replaceFirstChar { c -> c.uppercase() } }

    private data class DefaultButtonMapping(
        val setting: InputMappingSetting,
        val hostKeyCode: Int
    )
    // Auto-map always sets inverted = false. Users needing inverted axes should remap manually.
    private data class DefaultAxisMapping(
        val setting: InputMappingSetting,
        val hostAxis: Int,
        val hostDirection: Int
    )

    private val xboxFaceButtonMappings = listOf(
        DefaultButtonMapping(InputMappingSetting.BUTTON_A, KeyEvent.KEYCODE_BUTTON_B),
        DefaultButtonMapping(InputMappingSetting.BUTTON_B, KeyEvent.KEYCODE_BUTTON_A),
        DefaultButtonMapping(InputMappingSetting.BUTTON_X, KeyEvent.KEYCODE_BUTTON_Y),
        DefaultButtonMapping(InputMappingSetting.BUTTON_Y, KeyEvent.KEYCODE_BUTTON_X)
    )

    private val nintendoFaceButtonMappings = listOf(
        DefaultButtonMapping(InputMappingSetting.BUTTON_A, KeyEvent.KEYCODE_BUTTON_A),
        DefaultButtonMapping(InputMappingSetting.BUTTON_B, KeyEvent.KEYCODE_BUTTON_B),
        DefaultButtonMapping(InputMappingSetting.BUTTON_X, KeyEvent.KEYCODE_BUTTON_X),
        DefaultButtonMapping(InputMappingSetting.BUTTON_Y, KeyEvent.KEYCODE_BUTTON_Y)
    )

    private val commonButtonMappings = listOf(
        DefaultButtonMapping(InputMappingSetting.BUTTON_L, KeyEvent.KEYCODE_BUTTON_L1),
        DefaultButtonMapping(InputMappingSetting.BUTTON_R, KeyEvent.KEYCODE_BUTTON_R1),
        DefaultButtonMapping(InputMappingSetting.BUTTON_ZL, KeyEvent.KEYCODE_BUTTON_L2),
        DefaultButtonMapping(InputMappingSetting.BUTTON_ZR, KeyEvent.KEYCODE_BUTTON_R2),
        DefaultButtonMapping(InputMappingSetting.BUTTON_SELECT, KeyEvent.KEYCODE_BUTTON_SELECT),
        DefaultButtonMapping(InputMappingSetting.BUTTON_START, KeyEvent.KEYCODE_BUTTON_START)
    )

    private val dpadButtonMappings = listOf(
        DefaultButtonMapping(InputMappingSetting.DPAD_UP, KeyEvent.KEYCODE_DPAD_UP),
        DefaultButtonMapping(InputMappingSetting.DPAD_DOWN, KeyEvent.KEYCODE_DPAD_DOWN),
        DefaultButtonMapping(InputMappingSetting.DPAD_LEFT, KeyEvent.KEYCODE_DPAD_LEFT),
        DefaultButtonMapping(InputMappingSetting.DPAD_RIGHT, KeyEvent.KEYCODE_DPAD_RIGHT)
    )

    private val stickAxisMappings = listOf(
        DefaultAxisMapping(InputMappingSetting.CIRCLEPAD_LEFT, MotionEvent.AXIS_X, -1),
        DefaultAxisMapping(InputMappingSetting.CIRCLEPAD_RIGHT, MotionEvent.AXIS_X, 1),
        DefaultAxisMapping(InputMappingSetting.CIRCLEPAD_UP, MotionEvent.AXIS_Y, -1),
        DefaultAxisMapping(InputMappingSetting.CIRCLEPAD_DOWN, MotionEvent.AXIS_Y, 1),
        DefaultAxisMapping(InputMappingSetting.CSTICK_LEFT, MotionEvent.AXIS_Z, -1),
        DefaultAxisMapping(InputMappingSetting.CSTICK_RIGHT, MotionEvent.AXIS_Z, 1),
        DefaultAxisMapping(InputMappingSetting.CSTICK_UP, MotionEvent.AXIS_RZ, -1),
        DefaultAxisMapping(InputMappingSetting.CSTICK_DOWN, MotionEvent.AXIS_RZ, 1)
    )

    private val dpadAxisMappings = listOf(
        DefaultAxisMapping(InputMappingSetting.DPAD_UP, MotionEvent.AXIS_HAT_Y, -1),
        DefaultAxisMapping(InputMappingSetting.DPAD_DOWN, MotionEvent.AXIS_HAT_Y, 1),
        DefaultAxisMapping(InputMappingSetting.DPAD_LEFT, MotionEvent.AXIS_HAT_X, -1),
        DefaultAxisMapping(InputMappingSetting.DPAD_RIGHT, MotionEvent.AXIS_HAT_X, 1)
    )

    // Nintendo Switch Joy-Con specific mappings.
    // Joy-Cons connected via Bluetooth on Android have several quirks:
    // - They register as two separate InputDevices (left and right)
    // - Android's evdev translation swaps A<->B (BTN_EAST->BUTTON_B, BTN_SOUTH->BUTTON_A)
    //   but does NOT swap X<->Y (BTN_NORTH->BUTTON_X, BTN_WEST->BUTTON_Y)
    // - D-pad buttons arrive as KEYCODE_UNKNOWN (0) with Linux BTN_DPAD_* scan codes
    // - Right stick uses AXIS_RX/AXIS_RY instead of AXIS_Z/AXIS_RZ

    // Joy-Con face buttons: A/B are swapped by Android's evdev layer, but X/Y are not.
    // This is different from both the standard Xbox table (full swap) and the
    // Nintendo table (no swap).
    private val joyconFaceButtonMappings = listOf(
        DefaultButtonMapping(InputMappingSetting.BUTTON_A, KeyEvent.KEYCODE_BUTTON_B),
        DefaultButtonMapping(InputMappingSetting.BUTTON_B, KeyEvent.KEYCODE_BUTTON_A),
        DefaultButtonMapping(InputMappingSetting.BUTTON_X, KeyEvent.KEYCODE_BUTTON_X),
        DefaultButtonMapping(InputMappingSetting.BUTTON_Y, KeyEvent.KEYCODE_BUTTON_Y)
    )

    // Joy-Con D-pad: uses Linux scan codes because Android reports BTN_DPAD_* as KEYCODE_UNKNOWN
    private val joyconDpadButtonMappings = listOf(
        DefaultButtonMapping(InputMappingSetting.DPAD_UP, LINUX_BTN_DPAD_UP),
        DefaultButtonMapping(InputMappingSetting.DPAD_DOWN, LINUX_BTN_DPAD_DOWN),
        DefaultButtonMapping(InputMappingSetting.DPAD_LEFT, LINUX_BTN_DPAD_LEFT),
        DefaultButtonMapping(InputMappingSetting.DPAD_RIGHT, LINUX_BTN_DPAD_RIGHT)
    )

    // Joy-Con sticks: left stick is AXIS_X/Y (standard), right stick is AXIS_RX/RY
    // (not Z/RZ like most controllers). The horizontal axis is inverted relative to
    // the standard orientation - verified empirically on paired Joy-Cons via Bluetooth.
    private val joyconStickAxisMappings = listOf(
        DefaultAxisMapping(InputMappingSetting.CIRCLEPAD_UP, MotionEvent.AXIS_Y, -1),
        DefaultAxisMapping(InputMappingSetting.CIRCLEPAD_DOWN, MotionEvent.AXIS_Y, 1),
        DefaultAxisMapping(InputMappingSetting.CIRCLEPAD_LEFT, MotionEvent.AXIS_X, -1),
        DefaultAxisMapping(InputMappingSetting.CIRCLEPAD_RIGHT, MotionEvent.AXIS_X, 1),
        DefaultAxisMapping(InputMappingSetting.CSTICK_UP, MotionEvent.AXIS_RY, -1),
        DefaultAxisMapping(InputMappingSetting.CSTICK_DOWN, MotionEvent.AXIS_RY, 1),
        DefaultAxisMapping(InputMappingSetting.CSTICK_LEFT, MotionEvent.AXIS_RX, 1),
        DefaultAxisMapping(InputMappingSetting.CSTICK_RIGHT, MotionEvent.AXIS_RX, -1)
    )

    /**
     * Detects whether a device is a Nintendo Switch Joy-Con (as opposed to a
     * Pro Controller or other Nintendo device) by checking vendor ID + device
     * capabilities. Joy-Cons lack AXIS_HAT_X/Y and use AXIS_RX/RY for the
     * right stick, while the Pro Controller has standard HAT axes and Z/RZ.
     */
    fun isJoyCon(device: InputDevice?): Boolean {
        if (device == null) return false
        if (device.vendorId != NINTENDO_VENDOR_ID) return false

        // Pro Controllers have HAT_X/HAT_Y (D-pad) and Z/RZ (right stick).
        // Joy-Cons lack both: no HAT axes, right stick on RX/RY instead of Z/RZ.
        var hasHatAxes = false
        var hasStandardRightStick = false
        for (range in device.motionRanges) {
            when (range.axis) {
                MotionEvent.AXIS_HAT_X, MotionEvent.AXIS_HAT_Y -> hasHatAxes = true
                MotionEvent.AXIS_Z, MotionEvent.AXIS_RZ -> hasStandardRightStick = true
            }
        }
        return !hasHatAxes && !hasStandardRightStick
    }

    fun clearAllBindings(settings: Settings) {
        InputMappingSetting.values().forEach { settings.set(it, Input()) }
    }

    private fun applyBindings(
        settings: Settings,
        buttonMappings: List<DefaultButtonMapping>,
        axisMappings: List<DefaultAxisMapping>
    ) {

        buttonMappings.forEach { mapping ->
            settings.set(mapping.setting, Input(key = mapping.hostKeyCode))
        }
        axisMappings.forEach { mapping ->
            settings.set(
                mapping.setting,
                Input(
                    axis = mapping.hostAxis,
                    direction = mapping.hostDirection
                )
            )
        }
    }

    /**
     * Applies Joy-Con specific bindings: scan code D-pad, partial face button
     * swap, and AXIS_RX/RY right stick.
     */
    fun applyJoyConBindings(settings: Settings) {
        applyBindings(
            settings,
            joyconFaceButtonMappings + commonButtonMappings + joyconDpadButtonMappings,
            joyconStickAxisMappings
        )
    }

    /**
     * Applies auto-mapped bindings based on detected controller layout and d-pad type.
     *
     * @param isNintendoLayout true if the controller uses Nintendo face button layout
     *   (A=east, B=south), false for Xbox layout (A=south, B=east)
     * @param useAxisDpad true if the d-pad should be mapped as axis (HAT_X/HAT_Y),
     *   false if it should be mapped as individual button keycodes (DPAD_UP/DOWN/LEFT/RIGHT)
     */
    fun applyAutoMapBindings(settings: Settings, isNintendoLayout: Boolean, useAxisDpad: Boolean) {
        val faceButtons = if (isNintendoLayout) nintendoFaceButtonMappings else xboxFaceButtonMappings
        val buttonMappings = if (useAxisDpad) {
            faceButtons + commonButtonMappings
        } else {
            faceButtons + commonButtonMappings + dpadButtonMappings
        }
        val axisMappings = if (useAxisDpad) {
            stickAxisMappings + dpadAxisMappings
        } else {
            stickAxisMappings
        }
        applyBindings(settings,buttonMappings, axisMappings)
    }

    /**
     * Some controllers report extra button presses that can be ignored.
     */
    fun shouldKeyBeIgnored(inputDevice: InputDevice, keyCode: Int): Boolean {
        return if (isDualShock4(inputDevice)) {
            // The two analog triggers generate analog motion events as well as a keycode.
            // We always prefer to use the analog values, so throw away the button press
            keyCode == KeyEvent.KEYCODE_BUTTON_L2 || keyCode == KeyEvent.KEYCODE_BUTTON_R2
        } else false
    }

    /**
     * Scale an axis to be zero-centered with a proper range.
     */
    fun scaleAxis(inputDevice: InputDevice, axis: Int, value: Float): Float {
        if (isDualShock4(inputDevice)) {
            // Android doesn't have correct mappings for this controller's triggers. It reports them
            // as RX & RY, centered at -1.0, and with a range of [-1.0, 1.0]
            // Scale them to properly zero-centered with a range of [0.0, 1.0].
            if (axis == MotionEvent.AXIS_RX || axis == MotionEvent.AXIS_RY) {
                return (value + 1) / 2.0f
            }
        } else if (isXboxOneWireless(inputDevice)) {
            // Same as the DualShock 4, the mappings are missing.
            if (axis == MotionEvent.AXIS_Z || axis == MotionEvent.AXIS_RZ) {
                return (value + 1) / 2.0f
            }
            if (axis == MotionEvent.AXIS_GENERIC_1) {
                // This axis is stuck at ~.5. Ignore it.
                return 0.0f
            }
        } else if (isMogaPro2Hid(inputDevice)) {
            // This controller has a broken axis that reports a constant value. Ignore it.
            if (axis == MotionEvent.AXIS_GENERIC_1) {
                return 0.0f
            }
        }
        return value
    }

    private fun isDualShock4(inputDevice: InputDevice): Boolean {
        // Sony DualShock 4 controller
        return inputDevice.vendorId == 0x54c && inputDevice.productId == 0x9cc
    }

    private fun isXboxOneWireless(inputDevice: InputDevice): Boolean {
        // Microsoft Xbox One controller
        return inputDevice.vendorId == 0x45e && inputDevice.productId == 0x2e0
    }

    private fun isMogaPro2Hid(inputDevice: InputDevice): Boolean {
        // Moga Pro 2 HID
        return inputDevice.vendorId == 0x20d6 && inputDevice.productId == 0x6271
    }

    data class JoystickComponent(val joystickType: Int, val isVertical: Boolean)

    fun getJoystickComponent(buttonType: Int): JoystickComponent? = when (buttonType) {
        NativeLibrary.ButtonType.STICK_LEFT_UP,
        NativeLibrary.ButtonType.STICK_LEFT_DOWN -> JoystickComponent(NativeLibrary.ButtonType.STICK_LEFT, true)
        NativeLibrary.ButtonType.STICK_LEFT_LEFT,
        NativeLibrary.ButtonType.STICK_LEFT_RIGHT -> JoystickComponent(NativeLibrary.ButtonType.STICK_LEFT, false)
        NativeLibrary.ButtonType.STICK_C_UP,
        NativeLibrary.ButtonType.STICK_C_DOWN -> JoystickComponent(NativeLibrary.ButtonType.STICK_C, true)
        NativeLibrary.ButtonType.STICK_C_LEFT,
        NativeLibrary.ButtonType.STICK_C_RIGHT -> JoystickComponent(NativeLibrary.ButtonType.STICK_C, false)
        else -> null
    }

    val buttonKeys = listOf(
        InputMappingSetting.BUTTON_A,
        InputMappingSetting.BUTTON_B,
        InputMappingSetting.BUTTON_X,
        InputMappingSetting.BUTTON_Y,
        InputMappingSetting.BUTTON_SELECT,
        InputMappingSetting.BUTTON_START,
        InputMappingSetting.BUTTON_HOME
    )
    val buttonTitles = listOf(
        R.string.button_a,
        R.string.button_b,
        R.string.button_x,
        R.string.button_y,
        R.string.button_select,
        R.string.button_start,
        R.string.button_home
    )
    val circlePadKeys = listOf(
        InputMappingSetting.CIRCLEPAD_UP,
        InputMappingSetting.CIRCLEPAD_DOWN,
        InputMappingSetting.CIRCLEPAD_LEFT,
        InputMappingSetting.CIRCLEPAD_RIGHT
    )
    val cStickKeys = listOf(
        InputMappingSetting.CSTICK_UP,
        InputMappingSetting.CSTICK_DOWN,
        InputMappingSetting.CSTICK_LEFT,
        InputMappingSetting.CSTICK_RIGHT
    )

    val dPadButtonKeys = listOf(
        InputMappingSetting.DPAD_UP,
        InputMappingSetting.DPAD_DOWN,
        InputMappingSetting.DPAD_LEFT,
        InputMappingSetting.DPAD_RIGHT
    )

    val dPadTitles = listOf(
        R.string.direction_up,
        R.string.direction_down,
        R.string.direction_left,
        R.string.direction_right
    )
    val axisTitles = dPadTitles

    val triggerKeys = listOf(
        InputMappingSetting.BUTTON_L,
        InputMappingSetting.BUTTON_R,
        InputMappingSetting.BUTTON_ZL,
        InputMappingSetting.BUTTON_ZR
    )
    val triggerTitles = listOf(
        R.string.button_l,
        R.string.button_r,
        R.string.button_zl,
        R.string.button_zr
    )
    val hotKeys = listOf(
        InputMappingSetting.HOTKEY_ENABLE,
        InputMappingSetting.HOTKEY_SWAP,
        InputMappingSetting.HOTKEY_CYCLE_LAYOUT,
        InputMappingSetting.HOTKEY_CLOSE_GAME,
        InputMappingSetting.HOTKEY_PAUSE_OR_RESUME,
        InputMappingSetting.HOTKEY_QUICKSAVE,
        InputMappingSetting.HOTKEY_QUICKLOAD,
        InputMappingSetting.HOTKEY_TURBO_LIMIT
    )
    val hotkeyTitles = listOf(
        R.string.controller_hotkey_enable_button,
        R.string.emulation_swap_screens,
        R.string.emulation_cycle_landscape_layouts,
        R.string.emulation_close_game,
        R.string.emulation_toggle_pause,
        R.string.emulation_quicksave,
        R.string.emulation_quickload,
        R.string.turbo_limit_hotkey
    )

}