// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import org.citra.citra_emu.BuildConfig

object BuildUtil {
    @Suppress("unused")
    object BuildFlavors {
        const val GOOGLEPLAY = "googlePlay"
        const val VANILLA = "vanilla"
    }

    fun assertNotGooglePlay() {
        if (isGooglePlayBuild) {
            error("Non-GooglePlay code being called in GooglePlay build")
        }
    }

    @Suppress("SimplifyBooleanWithConstants", "KotlinConstantConditions")
    val isGooglePlayBuild =
        BuildConfig.FLAVOR == BuildFlavors.GOOGLEPLAY
}
