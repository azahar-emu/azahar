// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.utils

import android.content.Context
import android.content.SharedPreferences
import androidx.preference.PreferenceManager
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.features.settings.model.Settings
import org.citra.citra_emu.utils.Log
import java.io.BufferedReader
import java.io.InputStreamReader

object InputConfigSync {
    private const val INPUT_MAPPING_PREFIX = "InputMapping"

    private val context: Context get() = CitraApplication.appContext
    private val preferences: SharedPreferences
        get() = PreferenceManager.getDefaultSharedPreferences(context)

    // Maps Settings.KEY_* to NativeLibrary.ButtonType codes
    private val buttonKeyToCode = mapOf(
        Settings.KEY_BUTTON_A to NativeLibrary.ButtonType.BUTTON_A,
        Settings.KEY_BUTTON_B to NativeLibrary.ButtonType.BUTTON_B,
        Settings.KEY_BUTTON_X to NativeLibrary.ButtonType.BUTTON_X,
        Settings.KEY_BUTTON_Y to NativeLibrary.ButtonType.BUTTON_Y,
        Settings.KEY_BUTTON_L to NativeLibrary.ButtonType.TRIGGER_L,
        Settings.KEY_BUTTON_R to NativeLibrary.ButtonType.TRIGGER_R,
        Settings.KEY_BUTTON_ZL to NativeLibrary.ButtonType.BUTTON_ZL,
        Settings.KEY_BUTTON_ZR to NativeLibrary.ButtonType.BUTTON_ZR,
        Settings.KEY_BUTTON_SELECT to NativeLibrary.ButtonType.BUTTON_SELECT,
        Settings.KEY_BUTTON_START to NativeLibrary.ButtonType.BUTTON_START,
        Settings.KEY_BUTTON_HOME to NativeLibrary.ButtonType.BUTTON_HOME,
        Settings.KEY_BUTTON_UP to NativeLibrary.ButtonType.DPAD_UP,
        Settings.KEY_BUTTON_DOWN to NativeLibrary.ButtonType.DPAD_DOWN,
        Settings.KEY_BUTTON_LEFT to NativeLibrary.ButtonType.DPAD_LEFT,
        Settings.KEY_BUTTON_RIGHT to NativeLibrary.ButtonType.DPAD_RIGHT
    )

    // Maps Settings.KEY_* to analog codes
    private val analogKeyToCode = mapOf(
        Settings.KEY_CIRCLEPAD_AXIS_HORIZONTAL to NativeLibrary.ButtonType.STICK_LEFT,
        Settings.KEY_CIRCLEPAD_AXIS_VERTICAL to NativeLibrary.ButtonType.STICK_LEFT,
        Settings.KEY_CSTICK_AXIS_HORIZONTAL to NativeLibrary.ButtonType.STICK_C,
        Settings.KEY_CSTICK_AXIS_VERTICAL to NativeLibrary.ButtonType.STICK_C,
        Settings.KEY_DPAD_AXIS_HORIZONTAL to NativeLibrary.ButtonType.DPAD,
        Settings.KEY_DPAD_AXIS_VERTICAL to NativeLibrary.ButtonType.DPAD
    )

    // All button keys
    private val allButtonKeys = listOf(
        Settings.KEY_BUTTON_A, Settings.KEY_BUTTON_B, Settings.KEY_BUTTON_X, Settings.KEY_BUTTON_Y,
        Settings.KEY_BUTTON_L, Settings.KEY_BUTTON_R, Settings.KEY_BUTTON_ZL, Settings.KEY_BUTTON_ZR,
        Settings.KEY_BUTTON_SELECT, Settings.KEY_BUTTON_START, Settings.KEY_BUTTON_HOME,
        Settings.KEY_BUTTON_UP, Settings.KEY_BUTTON_DOWN, Settings.KEY_BUTTON_LEFT, Settings.KEY_BUTTON_RIGHT
    )

    // All axis keys
    private val allAxisKeys = listOf(
        Settings.KEY_CIRCLEPAD_AXIS_HORIZONTAL, Settings.KEY_CIRCLEPAD_AXIS_VERTICAL,
        Settings.KEY_CSTICK_AXIS_HORIZONTAL, Settings.KEY_CSTICK_AXIS_VERTICAL,
        Settings.KEY_DPAD_AXIS_HORIZONTAL, Settings.KEY_DPAD_AXIS_VERTICAL
    )

    fun writeButtonMappingToConfig(settingsKey: String, buttonCode: Int, keyCode: Int) {
        val paramPackage = "engine:gamepad,code:$buttonCode,keycode:$keyCode"
        writeToConfigIni(settingsKey, paramPackage)
        Log.debug("[InputConfigSync] Wrote button mapping: $settingsKey = $paramPackage")
    }

    fun writeAxisMappingToConfig(settingsKey: String, axis: Int, isHorizontal: Boolean) {
        val paramPackage = "engine:gamepad,code:$axis"
        writeToConfigIni(settingsKey, paramPackage)
        Log.debug("[InputConfigSync] Wrote axis mapping: $settingsKey = $paramPackage")
    }

    fun writeAxisButtonMappingToConfig(settingsKey: String, axis: Int, direction: String) {
        val threshold = if (direction == "+") "0.5" else "-0.5"
        val paramPackage = "engine:gamepad,axis:$axis,direction:$direction,threshold:$threshold"
        writeToConfigIni(settingsKey, paramPackage)
        Log.debug("[InputConfigSync] Wrote axis-button mapping: $settingsKey = $paramPackage")
    }

    fun clearMappingFromConfig(settingsKey: String) {
        writeToConfigIni(settingsKey, "")
        Log.debug("[InputConfigSync] Cleared mapping: $settingsKey")
    }

    fun syncFromConfigToPreferences() {
        try {
            val controlSettings = readControlsFromConfigIni()
            if (controlSettings.isEmpty()) {
                Log.info("[InputConfigSync] No control settings found in config.ini")
                return
            }

            val editor = preferences.edit()
            var syncCount = 0

            // Sync button mappings
            for (key in allButtonKeys) {
                val paramPackage = controlSettings[key]
                if (!paramPackage.isNullOrEmpty()) {
                    parseAndSyncButtonMapping(editor, key, paramPackage)
                    syncCount++
                }
            }

            // Sync axis mappings
            for (key in allAxisKeys) {
                val paramPackage = controlSettings[key]
                if (!paramPackage.isNullOrEmpty()) {
                    parseAndSyncAxisMapping(editor, key, paramPackage)
                    syncCount++
                }
            }

            editor.apply()
            Log.info("[InputConfigSync] Synced $syncCount controls from config.ini to SharedPreferences")
        } catch (e: Exception) {
            Log.error("[InputConfigSync] Failed to sync from config: ${e.message}")
            e.printStackTrace()
        }
    }

    private fun readControlsFromConfigIni(): Map<String, String> {
        val result = mutableMapOf<String, String>()

        try {
            val configFile = SettingsFile.getSettingsFile(SettingsFile.FILE_NAME_CONFIG)
            val inputStream = context.contentResolver.openInputStream(configFile.uri) ?: return result
            val reader = BufferedReader(InputStreamReader(inputStream))

            var inControlsSection = false

            reader.useLines { lines ->
                for (line in lines) {
                    val trimmedLine = line.trim()

                    // Check for section headers
                    if (trimmedLine.startsWith("[") && trimmedLine.endsWith("]")) {
                        val sectionName = trimmedLine.substring(1, trimmedLine.length - 1)
                        inControlsSection = (sectionName == Settings.SECTION_CONTROLS)
                        continue
                    }

                    // Parse key=value pairs in Controls section
                    if (inControlsSection && trimmedLine.contains("=")) {
                        val parts = trimmedLine.split("=", limit = 2)
                        if (parts.size == 2) {
                            val key = parts[0].trim()
                            val value = parts[1].trim()
                            if (value.isNotEmpty()) {
                                result[key] = value
                                Log.debug("[InputConfigSync] Read from config.ini: $key = $value")
                            }
                        }
                    }
                }
            }

            reader.close()
        } catch (e: Exception) {
            Log.error("[InputConfigSync] Error reading config.ini: ${e.message}")
        }

        return result
    }

    private fun parseAndSyncButtonMapping(editor: SharedPreferences.Editor, key: String, paramPackage: String) {
        val params = parseParamPackage(paramPackage)

        if (params.containsKey("code")) {
            // Get the Android keyCode (for SharedPreferences mapping and UI)
            // If keycode is present, use it; otherwise fall back to code
            val keyCode = params["keycode"]?.toIntOrNull() ?: params["code"]?.toIntOrNull() ?: return
            val guestButtonCode = buttonKeyToCode[key] ?: return

            val inputKey = "${INPUT_MAPPING_PREFIX}_HostAxis_$keyCode"
            editor.putInt(inputKey, guestButtonCode)

            val reverseKey = "${INPUT_MAPPING_PREFIX}_ReverseMapping_$key"
            editor.putString(reverseKey, inputKey)

            // Show keyCode in UI (the Android button code user pressed)
            editor.putString(key, "Gamepad: Button $keyCode")
        } else if (params.containsKey("axis")) {
            val axis = params["axis"]?.toIntOrNull() ?: return
            val direction = params["direction"] ?: "+"
            val guestButtonCode = buttonKeyToCode[key] ?: return

            val axisKey = "${INPUT_MAPPING_PREFIX}_HostAxis_$axis"
            editor.putInt("${axisKey}_GuestButton", guestButtonCode)

            val reverseKey = "${INPUT_MAPPING_PREFIX}_ReverseMapping_$key"
            editor.putString(reverseKey, axisKey)

            editor.putString(key, "Gamepad: Axis $axis $direction")
        }
    }

    private fun parseAndSyncAxisMapping(editor: SharedPreferences.Editor, key: String, paramPackage: String) {
        val params = parseParamPackage(paramPackage)

        if (params.containsKey("code")) {
            val axis = params["code"]?.toIntOrNull() ?: return
            val guestButtonCode = analogKeyToCode[key] ?: return
            val isHorizontal = key.contains("horizontal", ignoreCase = true)

            val axisKey = "${INPUT_MAPPING_PREFIX}_HostAxis_$axis"
            editor.putInt("${axisKey}_GuestButton", guestButtonCode)
            editor.putInt("${axisKey}_GuestOrientation", if (isHorizontal) 0 else 1)

            val reverseKey = "${INPUT_MAPPING_PREFIX}_ReverseMapping_${key}_${if (isHorizontal) 0 else 1}"
            editor.putString(reverseKey, axisKey)

            editor.putString(key, "Gamepad: Axis $axis")
        }
    }

    private fun parseParamPackage(paramPackage: String): Map<String, String> {
        return paramPackage.split(",")
            .mapNotNull { part ->
                val keyValue = part.split(":")
                if (keyValue.size == 2) {
                    keyValue[0] to keyValue[1]
                } else {
                    null
                }
            }
            .toMap()
    }

    private fun writeToConfigIni(key: String, value: String) {
        try {
            val setting = object : org.citra.citra_emu.features.settings.model.AbstractStringSetting {
                override var string: String = value
                override val key: String = key
                override val section: String = Settings.SECTION_CONTROLS
                override val isRuntimeEditable: Boolean = true
                override val valueAsString: String = value
                override val defaultValue: String = ""
            }
            SettingsFile.saveFile(SettingsFile.FILE_NAME_CONFIG, setting)
        } catch (e: Exception) {
            Log.error("[InputConfigSync] Failed to write to config.ini: ${e.message}")
        }
    }
}
