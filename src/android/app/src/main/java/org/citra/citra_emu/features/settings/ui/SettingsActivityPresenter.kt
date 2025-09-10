// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.ui

import android.net.Uri
import android.os.Bundle
import android.text.TextUtils
import androidx.documentfile.provider.DocumentFile
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.features.settings.model.BooleanSetting
import org.citra.citra_emu.features.settings.model.Settings
import org.citra.citra_emu.utils.SystemSaveGame
import org.citra.citra_emu.utils.DirectoryInitialization
import org.citra.citra_emu.utils.FileUtil
import org.citra.citra_emu.utils.Log
import org.citra.citra_emu.utils.PermissionsHandler
import org.citra.citra_emu.utils.TurboHelper

class SettingsActivityPresenter(private val activityView: SettingsActivityView) {
    val settings: Settings get() = activityView.settings

    private var shouldSave = false
    private lateinit var menuTag: String
    private lateinit var gameId: String

    fun onCreate(savedInstanceState: Bundle?, menuTag: String, gameId: String) {
        this.menuTag = menuTag
        this.gameId = gameId
        if (savedInstanceState != null) {
            shouldSave = savedInstanceState.getBoolean(KEY_SHOULD_SAVE)
        }
    }

    fun onResume() {
        SystemSaveGame.load()
    }

    fun onPause() {
        SystemSaveGame.save()
    }

    fun onStart() {
        prepareDirectoriesIfNeeded()
    }

    private fun loadSettingsUI() {
        if (!settings.isLoaded) {
            if (!TextUtils.isEmpty(gameId)) {
                settings.loadSettings(gameId, activityView)
            } else {
                settings.loadSettings(activityView)
            }
        }
        activityView.showSettingsFragment(menuTag, false, gameId)
        activityView.onSettingsFileLoaded()
    }

    private fun prepareDirectoriesIfNeeded() {
        if (!DirectoryInitialization.areCitraDirectoriesReady()) {
            DirectoryInitialization.start()
        }
        loadSettingsUI()
    }

    private fun updateAndroidImageVisibility() {
        val dataDirTreeUri: Uri
        val dataDirDocument: DocumentFile
        val nomediaFileDocument: DocumentFile?
        val nomediaFileExists: Boolean
        try {
            dataDirTreeUri = PermissionsHandler.citraDirectory
            dataDirDocument = DocumentFile.fromTreeUri(CitraApplication.appContext, dataDirTreeUri)!!
            nomediaFileDocument = dataDirDocument.findFile(".nomedia")
            nomediaFileExists = (nomediaFileDocument != null)
        } catch (e: Exception) {
            Log.error("[SettingsActivity]: Error occurred while trying to find .nomedia, error: " + e.message)
            return
        }

        if (BooleanSetting.ANDROID_HIDE_IMAGES.boolean) {
            if (!nomediaFileExists) {
                Log.info("[SettingsActivity]: Attempting to create .nomedia in user data directory")
                FileUtil.createFile(dataDirTreeUri.toString(), ".nomedia")
            }
        } else if (nomediaFileExists) {
            Log.info("[SettingsActivity]: Attempting to delete .nomedia in user data directory")
            nomediaFileDocument!!.delete()
        }
    }

    fun onStop(finishing: Boolean) {
        if (finishing && shouldSave) {
            Log.debug("[SettingsActivity] Settings activity stopping. Saving settings to INI...")
            settings.saveSettings(activityView)
            //added to ensure that layout changes take effect as soon as settings window closes
            NativeLibrary.reloadSettings()
            NativeLibrary.updateFramebuffer(NativeLibrary.isPortraitMode)
            updateAndroidImageVisibility()
            TurboHelper.reloadTurbo(false) // TODO: Can this go somewhere else? -OS
        }
        NativeLibrary.reloadSettings()
    }

    fun onSettingChanged() {
        shouldSave = true
    }

    fun onSettingsReset() {
        shouldSave = false
    }

    fun saveState(outState: Bundle) {
        outState.putBoolean(KEY_SHOULD_SAVE, shouldSave)
    }

    companion object {
        private const val KEY_SHOULD_SAVE = "should_save"
    }
}
