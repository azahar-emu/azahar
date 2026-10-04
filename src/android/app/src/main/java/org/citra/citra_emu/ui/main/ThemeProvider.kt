// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

package org.citra.citra_emu.ui.main

interface ThemeProvider {
    /**
     * Provides theme ID by overriding an activity's 'setTheme' method and returning that result
     */
    var themeId: Int
}
