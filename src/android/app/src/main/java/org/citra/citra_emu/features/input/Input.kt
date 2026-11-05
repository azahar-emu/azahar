// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

package org.citra.citra_emu.features.input

/**
 * Stores information about a particular keyboard or gamepad input
 */
data class Input(
    val key: Int? = null,
    val axis: Int? = null,
    // +1 or -1
    val direction: Int? = null,
    val threshold: Float? = null
) {
    val empty: Boolean
        get() = key == null && axis == null
}
