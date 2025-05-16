// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import android.content.SharedPreferences
import androidx.preference.PreferenceManager
import org.citra.citra_emu.CitraApplication

object CitraDirectoryUtils {
    const val CITRA_DIRECTORY = "CITRA_DIRECTORY"
    const val LIME3DS_DIRECTORY = "LIME3DS_DIRECTORY"
    val preferences: SharedPreferences =
        PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)

    fun needToUpdateManually(): Boolean {
        val directoryString = preferences.getString(CITRA_DIRECTORY, "")
        val limeDirectoryString = preferences.getString(LIME3DS_DIRECTORY,"")
        return (directoryString != "" && limeDirectoryString != "" && directoryString != limeDirectoryString)
    }

    fun attemptAutomaticUpdateDirectory() {
        val directoryString = preferences.getString(CITRA_DIRECTORY, "")
        val limeDirectoryString = preferences.getString(LIME3DS_DIRECTORY,"")
        if (needToUpdateManually()) {
            return;
        }
       if (directoryString == "" && limeDirectoryString != "") {
            // Upgrade from Lime3DS to Azahar
           PermissionsHandler.setCitraDirectory(limeDirectoryString)
            removeLimeDirectoryPreference()
            DirectoryInitialization.resetCitraDirectoryState()
            DirectoryInitialization.start()

       } else if (directoryString != "" && directoryString == limeDirectoryString) {
            // Both the Lime3DS and Azahar directories are the same,
            // so delete the obsolete Lime3DS value.
            removeLimeDirectoryPreference()
        }
    }

    fun removeLimeDirectoryPreference() {
        preferences.edit().remove(LIME3DS_DIRECTORY).apply()
    }
}
