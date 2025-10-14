// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.display

import android.app.Presentation
import android.content.Context
import android.graphics.SurfaceTexture
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.os.Bundle
import android.view.Display
import android.view.MotionEvent
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.WindowManager
import org.citra.citra_emu.features.settings.model.IntSetting
import org.citra.citra_emu.display.SecondaryDisplayLayout
import org.citra.citra_emu.NativeLibrary

class SecondaryDisplay(val context: Context) : DisplayManager.DisplayListener {
    private var pres: SecondaryDisplayPresentation? = null
    private val displayManager = context.getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
    private val vd: VirtualDisplay

    init {
        vd = displayManager.createVirtualDisplay(
            "HiddenDisplay",
            1920,
            1080,
            320,
            null,
            DisplayManager.VIRTUAL_DISPLAY_FLAG_PRESENTATION
        )
        displayManager.registerDisplayListener(this, null)
    }

    fun updateSurface() {
        NativeLibrary.secondarySurfaceChanged(pres!!.getSurfaceHolder().surface)
    }

    fun destroySurface() {
        NativeLibrary.secondarySurfaceDestroyed()
    }

    private fun getExternalDisplay(context: Context): Display? {
        val dm = context.getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
        val internalId = context.display.displayId ?: Display.DEFAULT_DISPLAY
        val displays = dm.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)
        return displays.firstOrNull { it.displayId != internalId && it.name != "HiddenDisplay" }
    }

    fun updateDisplay() {
        // decide if we are going to the external display or the internal one
        var display = getExternalDisplay(context)
        if (display == null ||
            IntSetting.SECONDARY_DISPLAY_LAYOUT.int == SecondaryDisplayLayout.NONE.int) {
            display = vd.display
        }

        // if our presentation is already on the right display, ignore
        if (pres?.display == display) return

        // otherwise, make a new presentation
        releasePresentation()
        pres = SecondaryDisplayPresentation(context, display!!, this)
        pres?.show()
    }

    fun releasePresentation() {
        pres?.dismiss()
        pres = null
    }

    fun releaseVD() {
        displayManager.unregisterDisplayListener(this)
        vd.release()
    }

    override fun onDisplayAdded(displayId: Int) {
        updateDisplay()
    }

    override fun onDisplayRemoved(displayId: Int) {
        updateDisplay()
    }
    override fun onDisplayChanged(displayId: Int) {
        updateDisplay()
    }
}
class SecondaryDisplayPresentation(
    context: Context, display: Display, val parent: SecondaryDisplay
) : Presentation(context, display) {
    private lateinit var surfaceView: SurfaceView
    private var touchscreenPointerId = -1

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window?.setFlags(
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL
        )

        // Initialize SurfaceView
        surfaceView = SurfaceView(context)
        surfaceView.holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) {

            }

            override fun surfaceChanged(
                holder: SurfaceHolder, format: Int, width: Int, height: Int
            ) {
                parent.updateSurface()
            }

            override fun surfaceDestroyed(holder: SurfaceHolder) {
                parent.destroySurface()
            }
        })

        this.surfaceView.setOnTouchListener { _, event ->

            val pointerIndex = event.actionIndex
            val pointerId = event.getPointerId(pointerIndex)
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                    if (touchscreenPointerId == -1) {
                        touchscreenPointerId = pointerId
                        NativeLibrary.onSecondaryTouchEvent(
                            event.getX(pointerIndex),
                            event.getY(pointerIndex),
                            true
                        )
                    }
                }

                MotionEvent.ACTION_MOVE -> {
                    val index = event.findPointerIndex(touchscreenPointerId)
                    if (index != -1) {
                        NativeLibrary.onSecondaryTouchMoved(
                            event.getX(index),
                            event.getY(index)
                        )
                    }
                }

                MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                    if (pointerId == touchscreenPointerId) {
                        NativeLibrary.onSecondaryTouchEvent(0f, 0f, false)
                        touchscreenPointerId = -1
                    }
                }
            }
            true
        }

        setContentView(surfaceView) // Set SurfaceView as content
    }

    // Publicly accessible method to get the SurfaceHolder
    fun getSurfaceHolder(): SurfaceHolder {
        return surfaceView.holder
    }
}
