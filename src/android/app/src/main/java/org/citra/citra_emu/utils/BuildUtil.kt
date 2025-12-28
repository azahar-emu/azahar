// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import org.citra.citra_emu.BuildConfig

object BuildUtil {
    fun assertNotGooglePlay() {

        @Suppress("KotlinConstantConditions", "SimplifyBooleanWithConstants")
        if (BuildConfig.FLAVOR == "googlePlay") {
            error("Non-GooglePlay code being called in GooglePlay build")
        }
    }
}
