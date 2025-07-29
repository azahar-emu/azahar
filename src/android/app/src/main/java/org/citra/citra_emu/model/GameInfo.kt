// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.model

import androidx.annotation.Keep
import java.io.IOException

class GameInfo(path: String) {
    @Keep
    private val pointer: Long

    init {
        pointer = initialize(path)
    }

    protected external fun finalize()

    external fun getTitle(): String

    external fun isValid(): Boolean

    external fun isEncrypted(): Boolean

    external fun getTitleID(): Long

    external fun getRegions(): String

    external fun getCompany(): String

    external fun getIcon(): IntArray?

    external fun isSystemTitle(): Boolean

    external fun getIsVisibleSystemTitle(): Boolean

    external fun getFileType(): String

    companion object {
        @JvmStatic
        private external fun initialize(path: String): Long
    }
}
