// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model

interface AbstractSetting<T> {
    val key: String
    val section: String
    val defaultValue: T
    val isRuntimeEditable: Boolean

    fun valueToString(value: T): String = value.toString()

    fun valueFromString(string: String): T?
}
