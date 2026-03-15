// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.utils

import android.content.Context
import android.net.Uri
import androidx.documentfile.provider.DocumentFile
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.R
import org.citra.citra_emu.features.settings.model.AbstractSetting
import org.citra.citra_emu.features.settings.model.BooleanSetting
import org.citra.citra_emu.features.settings.model.FloatSetting
import org.citra.citra_emu.features.settings.model.IntListSetting
import org.citra.citra_emu.features.settings.model.IntSetting
import org.citra.citra_emu.features.settings.model.Settings
import org.citra.citra_emu.features.settings.model.StringSetting
import org.citra.citra_emu.features.settings.ui.SettingsActivityView
import org.citra.citra_emu.utils.DirectoryInitialization.userDirectory
import org.citra.citra_emu.utils.Log
import org.ini4j.Wini
import java.io.BufferedReader
import java.io.FileNotFoundException
import java.io.IOException
import java.io.InputStreamReader


/**
 * Contains static methods for interacting with .ini files in which settings are stored.
 */
object SettingsFile {
    const val FILE_NAME_CONFIG = "config"

    private val allSettings: List<AbstractSetting<*>> by lazy {
        BooleanSetting.values().toList() +
                IntSetting.values().toList() +
                FloatSetting.values().toList() +
                StringSetting.values().toList() +
                IntListSetting.values().toList()
    }

    private fun findSettingByKey(key: String): AbstractSetting<*>? =
        allSettings.firstOrNull { it.key == key }
    /**
     * Reads a given .ini file from disk and updates a instance of the Settings class appropriately
     *
     * @param ini          The ini file to load the settings from
     * @param settings     The Settings instance to edit
     * @param isCustomGame
     * @param view         The current view.
     * @return An Observable that emits a HashMap of the file's contents, then completes.
     */
    fun readFile(
        ini: DocumentFile,
        settings: Settings,
        isCustomGame: Boolean,
        view: SettingsActivityView?
    ) {
        var reader: BufferedReader? = null
        try {
            val context: Context = CitraApplication.appContext
            val inputStream = context.contentResolver.openInputStream(ini.uri)
            reader = BufferedReader(InputStreamReader(inputStream))
            var currentSection: String? = null
            var line: String?
            while (reader.readLine().also { line = it } != null) {
                if (line!!.startsWith("[") && line.endsWith("]")) {
                    currentSection = line.substring(1, line.length-1)
                } else if (currentSection != null) {
                    val pair = parseLineToKeyValuePair(line) ?: continue
                    val (key, rawValue) = pair
                    val descriptor = findSettingByKey(key) ?: continue
                    loadSettingInto(settings, descriptor, rawValue, isCustomGame)
                }
            }
        } catch (e: FileNotFoundException) {
            Log.error("[SettingsFile] File not found: " + ini.uri + e.message)
            view?.onSettingsFileNotFound()
        } catch (e: IOException) {
            Log.error("[SettingsFile] Error reading from: " + ini.uri + e.message)
            view?.onSettingsFileNotFound()
        } finally {
            if (reader != null) {
                try {
                    reader.close()
                } catch (e: IOException) {
                    Log.error("[SettingsFile] Error closing: " + ini.uri + e.message)
                }
            }
        }
    }

    /**
     * Load global settings from the config file into the settings instance
     */
    fun loadSettings(settings: Settings, view: SettingsActivityView? = null) {
        readFile(getSettingsFile(FILE_NAME_CONFIG),settings,false,view)
    }

    /**
     * Load global settings AND custom settings into the settings instance, sets gameId
     */
    fun loadSettings(settings: Settings, gameId: String, view: SettingsActivityView? = null) {
        settings.gameId = gameId
        loadSettings(settings, view)
        val file = findCustomGameSettingsFile(gameId) ?: return
        readFile(file, settings, true, view)
    }

    /**
     * Uses the settings object to parse the raw string and store it in the correct map
     */
    @Suppress("UNCHECKED_CAST")
    private fun <T> loadSettingInto(
        settings: Settings,
        setting: AbstractSetting<T>,
        rawValue: String,
        isCustomGame: Boolean
    ) {
        val value = setting.valueFromString(rawValue) ?: return
        if (isCustomGame) {
            settings.setOverride(setting, value)
        } else {
            settings.setGlobal(setting, value)
        }
    }

    /**
     * Saves a the global settings from a Settings instance
     * to the global .ini file on disk. If unsuccessful, outputs an error
     * telling why it failed.
     *
     * @param settings The Settings instance we are saving
     * @param view     The current view.
     */
    fun saveGlobalFile(
        settings: Settings,
        view: SettingsActivityView? = null
    ) {
        val ini = getSettingsFile(FILE_NAME_CONFIG)
        try {
            val context: Context = CitraApplication.appContext
            val inputStream = context.contentResolver.openInputStream(ini.uri)
            val writer = Wini(inputStream)
            inputStream!!.close()

            for (setting in allSettings) {
                val value = settings.getGlobal(setting) ?: continue
                writeSettingToWini(writer, setting, value)
            }

            val outputStream = context.contentResolver.openOutputStream(ini.uri, "wt")
            writer.store(outputStream)
            outputStream!!.flush()
            outputStream.close()
        } catch (e: Exception) {
            Log.error("[SettingsFile] File not found: $FILE_NAME_CONFIG.ini: ${e.message}")
            view?.showToastMessage(
                CitraApplication.appContext
                    .getString(R.string.error_saving, FILE_NAME_CONFIG, e.message), false
            )
        }
    }

    /**
     * Save the per-game overrides to a per-game config file
     */

    fun saveCustomFile(
        settings: Settings,
        view: SettingsActivityView? = null
    ) {
        if (!settings.hasPerGameSettings()) return
        val ini = getOrCreateCustomGameSettingsFile(settings.gameId!!)
        try {
            val context: Context = CitraApplication.appContext
            val writer = Wini()

            val overrides = settings.getAllOverrides()
            for (descriptor in allSettings) {
                val value = overrides[descriptor.key] ?: continue
                writeSettingToWini(writer, descriptor, value)
            }

            val outputStream = context.contentResolver.openOutputStream(ini.uri, "wt")
            writer.store(outputStream)
            outputStream?.flush()
            outputStream?.close()
        } catch (e: Exception) {
            Log.error("[SettingsFile] Error saving custom file for ${settings.gameId}: ${e.message}")
            view?.onSettingsFileNotFound()
        }
    }

    fun <T> saveSetting(setting: AbstractSetting<T>, settings: Settings) {
        if (settings.hasOverride(setting)) {
            // Currently a per-game setting, keep it that way
            val ini = getOrCreateCustomGameSettingsFile(settings.gameId!!)
            writeSingleSettingToFile(ini, setting, settings.get(setting))
        } else {
            // Currently global, save to global file
            val ini = getSettingsFile(FILE_NAME_CONFIG)
            writeSingleSettingToFile(ini, setting, settings.getGlobal(setting))
        }
    }

    private fun <T> writeSingleSettingToFile(ini: DocumentFile, setting: AbstractSetting<T>, value: T) {
        try {
            val context = CitraApplication.appContext
            val inputStream = context.contentResolver.openInputStream(ini.uri)
            val writer = if (inputStream != null) Wini(inputStream) else Wini()
            inputStream?.close()
            writeSettingToWini(writer, setting, value as Any)
            val outputStream = context.contentResolver.openOutputStream(ini.uri, "wt")
            writer.store(outputStream)
            outputStream!!.flush()
            outputStream.close()
        } catch (e: Exception) {
            Log.error("[SettingsFile] Error saving setting ${setting.key}: ${e.message}")
        }
    }
    @Suppress("UNCHECKED_CAST")
    private fun <T> writeSettingToWini(writer: Wini, descriptor: AbstractSetting<T>, value: Any) {
        val typedValue = value as T
        writer.put(descriptor.section, descriptor.key, descriptor.valueToString(typedValue))
    }

    private fun parseLineToKeyValuePair(line: String): Pair<String, String>? {
        val splitLine = line.split("=".toRegex(), limit = 2)
        if (splitLine.size != 2) return null
        val key = splitLine[0].trim()
        val value = splitLine[1].trim()
        if (value.isEmpty()) return null
        return Pair(key, value)
    }


    fun getSettingsFile(fileName: String): DocumentFile {
        val root = DocumentFile.fromTreeUri(CitraApplication.appContext, Uri.parse(userDirectory))
        val configDirectory = root!!.findFile("config")
        return configDirectory!!.findFile("$fileName.ini")!!
    }

    fun customExists(gameId: String): Boolean = findCustomGameSettingsFile(gameId) != null

    private fun findCustomGameSettingsFile(gameId: String): DocumentFile? {
        val root = DocumentFile.fromTreeUri(CitraApplication.appContext, Uri.parse(userDirectory))
        val configDir = root?.findFile("config") ?: return null
        val customDir = configDir.findFile("custom") ?: return null
        return customDir.findFile("$gameId.ini")
    }

    private fun getOrCreateCustomGameSettingsFile(gameId: String): DocumentFile {
        val root = DocumentFile.fromTreeUri(CitraApplication.appContext, Uri.parse(userDirectory))!!
        val configDir = root.findFile("config") ?: root.createDirectory("config")!!
        val customDir = configDir.findFile("custom") ?: configDir.createDirectory("custom")!!
        return customDir.findFile("$gameId.ini")
            ?: customDir.createFile("*/*", "$gameId.ini")!!
    }
}
