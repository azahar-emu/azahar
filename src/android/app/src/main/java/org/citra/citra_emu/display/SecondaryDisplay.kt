// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.display

import android.app.Presentation
import android.content.Context
import android.graphics.SurfaceTexture
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.os.Build
import android.os.Bundle
import android.util.Log
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
    var preferredDisplayId = -1
    var currentDisplayId = -1

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
        val surface = pres?.getSurfaceHolder()?.surface
        if (surface != null && surface.isValid) {
            NativeLibrary.secondarySurfaceChanged(surface)
        } else {
            Log.w("SecondaryDisplay", "Attempted to update null or invalid surface")
        }
    }

    fun destroySurface() {
        NativeLibrary.secondarySurfaceDestroyed()
    }

   fun getSecondaryDisplays(context: Context): List<Display> {
       val dm = context.getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
       val currentDisplayId = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
           context.display.displayId
       } else {
           @Suppress("DEPRECATION")
           (context.getSystemService(Context.WINDOW_SERVICE) as WindowManager)
               .defaultDisplay.displayId
       }
        val displays = dm.displays
        val presDisplays = dm.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION);
        return displays.filter {
            val isPresentable = presDisplays.any { pd -> pd.displayId == it.displayId }
            val isNotDefaultOrPresentable = it != null && it.displayId != Display.DEFAULT_DISPLAY || isPresentable
                    isNotDefaultOrPresentable &&
                    it.displayId != currentDisplayId &&
                    it.name != "HiddenDisplay" &&
                    it.state != Display.STATE_OFF &&
                    it.isValid
        }
    }

    fun updateDisplay() {
        val displays = getSecondaryDisplays(context)
        val display = if (displays.isEmpty() ||
            IntSetting.SECONDARY_DISPLAY_LAYOUT.int == SecondaryDisplayLayout.NONE.int
        ) {
            currentDisplayId = -1
            vd.display
        } else if (preferredDisplayId >=0 && displays.any { it.displayId == preferredDisplayId }) {
            currentDisplayId = preferredDisplayId
            displays.first { it.displayId == preferredDisplayId }
        } else {
            currentDisplayId = displays[0].displayId
            displays[0]
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
                Log.d("SecondaryDisplay", "Surface created")
            }

            override fun surfaceChanged(
                holder: SurfaceHolder, format: Int, width: Int, height: Int
            ) {
                Log.d("SecondaryDisplay", "Surface changed: ${width}x${height}")
                parent.updateSurface()
            }

            override fun surfaceDestroyed(holder: SurfaceHolder) {
                Log.d("SecondaryDisplay", "Surface destroyed")
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
