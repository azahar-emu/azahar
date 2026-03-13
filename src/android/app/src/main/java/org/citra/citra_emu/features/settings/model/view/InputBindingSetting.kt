// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model.view

import android.content.Context
import android.content.SharedPreferences
import android.view.InputDevice
import android.view.InputDevice.MotionRange
import android.view.KeyEvent
import android.view.MotionEvent
import android.widget.Toast
import androidx.preference.PreferenceManager
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.features.hotkeys.Hotkey
import org.citra.citra_emu.features.settings.model.AbstractSetting
import org.citra.citra_emu.features.settings.model.AbstractStringSetting
import org.citra.citra_emu.features.settings.model.Settings

class InputBindingSetting(
    val abstractSetting: AbstractSetting,
    titleId: Int
) : SettingsItem(abstractSetting, titleId, 0) {
    private val context: Context get() = CitraApplication.appContext
    private val preferences: SharedPreferences
        get() = PreferenceManager.getDefaultSharedPreferences(context)

    var value: String
        get() = preferences.getString(abstractSetting.key, "")!!
        set(string) {
            preferences.edit()
                .putString(abstractSetting.key, string)
                .apply()
        }

    /**
     * Returns true if this key is for the 3DS Circle Pad
     */
    fun isCirclePad(): Boolean =
        when (abstractSetting.key) {
            Settings.KEY_CIRCLEPAD_AXIS_HORIZONTAL,
            Settings.KEY_CIRCLEPAD_AXIS_VERTICAL -> true

            else -> false
        }

    /**
     * Returns true if this key is for a horizontal axis for a 3DS analog stick or D-pad
     */
    fun isHorizontalOrientation(): Boolean =
        when (abstractSetting.key) {
            Settings.KEY_CIRCLEPAD_AXIS_HORIZONTAL,
            Settings.KEY_CSTICK_AXIS_HORIZONTAL,
            Settings.KEY_DPAD_AXIS_HORIZONTAL -> true

            else -> false
        }

    /**
     * Returns true if this key is for the 3DS C-Stick
     */
    fun isCStick(): Boolean =
        when (abstractSetting.key) {
            Settings.KEY_CSTICK_AXIS_HORIZONTAL,
            Settings.KEY_CSTICK_AXIS_VERTICAL -> true

            else -> false
        }

    /**
     * Returns true if this key is for the 3DS D-Pad
     */
    fun isDPad(): Boolean =
        when (abstractSetting.key) {
            Settings.KEY_DPAD_AXIS_HORIZONTAL,
            Settings.KEY_DPAD_AXIS_VERTICAL -> true

            else -> false
        }
    /**
     * Returns true if this key is for the 3DS L/R or ZL/ZR buttons. Note, these are not real
     * triggers on the 3DS, but we support them as such on a physical gamepad.
     */
    fun isTrigger(): Boolean =
        when (abstractSetting.key) {
            Settings.KEY_BUTTON_L,
            Settings.KEY_BUTTON_R,
            Settings.KEY_BUTTON_ZL,
            Settings.KEY_BUTTON_ZR -> true

            else -> false
        }

    /**
     * Returns true if a gamepad axis can be used to map this key.
     */
    fun isAxisMappingSupported(): Boolean {
        return isCirclePad() || isCStick() || isDPad() || isTrigger()
    }

    /**
     * Returns true if a gamepad button can be used to map this key.
     */
    fun isButtonMappingSupported(): Boolean {
        return !isAxisMappingSupported() || isTrigger()
    }

    /**
     * Returns the Citra button code for the settings key.
     */
    private val buttonCode: Int
        get() =
            when (abstractSetting.key) {
                Settings.KEY_BUTTON_A -> NativeLibrary.ButtonType.BUTTON_A
                Settings.KEY_BUTTON_B -> NativeLibrary.ButtonType.BUTTON_B
                Settings.KEY_BUTTON_X -> NativeLibrary.ButtonType.BUTTON_X
                Settings.KEY_BUTTON_Y -> NativeLibrary.ButtonType.BUTTON_Y
                Settings.KEY_BUTTON_L -> NativeLibrary.ButtonType.TRIGGER_L
                Settings.KEY_BUTTON_R -> NativeLibrary.ButtonType.TRIGGER_R
                Settings.KEY_BUTTON_ZL -> NativeLibrary.ButtonType.BUTTON_ZL
                Settings.KEY_BUTTON_ZR -> NativeLibrary.ButtonType.BUTTON_ZR
                Settings.KEY_BUTTON_SELECT -> NativeLibrary.ButtonType.BUTTON_SELECT
                Settings.KEY_BUTTON_START -> NativeLibrary.ButtonType.BUTTON_START
                Settings.KEY_BUTTON_HOME -> NativeLibrary.ButtonType.BUTTON_HOME
                Settings.KEY_BUTTON_UP -> NativeLibrary.ButtonType.DPAD_UP
                Settings.KEY_BUTTON_DOWN -> NativeLibrary.ButtonType.DPAD_DOWN
                Settings.KEY_BUTTON_LEFT -> NativeLibrary.ButtonType.DPAD_LEFT
                Settings.KEY_BUTTON_RIGHT -> NativeLibrary.ButtonType.DPAD_RIGHT
                Settings.HOTKEY_ENABLE -> Hotkey.ENABLE.button
                Settings.HOTKEY_SCREEN_SWAP -> Hotkey.SWAP_SCREEN.button
                Settings.HOTKEY_CYCLE_LAYOUT -> Hotkey.CYCLE_LAYOUT.button
                Settings.HOTKEY_CLOSE_GAME -> Hotkey.CLOSE_GAME.button
                Settings.HOTKEY_PAUSE_OR_RESUME -> Hotkey.PAUSE_OR_RESUME.button
                Settings.HOTKEY_QUICKSAVE -> Hotkey.QUICKSAVE.button
                Settings.HOTKEY_QUICKlOAD -> Hotkey.QUICKLOAD.button
                Settings.HOTKEY_TURBO_LIMIT -> Hotkey.TURBO_LIMIT.button
                else -> -1
            }

    /**
     * Returns the key used to lookup the reverse mapping for this key, which is used to cleanup old
     * settings on re-mapping or clearing of a setting.
     */
    private val reverseKey: String
        get() {
            var reverseKey = "${INPUT_MAPPING_PREFIX}_ReverseMapping_${abstractSetting.key}"
            if (isAxisMappingSupported() && !isTrigger()) {
                // Triggers are the only axis-supported mappings without orientation
                reverseKey += "_" + if (isHorizontalOrientation()) {
                    0
                } else {
                    1
                }
            }
            return reverseKey
        }

    /**
     * Removes the old mapping for this key from the settings, e.g. on user clearing the setting.
     */
    fun removeOldMapping() {
        // Try remove all possible keys we wrote for this setting
        val oldKey = preferences.getString(reverseKey, "")
        if (oldKey != "") {
            (setting as AbstractStringSetting).string = ""
            preferences.edit()
                .remove(abstractSetting.key) // Used for ui text
                .remove(oldKey + "_GuestOrientation") // Used for axis orientation
                .remove(oldKey + "_GuestButton") // Used for axis button
                .remove(oldKey + "_Inverted") // used for axis inversion
                .remove(reverseKey)
            val buttonCodes = try {
                preferences.getStringSet(oldKey, mutableSetOf<String>())!!.toMutableSet()
            } catch (e: ClassCastException) {
                // if this is an int pref, either old button or an axis, so just remove it
                preferences.edit().remove(oldKey).apply()
                return;
            }
            buttonCodes.remove(buttonCode.toString());
            preferences.edit().putStringSet(oldKey,buttonCodes).apply()
        }
    }

    /**
     * Helper function to write a gamepad button mapping for the setting.
     */
    private fun writeButtonMapping(keyEvent: KeyEvent) {
        val editor = preferences.edit()
        val key = getInputButtonKey(keyEvent)
        // Pull in all codes associated with this key
        // Migrate from the old int preference if need be
        val buttonCodes = InputBindingSetting.getButtonSet(keyEvent)
        buttonCodes.add(buttonCode)
        // Cleanup old mapping for this setting
        removeOldMapping()

        editor.putStringSet(key, buttonCodes.mapTo(mutableSetOf()) {it.toString()})

        // Write next reverse mapping for future cleanup
        editor.putString(reverseKey, key)

        // Apply changes
        editor.apply()
    }

    /**
     * Helper function to write a gamepad axis mapping for the setting.
     */
    private fun writeAxisMapping(axis: Int, value: Int, inverted: Boolean) {
        // Cleanup old mapping
        removeOldMapping()

        // Write new mapping
        preferences.edit()
            .putInt(getInputAxisOrientationKey(axis), if (isHorizontalOrientation()) 0 else 1)
            .putInt(getInputAxisButtonKey(axis), value)
            .putBoolean(getInputAxisInvertedKey(axis),inverted)
            // Write next reverse mapping for future cleanup
            .putString(reverseKey, getInputAxisKey(axis))
            .apply()
    }

    /**
     * Saves the provided key input setting as an Android preference.
     *
     * @param keyEvent KeyEvent of this key press.
     */
    fun onKeyInput(keyEvent: KeyEvent) {
        if (!isButtonMappingSupported()) {
            Toast.makeText(context, R.string.input_message_analog_only, Toast.LENGTH_LONG).show()
            return
        }

        val code = translateEventToKeyId(keyEvent)
        writeButtonMapping(keyEvent)
        value = "${keyEvent.device.name}: ${getButtonName(code)}"
    }

    /**
     * Saves the provided motion input setting as an Android preference.
     *
     * @param device      InputDevice from which the input event originated.
     * @param motionRange MotionRange of the movement
     * @param axisDir     Either '-' or '+'
     */
    fun onMotionInput(device: InputDevice, motionRange: MotionRange, axisDir: Char) {
        if (!isAxisMappingSupported()) {
            Toast.makeText(context, R.string.input_message_button_only, Toast.LENGTH_LONG).show()
            return
        }
        val button = if (isCirclePad()) {
            NativeLibrary.ButtonType.STICK_LEFT
        } else if (isCStick()) {
            NativeLibrary.ButtonType.STICK_C
        } else if (isDPad()) {
            NativeLibrary.ButtonType.DPAD
        } else {
            buttonCode
        }
        // use UP (-) to map vertical, but use RIGHT (+) to map horizontal
        val inverted = if (isHorizontalOrientation()) axisDir == '-' else axisDir == '+'
        writeAxisMapping(motionRange.axis, button, inverted)
        value = "Axis ${motionRange.axis}$axisDir"
    }

    override val type = TYPE_INPUT_BINDING

    companion object {
        private const val INPUT_MAPPING_PREFIX = "InputMapping"

        private fun toTitleCase(raw: String): String =
            raw.replace("_", " ").lowercase()
                .split(" ").joinToString(" ") { it.replaceFirstChar { c -> c.uppercase() } }

        private const val BUTTON_NAME_L3 = "Button L3"
        private const val BUTTON_NAME_R3 = "Button R3"

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

        private data class DefaultButtonMapping(
            val settingKey: String,
            val hostKeyCode: Int,
            val guestButtonCode: Int
        )
        // Auto-map always sets inverted = false. Users needing inverted axes should remap manually.
        private data class DefaultAxisMapping(
            val settingKey: String,
            val hostAxis: Int,
            val guestButton: Int,
            val orientation: Int,
            val inverted: Boolean
        )

        private val xboxFaceButtonMappings = listOf(
            DefaultButtonMapping(Settings.KEY_BUTTON_A, KeyEvent.KEYCODE_BUTTON_B, NativeLibrary.ButtonType.BUTTON_A),
            DefaultButtonMapping(Settings.KEY_BUTTON_B, KeyEvent.KEYCODE_BUTTON_A, NativeLibrary.ButtonType.BUTTON_B),
            DefaultButtonMapping(Settings.KEY_BUTTON_X, KeyEvent.KEYCODE_BUTTON_Y, NativeLibrary.ButtonType.BUTTON_X),
            DefaultButtonMapping(Settings.KEY_BUTTON_Y, KeyEvent.KEYCODE_BUTTON_X, NativeLibrary.ButtonType.BUTTON_Y)
        )

        private val nintendoFaceButtonMappings = listOf(
            DefaultButtonMapping(Settings.KEY_BUTTON_A, KeyEvent.KEYCODE_BUTTON_A, NativeLibrary.ButtonType.BUTTON_A),
            DefaultButtonMapping(Settings.KEY_BUTTON_B, KeyEvent.KEYCODE_BUTTON_B, NativeLibrary.ButtonType.BUTTON_B),
            DefaultButtonMapping(Settings.KEY_BUTTON_X, KeyEvent.KEYCODE_BUTTON_X, NativeLibrary.ButtonType.BUTTON_X),
            DefaultButtonMapping(Settings.KEY_BUTTON_Y, KeyEvent.KEYCODE_BUTTON_Y, NativeLibrary.ButtonType.BUTTON_Y)
        )

        private val commonButtonMappings = listOf(
            DefaultButtonMapping(Settings.KEY_BUTTON_L, KeyEvent.KEYCODE_BUTTON_L1, NativeLibrary.ButtonType.TRIGGER_L),
            DefaultButtonMapping(Settings.KEY_BUTTON_R, KeyEvent.KEYCODE_BUTTON_R1, NativeLibrary.ButtonType.TRIGGER_R),
            DefaultButtonMapping(Settings.KEY_BUTTON_ZL, KeyEvent.KEYCODE_BUTTON_L2, NativeLibrary.ButtonType.BUTTON_ZL),
            DefaultButtonMapping(Settings.KEY_BUTTON_ZR, KeyEvent.KEYCODE_BUTTON_R2, NativeLibrary.ButtonType.BUTTON_ZR),
            DefaultButtonMapping(Settings.KEY_BUTTON_SELECT, KeyEvent.KEYCODE_BUTTON_SELECT, NativeLibrary.ButtonType.BUTTON_SELECT),
            DefaultButtonMapping(Settings.KEY_BUTTON_START, KeyEvent.KEYCODE_BUTTON_START, NativeLibrary.ButtonType.BUTTON_START)
        )

        private val dpadButtonMappings = listOf(
            DefaultButtonMapping(Settings.KEY_BUTTON_UP, KeyEvent.KEYCODE_DPAD_UP, NativeLibrary.ButtonType.DPAD_UP),
            DefaultButtonMapping(Settings.KEY_BUTTON_DOWN, KeyEvent.KEYCODE_DPAD_DOWN, NativeLibrary.ButtonType.DPAD_DOWN),
            DefaultButtonMapping(Settings.KEY_BUTTON_LEFT, KeyEvent.KEYCODE_DPAD_LEFT, NativeLibrary.ButtonType.DPAD_LEFT),
            DefaultButtonMapping(Settings.KEY_BUTTON_RIGHT, KeyEvent.KEYCODE_DPAD_RIGHT, NativeLibrary.ButtonType.DPAD_RIGHT)
        )

        private val stickAxisMappings = listOf(
            DefaultAxisMapping(Settings.KEY_CIRCLEPAD_AXIS_HORIZONTAL, MotionEvent.AXIS_X, NativeLibrary.ButtonType.STICK_LEFT, 0, false),
            DefaultAxisMapping(Settings.KEY_CIRCLEPAD_AXIS_VERTICAL, MotionEvent.AXIS_Y, NativeLibrary.ButtonType.STICK_LEFT, 1, false),
            DefaultAxisMapping(Settings.KEY_CSTICK_AXIS_HORIZONTAL, MotionEvent.AXIS_Z, NativeLibrary.ButtonType.STICK_C, 0, false),
            DefaultAxisMapping(Settings.KEY_CSTICK_AXIS_VERTICAL, MotionEvent.AXIS_RZ, NativeLibrary.ButtonType.STICK_C, 1, false)
        )

        private val dpadAxisMappings = listOf(
            DefaultAxisMapping(Settings.KEY_DPAD_AXIS_HORIZONTAL, MotionEvent.AXIS_HAT_X, NativeLibrary.ButtonType.DPAD, 0, false),
            DefaultAxisMapping(Settings.KEY_DPAD_AXIS_VERTICAL, MotionEvent.AXIS_HAT_Y, NativeLibrary.ButtonType.DPAD, 1, false)
        )

        // Nintendo Switch Joy-Con specific mappings.
        // Joy-Cons connected via Bluetooth on Android have several quirks:
        // - They register as two separate InputDevices (left and right)
        // - Android's evdev translation swaps A<->B (BTN_EAST->BUTTON_B, BTN_SOUTH->BUTTON_A)
        //   but does NOT swap X<->Y (BTN_NORTH->BUTTON_X, BTN_WEST->BUTTON_Y)
        // - D-pad buttons arrive as KEYCODE_UNKNOWN (0) with Linux BTN_DPAD_* scan codes
        // - Right stick uses AXIS_RX/AXIS_RY instead of AXIS_Z/AXIS_RZ
        private const val NINTENDO_VENDOR_ID = 0x057e

        // Linux BTN_DPAD_* values (0x220-0x223). Joy-Con D-pad buttons arrive as
        // KEYCODE_UNKNOWN with these scan codes because Android's input layer doesn't
        // translate them to KEYCODE_DPAD_*. translateEventToKeyId() falls back to
        // the scan code in that case.
        private const val LINUX_BTN_DPAD_UP = 0x220    // 544
        private const val LINUX_BTN_DPAD_DOWN = 0x221  // 545
        private const val LINUX_BTN_DPAD_LEFT = 0x222  // 546
        private const val LINUX_BTN_DPAD_RIGHT = 0x223 // 547

        // Joy-Con face buttons: A/B are swapped by Android's evdev layer, but X/Y are not.
        // This is different from both the standard Xbox table (full swap) and the
        // Nintendo table (no swap).
        private val joyconFaceButtonMappings = listOf(
            DefaultButtonMapping(Settings.KEY_BUTTON_A, KeyEvent.KEYCODE_BUTTON_B, NativeLibrary.ButtonType.BUTTON_A),
            DefaultButtonMapping(Settings.KEY_BUTTON_B, KeyEvent.KEYCODE_BUTTON_A, NativeLibrary.ButtonType.BUTTON_B),
            DefaultButtonMapping(Settings.KEY_BUTTON_X, KeyEvent.KEYCODE_BUTTON_X, NativeLibrary.ButtonType.BUTTON_X),
            DefaultButtonMapping(Settings.KEY_BUTTON_Y, KeyEvent.KEYCODE_BUTTON_Y, NativeLibrary.ButtonType.BUTTON_Y)
        )

        // Joy-Con D-pad: uses Linux scan codes because Android reports BTN_DPAD_* as KEYCODE_UNKNOWN
        private val joyconDpadButtonMappings = listOf(
            DefaultButtonMapping(Settings.KEY_BUTTON_UP, LINUX_BTN_DPAD_UP, NativeLibrary.ButtonType.DPAD_UP),
            DefaultButtonMapping(Settings.KEY_BUTTON_DOWN, LINUX_BTN_DPAD_DOWN, NativeLibrary.ButtonType.DPAD_DOWN),
            DefaultButtonMapping(Settings.KEY_BUTTON_LEFT, LINUX_BTN_DPAD_LEFT, NativeLibrary.ButtonType.DPAD_LEFT),
            DefaultButtonMapping(Settings.KEY_BUTTON_RIGHT, LINUX_BTN_DPAD_RIGHT, NativeLibrary.ButtonType.DPAD_RIGHT)
        )

        // Joy-Con sticks: left stick is AXIS_X/Y (standard), right stick is AXIS_RX/RY
        // (not Z/RZ like most controllers). The horizontal axis is inverted relative to
        // the standard orientation - verified empirically on paired Joy-Cons via Bluetooth.
        private val joyconStickAxisMappings = listOf(
            DefaultAxisMapping(Settings.KEY_CIRCLEPAD_AXIS_HORIZONTAL, MotionEvent.AXIS_X, NativeLibrary.ButtonType.STICK_LEFT, 0, false),
            DefaultAxisMapping(Settings.KEY_CIRCLEPAD_AXIS_VERTICAL, MotionEvent.AXIS_Y, NativeLibrary.ButtonType.STICK_LEFT, 1, false),
            DefaultAxisMapping(Settings.KEY_CSTICK_AXIS_HORIZONTAL, MotionEvent.AXIS_RX, NativeLibrary.ButtonType.STICK_C, 0, true),
            DefaultAxisMapping(Settings.KEY_CSTICK_AXIS_VERTICAL, MotionEvent.AXIS_RY, NativeLibrary.ButtonType.STICK_C, 1, false)
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

        private val allBindingKeys: Set<String> by lazy {
            (Settings.buttonKeys + Settings.triggerKeys +
                Settings.circlePadKeys + Settings.cStickKeys + Settings.dPadAxisKeys +
                Settings.dPadButtonKeys).toSet()
        }

        fun clearAllBindings() {
            val prefs = PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)
            val editor = prefs.edit()
            val allKeys = prefs.all.keys.toList()
            for (key in allKeys) {
                if (key.startsWith(INPUT_MAPPING_PREFIX) || key in allBindingKeys) {
                    editor.remove(key)
                }
            }
            editor.apply()
        }

        private fun applyBindings(
            buttonMappings: List<DefaultButtonMapping>,
            axisMappings: List<DefaultAxisMapping>
        ) {
            val prefs = PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)
            val editor = prefs.edit()
            buttonMappings.forEach { applyDefaultButtonMapping(editor, it) }
            axisMappings.forEach { applyDefaultAxisMapping(editor, it) }
            editor.apply()
        }

        /**
         * Applies Joy-Con specific bindings: scan code D-pad, partial face button
         * swap, and AXIS_RX/RY right stick.
         */
        fun applyJoyConBindings() {
            applyBindings(
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
        fun applyAutoMapBindings(isNintendoLayout: Boolean, useAxisDpad: Boolean) {
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
            applyBindings(buttonMappings, axisMappings)
        }

        private fun applyDefaultButtonMapping(
            editor: SharedPreferences.Editor,
            mapping: DefaultButtonMapping
        ) {
            val prefKey = getInputButtonKey(mapping.hostKeyCode)
            editor.putInt(prefKey, mapping.guestButtonCode)
            editor.putString(mapping.settingKey, getButtonName(mapping.hostKeyCode))
            editor.putString(
                "${INPUT_MAPPING_PREFIX}_ReverseMapping_${mapping.settingKey}",
                prefKey
            )
        }

        private fun applyDefaultAxisMapping(
            editor: SharedPreferences.Editor,
            mapping: DefaultAxisMapping
        ) {
            val axisKey = getInputAxisKey(mapping.hostAxis)
            editor.putInt(getInputAxisOrientationKey(mapping.hostAxis), mapping.orientation)
            editor.putInt(getInputAxisButtonKey(mapping.hostAxis), mapping.guestButton)
            editor.putBoolean(getInputAxisInvertedKey(mapping.hostAxis), mapping.inverted)
            val dir = if (mapping.orientation == 0) '+' else '-'
            editor.putString(mapping.settingKey, "Axis ${mapping.hostAxis}$dir")
            val reverseKey = "${INPUT_MAPPING_PREFIX}_ReverseMapping_${mapping.settingKey}_${mapping.orientation}"
            editor.putString(reverseKey, axisKey)
        }

        /**
         * Returns the settings key for the specified Citra button code.
         */
        private fun getButtonKey(buttonCode: Int): String =
            when (buttonCode) {
                NativeLibrary.ButtonType.BUTTON_A -> Settings.KEY_BUTTON_A
                NativeLibrary.ButtonType.BUTTON_B -> Settings.KEY_BUTTON_B
                NativeLibrary.ButtonType.BUTTON_X -> Settings.KEY_BUTTON_X
                NativeLibrary.ButtonType.BUTTON_Y -> Settings.KEY_BUTTON_Y
                NativeLibrary.ButtonType.TRIGGER_L -> Settings.KEY_BUTTON_L
                NativeLibrary.ButtonType.TRIGGER_R -> Settings.KEY_BUTTON_R
                NativeLibrary.ButtonType.BUTTON_ZL -> Settings.KEY_BUTTON_ZL
                NativeLibrary.ButtonType.BUTTON_ZR -> Settings.KEY_BUTTON_ZR
                NativeLibrary.ButtonType.BUTTON_SELECT -> Settings.KEY_BUTTON_SELECT
                NativeLibrary.ButtonType.BUTTON_START -> Settings.KEY_BUTTON_START
                NativeLibrary.ButtonType.BUTTON_HOME -> Settings.KEY_BUTTON_HOME
                NativeLibrary.ButtonType.DPAD_UP -> Settings.KEY_BUTTON_UP
                NativeLibrary.ButtonType.DPAD_DOWN -> Settings.KEY_BUTTON_DOWN
                NativeLibrary.ButtonType.DPAD_LEFT -> Settings.KEY_BUTTON_LEFT
                NativeLibrary.ButtonType.DPAD_RIGHT -> Settings.KEY_BUTTON_RIGHT
                else -> ""
            }
        /**
         * Get the mutable set of int button values this key should map to given an event
         */
        fun getButtonSet(keyCode: KeyEvent):MutableSet<Int> {
            val key = getInputButtonKey(keyCode)
            val preferences = PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)
            var buttonCodes = try {
                preferences.getStringSet(key, mutableSetOf<String>())
            } catch (e: ClassCastException) {
                val prefInt = preferences.getInt(key, -1);
                val migratedSet = if (prefInt != -1) {
                    mutableSetOf(prefInt.toString())
                } else {
                    mutableSetOf<String>()
                }
                migratedSet
            }
            if (buttonCodes == null) buttonCodes = mutableSetOf<String>()
            return buttonCodes.mapNotNull { it.toIntOrNull() }.toMutableSet()
        }

        private fun getInputButtonKey(keyId: Int): String = "${INPUT_MAPPING_PREFIX}_HostAxis_${keyId}"

        /** Falls back to the scan code when keyCode is KEYCODE_UNKNOWN. */
        fun getInputButtonKey(event: KeyEvent): String = getInputButtonKey(translateEventToKeyId(event))

        /**
         * Helper function to get the settings key for an gamepad axis.
         */
        fun getInputAxisKey(axis: Int): String = "${INPUT_MAPPING_PREFIX}_HostAxis_${axis}"

        /**
         * Helper function to get the settings key for an gamepad axis button (stick or trigger).
         */
        fun getInputAxisButtonKey(axis: Int): String = "${getInputAxisKey(axis)}_GuestButton"

        /**
         * Helper function to get the settings key for an whether a gamepad axis is inverted.
         */
        fun getInputAxisInvertedKey(axis: Int): String = "${getInputAxisKey(axis)}_Inverted"

        /**
         * Helper function to get the settings key for an gamepad axis orientation.
         */
        fun getInputAxisOrientationKey(axis: Int): String =
            "${getInputAxisKey(axis)}_GuestOrientation"


        /**
         * This function translates a keyEvent into an "keyid"
         * This key id is either the keyCode from the event, or
         * the raw scanCode.
         * Only when the keyCode itself is 0, (so it is an unknown key)
         * we fall back to the raw scan code.
         * This handles keys like the media-keys on google statia-controllers
         * that don't have a conventional "mapping" and report as "unknown"
         */
        fun translateEventToKeyId(event: KeyEvent): Int {
            return if (event.keyCode == 0) {
                event.scanCode
            } else {
                event.keyCode
            }
        }
    }
}
