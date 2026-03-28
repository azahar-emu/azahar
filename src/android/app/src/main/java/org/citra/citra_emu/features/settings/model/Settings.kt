// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model

import org.citra.citra_emu.features.input.Input
import org.citra.citra_emu.features.input.InputMappingManager
import org.citra.citra_emu.features.settings.utils.SettingsFile

class Settings {
    private val globalValues = HashMap<String, Any>()
    private val perGameOverrides = HashMap<String, Any>()

    val inputMappingManager = InputMappingManager()

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

    /** Sets the global value specifically */
    fun <T> setGlobal(setting: AbstractSetting<T>, value: T) {
        globalValues[setting.key] = value as Any
        // only update the InputMapping if this global setting actually is in effect now
        if (setting is InputMappingSetting && !hasOverride(setting)) {
            inputMappingManager.rebind(setting, value as? Input)
        }
    }

    /** Sets the override specifically */
    fun <T> setOverride(setting: AbstractSetting<T>, value: T) {
        perGameOverrides[setting.key] = value as Any
        if (setting is InputMappingSetting) {
            inputMappingManager.rebind(setting,  value as? Input)
        }
    }

    /** Sets the per-game or global setting based on whether this file has ANY per-game setting.
     * This should be used by the Custom Settings Activity
     */
    fun <T> set(setting: AbstractSetting<T>, value: T) {
        if (isPerGame()) setOverride(setting, value) else setGlobal(setting, value)
    }

    /**
     * Updates an existing setting honoring whether this particular setting is *currently* global or local.
     * This should be used by the Quick Menu
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

        inputMappingManager.rebuild(this)
    }

    fun <T> clearOverride(setting: AbstractSetting<T>) {
        perGameOverrides.remove(setting.key)
        if (setting is InputMappingSetting) {
            inputMappingManager.rebind(setting, getGlobal(setting))
        }
    }

    fun hasOverride(setting: AbstractSetting<*>): Boolean {
        return perGameOverrides.containsKey(setting.key)
    }

    fun getAllOverrides(): Map<String, Any> = perGameOverrides.toMap()

    fun getAllGlobal(): Map<String, Any> = globalValues.toMap()

    fun clearAll() {
        globalValues.clear()
        perGameOverrides.clear()
        inputMappingManager.clear()
    }

    fun clearOverrides() {
        perGameOverrides.clear()
        inputMappingManager.rebuild(this)
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