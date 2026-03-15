// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model

import org.citra.citra_emu.R

class Settings {
    private val globalValues = HashMap<String, Any>()
    private val perGameOverrides = HashMap<String, Any>()

    var gameId: String? = null

    fun isPerGame(): Boolean = gameId != null && gameId != ""

    fun <T> get(setting: AbstractSetting<T>): T {
        @Suppress("UNCHECKED_CAST")
        return (perGameOverrides[setting.key]
            ?: globalValues[setting.key]
            ?: setting.defaultValue) as T
    }

    fun <T> getGlobal(setting: AbstractSetting<T>): T {
        @Suppress("UNCHECKED_CAST")
        return (globalValues[setting.key] ?: setting.defaultValue) as T
    }

    fun <T> setGlobal(setting: AbstractSetting<T>, value: T) {
        globalValues[setting.key] = value as Any
    }

    fun <T> setOverride(setting: AbstractSetting<T>, value: T) {
        perGameOverrides[setting.key] = value as Any
    }

    /** Sets the per-game or global setting based on whether this file has ANY per-game setting.
     * This should be used, for example, by the Settings Activity
     */
    fun <T> set(setting: AbstractSetting<T>, value: T) {
        if (isPerGame()) setOverride(setting, value) else setGlobal(setting, value)
    }

    /**
     * Updates an existing setting honoring whether it is *currently* global or local. This will
     * be used by the Quick Menu
     */
    fun <T> update(setting: AbstractSetting<T>, value: T) {
        if (hasOverride(setting)) setOverride(setting, value) else setGlobal(setting, value)
    }

    /** Merge the globals from other into the current settings. Merge per-game if game id is the same. */
    fun mergeSettings(other: Settings) {
        other.globalValues.forEach{ (key, value) ->
            globalValues[key] = value
        }

        if (gameId != other.gameId) return

        perGameOverrides.clear()
        other.perGameOverrides.forEach{ (key, value) ->
            perGameOverrides[key] = value
        }
    }

    fun <T> clearOverride(setting: AbstractSetting<T>) {
        perGameOverrides.remove(setting.key)
    }

    fun hasOverride(setting: AbstractSetting<*>): Boolean {
        return perGameOverrides.containsKey(setting.key)
    }

    fun getAllOverrides(): Map<String, Any> = perGameOverrides.toMap()

    fun getAllGlobal(): Map<String, Any> = globalValues.toMap()

    fun clearAll() {
        globalValues.clear()
        perGameOverrides.clear()
    }

    fun clearOverrides() {
        perGameOverrides.clear()
    }

    fun removePerGameSettings() {
        clearOverrides()
        gameId = null
    }


    companion object {

        const val SECTION_CORE = "Core"
        const val SECTION_SYSTEM = "System"
        const val SECTION_CAMERA = "Camera"
        const val SECTION_CONTROLS = "Controls"
        const val SECTION_RENDERER = "Renderer"
        const val SECTION_LAYOUT = "Layout"
        const val SECTION_UTILITY = "Utility"
        const val SECTION_AUDIO = "Audio"
        const val SECTION_DEBUG = "Debugging"
        const val SECTION_THEME = "Theme"
        const val SECTION_CUSTOM_LANDSCAPE = "Custom Landscape Layout"
        const val SECTION_CUSTOM_PORTRAIT = "Custom Portrait Layout"
        const val SECTION_PERFORMANCE_OVERLAY = "Performance Overlay"
        const val SECTION_STORAGE = "Storage"
        const val SECTION_MISC = "Miscellaneous"

        const val KEY_BUTTON_A = "button_a"
        const val KEY_BUTTON_B = "button_b"
        const val KEY_BUTTON_X = "button_x"
        const val KEY_BUTTON_Y = "button_y"
        const val KEY_BUTTON_SELECT = "button_select"
        const val KEY_BUTTON_START = "button_start"
        const val KEY_BUTTON_HOME = "button_home"
        const val KEY_BUTTON_UP = "button_up"
        const val KEY_BUTTON_DOWN = "button_down"
        const val KEY_BUTTON_LEFT = "button_left"
        const val KEY_BUTTON_RIGHT = "button_right"
        const val KEY_BUTTON_L = "button_l"
        const val KEY_BUTTON_R = "button_r"
        const val KEY_BUTTON_ZL = "button_zl"
        const val KEY_BUTTON_ZR = "button_zr"
        const val KEY_CIRCLEPAD_AXIS_VERTICAL = "circlepad_axis_vertical"
        const val KEY_CIRCLEPAD_AXIS_HORIZONTAL = "circlepad_axis_horizontal"
        const val KEY_CSTICK_AXIS_VERTICAL = "cstick_axis_vertical"
        const val KEY_CSTICK_AXIS_HORIZONTAL = "cstick_axis_horizontal"
        const val KEY_DPAD_AXIS_VERTICAL = "dpad_axis_vertical"
        const val KEY_DPAD_AXIS_HORIZONTAL = "dpad_axis_horizontal"
        const val HOTKEY_ENABLE = "hotkey_enable"
        const val HOTKEY_SCREEN_SWAP = "hotkey_screen_swap"
        const val HOTKEY_CYCLE_LAYOUT = "hotkey_toggle_layout"
        const val HOTKEY_CLOSE_GAME = "hotkey_close_game"
        const val HOTKEY_PAUSE_OR_RESUME = "hotkey_pause_or_resume_game"
        const val HOTKEY_QUICKSAVE = "hotkey_quickload"
        const val HOTKEY_QUICKlOAD = "hotkey_quickpause"
        const val HOTKEY_TURBO_LIMIT = "hotkey_turbo_limit"

        val buttonKeys = listOf(
            KEY_BUTTON_A,
            KEY_BUTTON_B,
            KEY_BUTTON_X,
            KEY_BUTTON_Y,
            KEY_BUTTON_SELECT,
            KEY_BUTTON_START,
            KEY_BUTTON_HOME
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
            KEY_CIRCLEPAD_AXIS_VERTICAL,
            KEY_CIRCLEPAD_AXIS_HORIZONTAL
        )
        val cStickKeys = listOf(
            KEY_CSTICK_AXIS_VERTICAL,
            KEY_CSTICK_AXIS_HORIZONTAL
        )
        val dPadAxisKeys = listOf(
            KEY_DPAD_AXIS_VERTICAL,
            KEY_DPAD_AXIS_HORIZONTAL
        )
        val dPadButtonKeys = listOf(
            KEY_BUTTON_UP,
            KEY_BUTTON_DOWN,
            KEY_BUTTON_LEFT,
            KEY_BUTTON_RIGHT
        )
        val axisTitles = listOf(
            R.string.controller_axis_vertical,
            R.string.controller_axis_horizontal
        )
        val dPadTitles = listOf(
            R.string.direction_up,
            R.string.direction_down,
            R.string.direction_left,
            R.string.direction_right
        )
        val triggerKeys = listOf(
            KEY_BUTTON_L,
            KEY_BUTTON_R,
            KEY_BUTTON_ZL,
            KEY_BUTTON_ZR
        )
        val triggerTitles = listOf(
            R.string.button_l,
            R.string.button_r,
            R.string.button_zl,
            R.string.button_zr
        )
        val hotKeys = listOf(
            HOTKEY_ENABLE,
            HOTKEY_SCREEN_SWAP,
            HOTKEY_CYCLE_LAYOUT,
            HOTKEY_CLOSE_GAME,
            HOTKEY_PAUSE_OR_RESUME,
            HOTKEY_QUICKSAVE,
            HOTKEY_QUICKlOAD,
            HOTKEY_TURBO_LIMIT
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

        // TODO: Move these in with the other setting keys in GenerateSettingKeys.cmake
        const val PREF_FIRST_APP_LAUNCH = "FirstApplicationLaunch"
        const val PREF_MATERIAL_YOU = "MaterialYouTheme"
        const val PREF_THEME_MODE = "ThemeMode"
        const val PREF_BLACK_BACKGROUNDS = "BlackBackgrounds"
        const val PREF_SHOW_HOME_APPS = "ShowHomeApps"
        const val PREF_STATIC_THEME_COLOR = "StaticThemeColor"

        private val configFileSectionsMap: MutableMap<String, List<String>> = HashMap()

        /** Stores the settings as a singleton available everywhere.*/
        val settings = Settings()
    }
}