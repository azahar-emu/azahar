// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model

interface AbstractListSetting<E> : AbstractSetting {
    var list: List<E>
}
