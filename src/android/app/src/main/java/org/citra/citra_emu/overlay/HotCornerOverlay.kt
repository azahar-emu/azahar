// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.overlay

import android.content.Context
import android.content.res.Configuration
import android.util.AttributeSet
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.view.Gravity
import android.view.View
import android.view.MotionEvent
import android.widget.FrameLayout
import org.citra.citra_emu.R
import org.citra.citra_emu.utils.HotCornerSettings

class HotCornerOverlay @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : FrameLayout(context, attrs, defStyleAttr) {

    interface OnActionListener {
        fun onHotCornerAction(action: HotCornerSettings.HotCornerAction)
    }

    var actionListener: OnActionListener? = null
    private var previewEnabled = false
    private var previewRadiusPx: Int? = null

    init {
        isClickable = false
        isFocusable = false
        importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO
    }

    fun refresh() {
        removeAllViews()

        val orientation = resources.configuration.orientation
        val radiusDp = HotCornerSettings.getRadiusDp()
        val sizePx = previewRadiusPx ?: (radiusDp * resources.displayMetrics.density).toInt()

        addCornerIfNeeded(
            sizePx,
            Gravity.BOTTOM or Gravity.START,
            HotCornerSettings.getAction(orientation, HotCornerSettings.HotCornerPosition.BOTTOM_LEFT)
        )

        addCornerIfNeeded(
            sizePx,
            Gravity.BOTTOM or Gravity.END,
            HotCornerSettings.getAction(orientation, HotCornerSettings.HotCornerPosition.BOTTOM_RIGHT)
        )

        if (previewEnabled) addPreviewOverlay(sizePx)
    }

    private fun addCornerIfNeeded(sizePx: Int, gravity: Int, action: HotCornerSettings.HotCornerAction) {
        if (action == HotCornerSettings.HotCornerAction.NONE) return

        val position = if ((gravity and Gravity.START) == Gravity.START) {
            HotCornerSettings.HotCornerPosition.BOTTOM_LEFT
        } else {
            HotCornerSettings.HotCornerPosition.BOTTOM_RIGHT
        }

        val v = QuarterCircleTouchView(context, position, sizePx).apply {
            layoutParams = LayoutParams(sizePx, sizePx, gravity)
            contentDescription = action.name
            setOnClickListener { actionListener?.onHotCornerAction(action) }
        }
        addView(v)
    }

    fun showPreview(enable: Boolean) {
        previewEnabled = enable
        if (!enable) previewRadiusPx = null
        refresh()
    }

    fun updatePreviewRadiusDp(dp: Int) {
        previewRadiusPx = (dp * resources.displayMetrics.density).toInt()
        refresh()
    }

    private fun addPreviewOverlay(sizePx: Int) {
        val overlayColor = 0x80FF0000.toInt() // 50% alpha red

        val left = QuarterCirclePreviewView(
            context,
            HotCornerSettings.HotCornerPosition.BOTTOM_LEFT,
            sizePx,
            overlayColor
        ).apply {
            layoutParams = LayoutParams(sizePx, sizePx, Gravity.BOTTOM or Gravity.START)
        }

        val right = QuarterCirclePreviewView(
            context,
            HotCornerSettings.HotCornerPosition.BOTTOM_RIGHT,
            sizePx,
            overlayColor
        ).apply {
            layoutParams = LayoutParams(sizePx, sizePx, Gravity.BOTTOM or Gravity.END)
        }

        addView(left)
        addView(right)
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        refresh()
    }
}

private class QuarterCircleTouchView(
    context: Context,
    private val position: HotCornerSettings.HotCornerPosition,
    private val radiusPx: Int
) : View(context) {
    private var downInside = false

    init {
        isClickable = true
        isFocusable = false
        background = null
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val inside = isInsideQuarterCircle(event.x, event.y)
        return when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                if (inside) {
                    downInside = true
                    true
                } else {
                    downInside = false
                    false
                }
            }
            MotionEvent.ACTION_UP -> {
                val handled = downInside && inside
                if (handled) performClick()
                downInside = false
                handled
            }
            MotionEvent.ACTION_CANCEL -> {
                downInside = false
                false
            }
            else -> downInside
        }
    }

    override fun performClick(): Boolean {
        return super.performClick()
    }

    private fun isInsideQuarterCircle(x: Float, y: Float): Boolean {
        val r = radiusPx.toFloat()
        val (cx, cy) = when (position) {
            HotCornerSettings.HotCornerPosition.BOTTOM_LEFT -> 0f to r
            HotCornerSettings.HotCornerPosition.BOTTOM_RIGHT -> r to r
        }

        val dx = x - cx
        val dy = y - cy
        // Constrain to the visible quadrant only
        val inQuadrant = when (position) {
            HotCornerSettings.HotCornerPosition.BOTTOM_LEFT -> (dx >= 0f && dy <= 0f)
            HotCornerSettings.HotCornerPosition.BOTTOM_RIGHT -> (dx <= 0f && dy <= 0f)
        }
        return inQuadrant && (dx * dx + dy * dy <= r * r)
    }
}

private class QuarterCirclePreviewView(
    context: Context,
    private val position: HotCornerSettings.HotCornerPosition,
    private val radiusPx: Int,
    color: Int
) : View(context) {
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.FILL
        this.color = color
    }
    private val oval = RectF()

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val r = radiusPx.toFloat()
        when (position) {
            HotCornerSettings.HotCornerPosition.BOTTOM_LEFT -> {
                // Center at (0, r), rect spans [-r, 0]..[r, 2r]
                oval.set(-r, 0f, r, 2f * r)
                canvas.drawArc(oval, 270f, 90f, true, paint)
            }
            HotCornerSettings.HotCornerPosition.BOTTOM_RIGHT -> {
                // Center at (r, r), rect spans [0, 0]..[2r, 2r]
                oval.set(0f, 0f, 2f * r, 2f * r)
                canvas.drawArc(oval, 180f, 90f, true, paint)
            }
        }
    }
}