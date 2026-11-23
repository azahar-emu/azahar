// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

package org.citra.citra_emu.display

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Rect
import android.util.AttributeSet
import android.view.View
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.launch

/** An in-app decoration layer which never draws in a native game-screen rectangle. */
class EmulationBackgroundView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {
    var secondaryDisplay: Boolean = false
    private var observation: CoroutineScope? = null
    private var bitmap: Bitmap? = null
    private var layout: EmulationBackgroundLayout? = null
    private val paint = Paint(Paint.FILTER_BITMAP_FLAG)

    init {
        isClickable = false
        isFocusable = false
        importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
        observation = scope
        val store = EmulationBackgroundStore.get(context)
        scope.launch { store.load() }
        scope.launch {
            combine(store.image, EmulationBackgroundLayouts.forDisplay(secondaryDisplay)) {
                    image,
                    regions
                ->
                image to regions
            }.collect { (image, regions) ->
                bitmap = image
                layout = regions
                invalidate()
            }
        }
    }

    override fun onDetachedFromWindow() {
        observation?.cancel()
        observation = null
        bitmap = null
        layout = null
        super.onDetachedFromWindow()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        EmulationBackgroundPainter.draw(canvas, bitmap, layout, width, height, paint)
    }
}

internal object EmulationBackgroundPainter {
    fun draw(
        canvas: Canvas,
        bitmap: Bitmap?,
        layout: EmulationBackgroundLayout?,
        width: Int,
        height: Int,
        paint: Paint
    ) {
        val image = bitmap ?: return
        val regions = layout ?: return
        // A resize may arrive before its native layout. Draw nothing until both agree.
        if (regions.width != width || regions.height != height) return
        val saved = canvas.save()
        for (screen in regions.screens) canvas.clipOutRect(screen)
        canvas.drawBitmap(image, null, Rect(0, 0, width, height), paint)
        canvas.restoreToCount(saved)
    }
}
