// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model

import android.text.TextUtils
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.R
import org.citra.citra_emu.features.settings.ui.SettingsActivityView
import org.citra.citra_emu.features.settings.utils.SettingsFile
import java.util.TreeMap

class Settings {
    private var gameId: String? = null

    var isLoaded = false
    private val touchedKeys: MutableSet<String> = mutableSetOf()

    /**
     * A HashMap<String></String>, SettingSection> that constructs a new SettingSection instead of returning null
     * when getting a key not already in the map
     */
    class SettingsSectionMap : HashMap<String, SettingSection?>() {
        override operator fun get(key: String): SettingSection? {
            if (!super.containsKey(key)) {
                val section = SettingSection(key)
                super.put(key, section)
                return section
            }
            return super.get(key)
        }
    }

    var sections: HashMap<String, SettingSection?> = SettingsSectionMap()

    fun getSection(sectionName: String): SettingSection? {
        return sections[sectionName]
    }

    val isEmpty: Boolean
        get() = sections.isEmpty()

    fun loadSettings(view: SettingsActivityView? = null) {
        sections = SettingsSectionMap()
        touchedKeys.clear()
        loadCitraSettings(view)
        if (!TextUtils.isEmpty(gameId)) {
            loadCustomGameSettings(gameId!!, view)
        }
        isLoaded = true
    }

    private fun loadCitraSettings(view: SettingsActivityView?) {
        for ((fileName) in configFileSectionsMap) {
            sections.putAll(SettingsFile.readFile(fileName, view))
        }
    }

    private fun loadCustomGameSettings(gameId: String, view: SettingsActivityView?) {
        // Custom game settings
        mergeSections(SettingsFile.readCustomGameSettings(gameId, view))
    }

    private fun mergeSections(updatedSections: HashMap<String, SettingSection?>) {
        for ((key, updatedSection) in updatedSections) {
            if (sections.containsKey(key)) {
                val originalSection = sections[key]
                originalSection!!.mergeSection(updatedSection!!)
            } else {
                sections[key] = updatedSection
            }
        }
    }

    fun loadSettings(gameId: String, view: SettingsActivityView) {
        this.gameId = gameId
        loadSettings(view)
    }

    fun saveSettings(view: SettingsActivityView) {
        if (TextUtils.isEmpty(gameId)) {
            view.showToastMessage(
                CitraApplication.appContext.getString(R.string.ini_saved),
                false
            )
            for ((fileName, sectionNames) in configFileSectionsMap.entries) {
                val iniSections = TreeMap<String, SettingSection?>()
                for (section in sectionNames) {
                    iniSections[section] = sections[section]
            }
            SettingsFile.saveFile(fileName, iniSections, view)
        }
        } else {
            // Save per-game settings to config/custom/<gameId>.ini.
            // Compare current (merged) values to global config.ini and include explicit choices.
            val globalSections = SettingsFile.readFile(SettingsFile.FILE_NAME_CONFIG, view)

            val overrides = TreeMap<String, SettingSection?>()
            val priorOverrides = SettingsFile.readCustomGameSettings(gameId!!, view)
            for ((sectionName, effectiveSection) in sections) {
                if (effectiveSection == null) continue
                val globalSection = globalSections[sectionName]
                val priorSection = priorOverrides[sectionName]

                val overrideSection = SettingSection(sectionName)
                for ((key, effSetting) in effectiveSection.settings) {
                    if (effSetting == null) continue

                    val globalSetting = globalSection?.getSetting(key)
                    val hadPrior = priorSection?.getSetting(key) != null
                    val wasTouched = touchedKeys.contains("$sectionName::$key")

                    // Include key when one of the following is true:
                    // - value differs from the compiled default (explicit choice),
                    // - key existed previously in the per-game file (preserve intent),
                    // - user touched this setting in this session (explicit choice).
                    if (!isDefaultValue(effSetting) || hadPrior || wasTouched) {
                        val toWrite: AbstractSetting = if (isDefaultValue(effSetting))
                            SimpleStringSetting(key, sectionName, "") else effSetting
                        overrideSection.putSetting(toWrite)
                    }
                }

                if (!overrideSection.settings.isEmpty()) {
                    overrides[sectionName] = overrideSection
                }
            }

            view.showToastMessage(
                CitraApplication.appContext.getString(R.string.ini_saved),
                false
            )
            SettingsFile.saveCustomFile(gameId!!, overrides, view)
        }
    }

    private fun isDefaultValue(setting: AbstractSetting): Boolean = when (setting) {
        is AbstractBooleanSetting -> setting.boolean == setting.defaultValue
        is AbstractIntSetting -> setting.int == setting.defaultValue
        is ScaledFloatSetting -> setting.float == setting.defaultValue * setting.scale
        is FloatSetting -> setting.float == setting.defaultValue
        is AbstractShortSetting -> setting.short == setting.defaultValue
        is AbstractStringSetting -> setting.string == setting.defaultValue
        else -> false
    }

    private fun areSettingsEqual(a: AbstractSetting, b: AbstractSetting?): Boolean {
        if (b == null) {
            // Global missing means it uses compiled default; equal if a is default.
            return isDefaultValue(a)
        }
        return when {
            a is AbstractBooleanSetting && b is AbstractBooleanSetting -> a.boolean == b.boolean
            a is AbstractIntSetting && b is AbstractIntSetting -> a.int == b.int
            a is ScaledFloatSetting && b is ScaledFloatSetting -> a.float == b.float
            a is FloatSetting && b is FloatSetting -> a.float == b.float
            a is AbstractShortSetting && b is AbstractShortSetting -> a.short == b.short
            a is AbstractStringSetting && b is AbstractStringSetting -> a.string == b.string
            else -> a.valueAsString == b.valueAsString
        }
    }

    private data class SimpleStringSetting(
        override val key: String?,
        override val section: String?,
        private val value: String
    ) : AbstractSetting {
        override val isRuntimeEditable: Boolean get() = true
        override val valueAsString: String get() = value
        override val defaultValue: Any get() = ""
    }

    fun markTouched(setting: AbstractSetting) {
        if (setting.section != null && setting.key != null) {
            touchedKeys.add("${setting.section}::${setting.key}")
        }
    }

    fun saveSetting(setting: AbstractSetting, filename: String) {
        SettingsFile.saveFile(filename, setting)
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
            HOTKEY_SCREEN_SWAP,
            HOTKEY_CYCLE_LAYOUT,
            HOTKEY_CLOSE_GAME,
            HOTKEY_PAUSE_OR_RESUME,
            HOTKEY_QUICKSAVE,
            HOTKEY_QUICKlOAD,
            HOTKEY_TURBO_LIMIT
        )
        val hotkeyTitles = listOf(
            R.string.emulation_swap_screens,
            R.string.emulation_cycle_landscape_layouts,
            R.string.emulation_close_game,
            R.string.emulation_toggle_pause,
            R.string.emulation_quicksave,
            R.string.emulation_quickload,
            R.string.turbo_limit_hotkey
        )

        const val PREF_FIRST_APP_LAUNCH = "FirstApplicationLaunch"
        const val PREF_MATERIAL_YOU = "MaterialYouTheme"
        const val PREF_THEME_MODE = "ThemeMode"
        const val PREF_BLACK_BACKGROUNDS = "BlackBackgrounds"
        const val PREF_SHOW_HOME_APPS = "ShowHomeApps"
        const val PREF_STATIC_THEME_COLOR = "StaticThemeColor"

        private val configFileSectionsMap: MutableMap<String, List<String>> = HashMap()

        init {
            configFileSectionsMap[SettingsFile.FILE_NAME_CONFIG] =
                listOf(
                    SECTION_CORE,
                    SECTION_SYSTEM,
                    SECTION_CAMERA,
                    SECTION_CONTROLS,
                    SECTION_RENDERER,
                    SECTION_LAYOUT,
                    SECTION_STORAGE,
                    SECTION_UTILITY,
                    SECTION_AUDIO,
                    SECTION_DEBUG
                )
        }
    }
}
