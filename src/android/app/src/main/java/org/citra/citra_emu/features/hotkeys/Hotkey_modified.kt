// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.hotkeys

enum class Hotkey(val button: Int) {
    SWAP_SCREEN(10001),
    CYCLE_LAYOUT(10002),
    CLOSE_GAME(10003),
    PAUSE_OR_RESUME(10004),
    QUICKSAVE(10005),
    QUICKLOAD(10006),
    TURBO_LIMIT(10007),
    USE_DEFAULT_LAYOUT(10010),
    USE_SINGLESCREEN_LAYOUT(10011),
    USE_LARGESCREEN_LAYOUT(10012),
    USE_HYBRIDSCREEN_LAYOUT(10013),
    USE_SIDESCREEN_LAYOUT(10014),
    USE_SEPARATEWINDOWS_LAYOUT(10015),
    USE_CUSTOM_LAYOUT(10016);
}
