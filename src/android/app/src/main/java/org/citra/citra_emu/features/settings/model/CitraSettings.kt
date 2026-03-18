// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

package org.citra.citra_emu.features.settings.model

import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.features.input.Input
import org.citra.citra_emu.features.input.InputMappingManager

class CitraSettings {
    private val globalValues = HashMap<String, Any>()
    private val perAppOverrides = HashMap<String, Any>()

    var gameId: String? = null

    fun isPerApp(): Boolean = (gameId != null && gameId != "")

    fun <T> get(setting: AbstractSetting<T>): T {
        @Suppress("UNCHECKED_CAST")
        return (
            perAppOverrides[setting.key]
                ?: globalValues[setting.key]
                ?: setting.defaultValue
            ) as T
    }

    fun <T> getGlobal(setting: AbstractSetting<T>): T {
        @Suppress("UNCHECKED_CAST")
        return (globalValues[setting.key] ?: setting.defaultValue) as T
    }

    fun <T> setGlobal(setting: AbstractSetting<T>, value: T) {
        globalValues[setting.key] = value as Any
        if (this == activeSettings && !hasOverride(setting) && setting is InputMappingSetting) {
            InputMappingManager.rebind(setting, value as Input)
        }
    }

    fun <T> setOverride(setting: AbstractSetting<T>, value: T) {
        perAppOverrides[setting.key] = value as Any
        if (this == activeSettings && setting is InputMappingSetting) {
            InputMappingManager.rebind(setting, value as Input)
        }
    }

    /**
     * This updates a setting based on the Settings object scope. If the settings object is
     * applied to a specific game either because a game is running or using a per-game settings menu,
     * this will update the override for that game. Otherwise it will update the global value.
     *
     * This should be used by the Settings menus.
     */
    fun <T> setBySettingsContext(setting: AbstractSetting<T>, value: T) {
        if (isPerApp()) setOverride(setting, value) else setGlobal(setting, value)
    }

    /**
     * Updates an existing setting in place. If this setting is currently a per-game setting,
     * this changes the per-game value. If it is currently global, it will change the global value.
     *
     * This should be used by the Quick Menu, hotkeys, and other "instant" settings changes
     */
    fun <T> setByCurrentState(setting: AbstractSetting<T>, value: T) {
        if (hasOverride(setting)) setOverride(setting, value) else setGlobal(setting, value)
    }

    /** Merge the globals from other into the current settings. Merge per-game if game id is the same. */
    fun mergeSettings(other: CitraSettings) {
        other.globalValues.forEach { (key, value) ->
            globalValues[key] = value
        }

        if (gameId == other.gameId) {
            perAppOverrides.clear()
            other.perAppOverrides.forEach { (key, value) ->
                perAppOverrides[key] = value
            }
        }

        if (this == activeSettings) {
            InputMappingManager.rebuild()
        }
    }

    fun <T> clearOverride(setting: AbstractSetting<T>) {
        perAppOverrides.remove(setting.key)
        if (this == activeSettings && setting is InputMappingSetting) {
            InputMappingManager.rebind(setting, getGlobal(setting) as Input)
        }
    }

    fun hasOverride(setting: AbstractSetting<*>): Boolean = perAppOverrides.containsKey(setting.key)

    fun getAllOverrides(): Map<String, Any> = perAppOverrides.toMap()

    fun getAllGlobal(): Map<String, Any> = globalValues.toMap()

    fun clearAll() {
        globalValues.clear()
        perAppOverrides.clear()
        if (this == activeSettings) {
            InputMappingManager.clear()
        }
    }

    fun changeToGlobalContext() {
        gameId = null
        clearAllOverrides()
    }

    fun clearAllOverrides() {
        perAppOverrides.clear()
        if (this == activeSettings) {
            InputMappingManager.rebuild()
        }
    }

    companion object {
        const val SECTION_CORE = "Core"
        const val SECTION_SYSTEM = "System"
        const val SECTION_CAMERA = "Camera"
        const val SECTION_CONTROLS = "Controls"
        const val SECTION_RENDERER = "Renderer"
        const val SECTION_LAYOUT = "Layout"
        const val SECTION_UTILITY = "Utility"
        const val SECTION_NETWORK = "WebService"
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

        /** The settings (global and per-game overrides) that are active right now.*/
        val activeSettings = CitraSettings()

        fun syncDefaultsToNative() {
            val allSettings: List<AbstractSetting<*>> by lazy {
                BooleanSetting.values().toList() +
                    IntSetting.values().toList() +
                    FloatSetting.values().toList() +
                    StringSetting.values().toList() +
                    IntListSetting.values().toList()
            }
            for (setting in allSettings) {
                NativeLibrary.addDefaultSetting(
                    setting.key,
                    setting.defaultValueToString()
                )
            }
            defaultsSynced = true
        }
        var defaultsSynced: Boolean = false
    }
}
