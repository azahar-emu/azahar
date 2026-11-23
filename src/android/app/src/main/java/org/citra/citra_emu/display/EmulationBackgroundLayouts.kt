// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

package org.citra.citra_emu.display

import android.graphics.Rect
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

/** A snapshot of the native renderer's occupied screen regions, in surface pixels. */
internal data class EmulationBackgroundLayout(
    val width: Int,
    val height: Int,
    val screens: List<Rect>
)

internal object EmulationBackgroundLayouts {
    private val main = MutableStateFlow<EmulationBackgroundLayout?>(null)
    private val second = MutableStateFlow<EmulationBackgroundLayout?>(null)

    fun forDisplay(secondary: Boolean) = if (secondary) second.asStateFlow() else main.asStateFlow()

    // Called by the native layout owner, including when a surface is destroyed. No UI-thread
    // reads of the renderer/window lifetime are necessary.
    fun update(secondary: Boolean, width: Int, height: Int, rectangles: IntArray?) {
        val target = if (secondary) second else main
        if (width <= 0 || height <= 0 || rectangles == null || rectangles.size % 4 != 0) {
            target.value = null
            return
        }
        val screens = rectangles.asList().chunked(4).map { (left, top, right, bottom) ->
            Rect(
                left.coerceIn(0, width),
                top.coerceIn(0, height),
                right.coerceIn(0, width),
                bottom.coerceIn(0, height)
            )
        }.filterNot { it.isEmpty }
        // Hide the background until the renderer has supplied at least one valid game region.
        target.value =
            if (screens.isEmpty()) null else EmulationBackgroundLayout(width, height, screens)
    }
}
