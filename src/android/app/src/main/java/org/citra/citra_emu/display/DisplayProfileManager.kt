// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.display

import android.content.Context
import android.content.SharedPreferences
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.features.settings.model.BooleanSetting
import org.citra.citra_emu.features.settings.model.IntSetting
import org.citra.citra_emu.features.settings.utils.SettingsFile
import org.citra.citra_emu.utils.Log
import java.util.concurrent.CopyOnWriteArrayList

/**
 * Manages display profiles for automatic configuration when external displays
 * are connected (e.g., Xreal glasses, external monitors).
 */
object DisplayProfileManager {
    private const val PREFS_NAME = "display_profiles"
    private const val KEY_PROFILES = "profiles"
    private const val KEY_ENABLED = "enabled"
    private const val KEY_SNAPSHOT = "settings_snapshot"
    private const val KEY_ACTIVE_PROFILE = "active_profile"

    private val json = Json {
        ignoreUnknownKeys = true
        encodeDefaults = true
    }

    private val preferences: SharedPreferences by lazy {
        CitraApplication.appContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
    }

    private val profiles: CopyOnWriteArrayList<DisplayProfile> = CopyOnWriteArrayList()
    @Volatile private var settingsSnapshot: SettingsSnapshot? = null
    @Volatile private var activeProfileName: String? = null

    /**
     * Initialize the manager by loading saved profiles
     */
    fun initialize() {
        loadProfiles()
        loadSnapshot()
        activeProfileName = preferences.getString(KEY_ACTIVE_PROFILE, null)
    }

    /**
     * Check if per-display profiles feature is enabled
     */
    var isEnabled: Boolean
        get() = preferences.getBoolean(KEY_ENABLED, true)
        set(value) {
            preferences.edit().putBoolean(KEY_ENABLED, value).apply()
        }

    /**
     * Get all configured profiles
     */
    fun getProfiles(): List<DisplayProfile> = profiles.toList()

    /**
     * Add a new profile
     */
    fun addProfile(profile: DisplayProfile) {
        profiles.add(profile)
        saveProfiles()
    }

    /**
     * Update an existing profile
     */
    fun updateProfile(oldName: String, newProfile: DisplayProfile) {
        val index = profiles.indexOfFirst { it.profileName == oldName }
        if (index != -1) {
            profiles[index] = newProfile
            saveProfiles()
        }
    }

    /**
     * Remove a profile by name
     */
    fun removeProfile(profileName: String) {
        profiles.removeAll { it.profileName == profileName }
        saveProfiles()
    }

    /**
     * Find a matching profile for the given display name
     */
    fun findMatchingProfile(displayName: String): DisplayProfile? {
        if (!isEnabled) return null
        return profiles.firstOrNull { it.matchesDisplay(displayName) }
    }

    /**
     * Get the currently active profile name (if any)
     */
    fun getActiveProfileName(): String? = activeProfileName

    /**
     * Check if a profile is currently active
     */
    fun hasActiveProfile(): Boolean = activeProfileName != null

    /**
     * Save the current settings as a snapshot before applying a profile
     */
    fun saveSettingsSnapshot() {
        settingsSnapshot = SettingsSnapshot(
            stereoMode = IntSetting.STEREOSCOPIC_3D_MODE.int,
            factor3d = IntSetting.STEREOSCOPIC_3D_DEPTH.int,
            swapEyes = BooleanSetting.SWAP_EYES_3D.boolean,
            layoutOption = IntSetting.SCREEN_LAYOUT.int,
            stereoWhichDisplay = IntSetting.RENDER_3D_WHICH_DISPLAY.int
        )
        saveSnapshot()
        Log.info("[DisplayProfileManager] Settings snapshot saved")
    }

    /**
     * Apply a display profile's settings
     */
    fun applyProfile(profile: DisplayProfile) {
        Log.info("[DisplayProfileManager] Applying profile: ${profile.profileName}")

        // Update in-memory values
        IntSetting.STEREOSCOPIC_3D_MODE.int = profile.stereoMode
        IntSetting.STEREOSCOPIC_3D_DEPTH.int = profile.factor3d
        BooleanSetting.SWAP_EYES_3D.boolean = profile.swapEyes
        IntSetting.SCREEN_LAYOUT.int = profile.layoutOption
        IntSetting.RENDER_3D_WHICH_DISPLAY.int = profile.stereoWhichDisplay

        // Save to config file so native library can read them
        try {
            SettingsFile.saveFile(SettingsFile.FILE_NAME_CONFIG, IntSetting.STEREOSCOPIC_3D_MODE)
            SettingsFile.saveFile(SettingsFile.FILE_NAME_CONFIG, IntSetting.STEREOSCOPIC_3D_DEPTH)
            SettingsFile.saveFile(SettingsFile.FILE_NAME_CONFIG, BooleanSetting.SWAP_EYES_3D)
            SettingsFile.saveFile(SettingsFile.FILE_NAME_CONFIG, IntSetting.SCREEN_LAYOUT)
            SettingsFile.saveFile(SettingsFile.FILE_NAME_CONFIG, IntSetting.RENDER_3D_WHICH_DISPLAY)
        } catch (e: Exception) {
            Log.error("[DisplayProfileManager] Failed to save settings: ${e.message}")
        }

        activeProfileName = profile.profileName
        preferences.edit().putString(KEY_ACTIVE_PROFILE, profile.profileName).apply()

        // Notify the native side to reload settings and update framebuffer
        if (NativeLibrary.isRunning()) {
            NativeLibrary.reloadSettings()
            NativeLibrary.updateFramebuffer(NativeLibrary.isPortraitMode)
        }
    }

    /**
     * Restore settings from the saved snapshot
     */
    fun restoreSettingsSnapshot() {
        val snapshot = settingsSnapshot
        if (snapshot == null) {
            Log.warning("[DisplayProfileManager] No settings snapshot to restore")
            return
        }

        Log.info("[DisplayProfileManager] Restoring settings from snapshot")

        IntSetting.STEREOSCOPIC_3D_MODE.int = snapshot.stereoMode
        IntSetting.STEREOSCOPIC_3D_DEPTH.int = snapshot.factor3d
        BooleanSetting.SWAP_EYES_3D.boolean = snapshot.swapEyes
        IntSetting.SCREEN_LAYOUT.int = snapshot.layoutOption
        IntSetting.RENDER_3D_WHICH_DISPLAY.int = snapshot.stereoWhichDisplay

        // Save to config file so native library can read them
        try {
            SettingsFile.saveFile(SettingsFile.FILE_NAME_CONFIG, IntSetting.STEREOSCOPIC_3D_MODE)
            SettingsFile.saveFile(SettingsFile.FILE_NAME_CONFIG, IntSetting.STEREOSCOPIC_3D_DEPTH)
            SettingsFile.saveFile(SettingsFile.FILE_NAME_CONFIG, BooleanSetting.SWAP_EYES_3D)
            SettingsFile.saveFile(SettingsFile.FILE_NAME_CONFIG, IntSetting.SCREEN_LAYOUT)
            SettingsFile.saveFile(SettingsFile.FILE_NAME_CONFIG, IntSetting.RENDER_3D_WHICH_DISPLAY)
        } catch (e: Exception) {
            Log.error("[DisplayProfileManager] Failed to save snapshot settings: ${e.message}")
        }

        activeProfileName = null
        preferences.edit().remove(KEY_ACTIVE_PROFILE).apply()

        // Clear the snapshot after restoring
        settingsSnapshot = null
        preferences.edit().remove(KEY_SNAPSHOT).apply()

        // Notify the native side to reload settings and update framebuffer
        if (NativeLibrary.isRunning()) {
            NativeLibrary.reloadSettings()
            NativeLibrary.updateFramebuffer(NativeLibrary.isPortraitMode)
        }
    }

    /**
     * Handle display connected event
     */
    fun onDisplayConnected(displayName: String) {
        if (!isEnabled) {
            Log.debug("[DisplayProfileManager] Feature disabled, skipping display: $displayName")
            return
        }

        Log.info("[DisplayProfileManager] Display connected: $displayName")

        val profile = findMatchingProfile(displayName)
        if (profile != null) {
            Log.info("[DisplayProfileManager] Found matching profile: ${profile.profileName}")

            // Save current settings before applying profile
            if (!hasActiveProfile()) {
                saveSettingsSnapshot()
            }

            applyProfile(profile)
        } else {
            Log.debug("[DisplayProfileManager] No matching profile for display: $displayName")
        }
    }

    /**
     * Handle display disconnected event
     */
    fun onDisplayDisconnected() {
        if (!isEnabled) return

        if (hasActiveProfile()) {
            Log.info("[DisplayProfileManager] External display disconnected, restoring settings")
            restoreSettingsSnapshot()
        }
    }

    private fun loadProfiles() {
        val jsonString = preferences.getString(KEY_PROFILES, null)
        profiles.clear()
        if (jsonString != null) {
            try {
                profiles.addAll(json.decodeFromString<List<DisplayProfile>>(jsonString))
            } catch (e: Exception) {
                Log.error("[DisplayProfileManager] Failed to load profiles: ${e.message}")
            }
        }
    }

    private fun saveProfiles() {
        try {
            val jsonString = json.encodeToString(profiles.toList())
            preferences.edit().putString(KEY_PROFILES, jsonString).apply()
        } catch (e: Exception) {
            Log.error("[DisplayProfileManager] Failed to save profiles: ${e.message}")
        }
    }

    private fun loadSnapshot() {
        val jsonString = preferences.getString(KEY_SNAPSHOT, null)
        settingsSnapshot = if (jsonString != null) {
            try {
                json.decodeFromString<SettingsSnapshot>(jsonString)
            } catch (e: Exception) {
                Log.error("[DisplayProfileManager] Failed to load snapshot: ${e.message}")
                null
            }
        } else {
            null
        }
    }

    private fun saveSnapshot() {
        val snapshot = settingsSnapshot
        if (snapshot != null) {
            try {
                val jsonString = json.encodeToString(snapshot)
                preferences.edit().putString(KEY_SNAPSHOT, jsonString).apply()
            } catch (e: Exception) {
                Log.error("[DisplayProfileManager] Failed to save snapshot: ${e.message}")
            }
        }
    }
}
