// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.ui

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View

/**
 * A custom View that renders a preview of the 3DS bottom screen (320x240)
 * with draggable dots representing mapped touch points.
 */
class TouchScreenPreviewView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : View(context, attrs, defStyleAttr) {

    companion object {
        const val SCREEN_WIDTH = 320
        const val SCREEN_HEIGHT = 240
        private const val DOT_RADIUS = 12f
        private const val DRAG_THRESHOLD = 30f
    }

    data class TouchPoint(var x: Int, var y: Int, val buttonId: Int)

    private val points = mutableListOf<TouchPoint>()
    private var selectedIndex = -1
    private var draggingIndex = -1
    private var tapMode = false

    private val screenRect = RectF()

    private val screenPaint = Paint().apply {
        color = Color.BLACK
        style = Paint.Style.FILL
    }
    private val borderPaint = Paint().apply {
        color = Color.GRAY
        style = Paint.Style.STROKE
        strokeWidth = 2f
    }
    private val dotPaint = Paint().apply {
        color = Color.RED
        style = Paint.Style.FILL
        isAntiAlias = true
    }
    private val selectedDotPaint = Paint().apply {
        color = Color.YELLOW
        style = Paint.Style.FILL
        isAntiAlias = true
    }
    private val textPaint = Paint().apply {
        color = Color.WHITE
        textSize = 24f
        textAlign = Paint.Align.CENTER
        isAntiAlias = true
    }

    var onPointAdded: ((x: Int, y: Int) -> Unit)? = null
    var onPointMoved: ((index: Int, x: Int, y: Int) -> Unit)? = null
    var onPointSelected: ((index: Int) -> Unit)? = null

    fun setPoints(newPoints: List<TouchPoint>) {
        points.clear()
        points.addAll(newPoints)
        invalidate()
    }

    fun addPoint(point: TouchPoint) {
        points.add(point)
        invalidate()
    }

    fun removePoint(index: Int) {
        if (index in points.indices) {
            points.removeAt(index)
            if (selectedIndex == index) selectedIndex = -1
            else if (selectedIndex > index) selectedIndex--
            invalidate()
        }
    }

    fun setSelectedIndex(index: Int) {
        selectedIndex = index
        invalidate()
    }

    fun enableTapMode() {
        tapMode = true
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        calculateScreenRect(w, h)
    }

    private fun calculateScreenRect(viewWidth: Int, viewHeight: Int) {
        val aspectRatio = SCREEN_WIDTH.toFloat() / SCREEN_HEIGHT.toFloat()
        val viewAspect = viewWidth.toFloat() / viewHeight.toFloat()

        val screenW: Float
        val screenH: Float
        if (viewAspect > aspectRatio) {
            screenH = viewHeight.toFloat()
            screenW = screenH * aspectRatio
        } else {
            screenW = viewWidth.toFloat()
            screenH = screenW / aspectRatio
        }

        val left = (viewWidth - screenW) / 2f
        val top = (viewHeight - screenH) / 2f
        screenRect.set(left, top, left + screenW, top + screenH)
    }

    private fun screenToView(screenX: Int, screenY: Int): Pair<Float, Float> {
        val vx = screenRect.left + (screenX.toFloat() / SCREEN_WIDTH) * screenRect.width()
        val vy = screenRect.top + (screenY.toFloat() / SCREEN_HEIGHT) * screenRect.height()
        return Pair(vx, vy)
    }

    private fun viewToScreen(viewX: Float, viewY: Float): Pair<Int, Int> {
        val sx = ((viewX - screenRect.left) / screenRect.width() * SCREEN_WIDTH)
            .toInt().coerceIn(0, SCREEN_WIDTH - 1)
        val sy = ((viewY - screenRect.top) / screenRect.height() * SCREEN_HEIGHT)
            .toInt().coerceIn(0, SCREEN_HEIGHT - 1)
        return Pair(sx, sy)
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)

        // Draw screen background
        canvas.drawRect(screenRect, screenPaint)
        canvas.drawRect(screenRect, borderPaint)

        // Draw grid lines for reference
        val gridPaint = Paint().apply {
            color = Color.DKGRAY
            style = Paint.Style.STROKE
            strokeWidth = 1f
        }
        for (i in 1..3) {
            val x = screenRect.left + screenRect.width() * i / 4f
            canvas.drawLine(x, screenRect.top, x, screenRect.bottom, gridPaint)
        }
        for (i in 1..2) {
            val y = screenRect.top + screenRect.height() * i / 3f
            canvas.drawLine(screenRect.left, y, screenRect.right, y, gridPaint)
        }

        // Draw coordinate label
        val labelPaint = Paint().apply {
            color = Color.GRAY
            textSize = 20f
            isAntiAlias = true
        }
        canvas.drawText("3DS Bottom Screen (320×240)", screenRect.left + 8, screenRect.top + 20, labelPaint)

        // Draw touch points
        points.forEachIndexed { index, point ->
            val (vx, vy) = screenToView(point.x, point.y)
            val paint = if (index == selectedIndex) selectedDotPaint else dotPaint
            canvas.drawCircle(vx, vy, DOT_RADIUS, paint)
            canvas.drawText("${index + 1}", vx, vy + 6f, textPaint)
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.action) {
            MotionEvent.ACTION_DOWN -> {
                val touchX = event.x
                val touchY = event.y

                if (tapMode) {
                    if (screenRect.contains(touchX, touchY)) {
                        val (sx, sy) = viewToScreen(touchX, touchY)
                        tapMode = false
                        onPointAdded?.invoke(sx, sy)
                        return true
                    }
                    return true
                }

                // Check if touching an existing point
                draggingIndex = -1
                for (i in points.indices.reversed()) {
                    val (vx, vy) = screenToView(points[i].x, points[i].y)
                    val dx = touchX - vx
                    val dy = touchY - vy
                    if (dx * dx + dy * dy < DRAG_THRESHOLD * DRAG_THRESHOLD) {
                        draggingIndex = i
                        selectedIndex = i
                        onPointSelected?.invoke(i)
                        invalidate()
                        return true
                    }
                }
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                if (draggingIndex >= 0 && screenRect.contains(event.x, event.y)) {
                    val (sx, sy) = viewToScreen(event.x, event.y)
                    points[draggingIndex].x = sx
                    points[draggingIndex].y = sy
                    invalidate()
                    return true
                }
            }
            MotionEvent.ACTION_UP -> {
                if (draggingIndex >= 0) {
                    val (sx, sy) = viewToScreen(event.x, event.y)
                    points[draggingIndex].x = sx.coerceIn(0, SCREEN_WIDTH - 1)
                    points[draggingIndex].y = sy.coerceIn(0, SCREEN_HEIGHT - 1)
                    onPointMoved?.invoke(draggingIndex, points[draggingIndex].x, points[draggingIndex].y)
                    draggingIndex = -1
                    invalidate()
                    return true
                }
            }
        }
        return super.onTouchEvent(event)
    }
}
