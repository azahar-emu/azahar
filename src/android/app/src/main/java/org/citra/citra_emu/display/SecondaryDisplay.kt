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
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.features.settings.model.IntSetting

class SecondaryDisplay(val context: Context) {
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
    }

    fun updateSurface() {
        NativeLibrary.secondarySurfaceChanged(pres!!.getSurfaceHolder().surface)
    }

    fun destroySurface() {
        NativeLibrary.secondarySurfaceDestroyed()
    }

    fun updateDisplay() {
        // decide if we are going to the external display or the internal one
        var display = getCustomerDisplay()
        if (display == null ||
            IntSetting.SECONDARY_DISPLAY_LAYOUT.int == SecondaryDisplayLayout.NONE.int) {
            display = vd.display
        }

        // if our presentation is already on the right display, ignore
        if (pres?.display == display) return;

        // otherwise, make a new presentation
        releasePresentation()
        pres = SecondaryDisplayPresentation(context, display!!, this)
        pres?.show()
    }

    private fun getCustomerDisplay(): Display? {
        val displays = displayManager.displays
        // code taken from MelonDS dual screen - should fix odin 2 detection bug
        return displayManager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)
            .firstOrNull { it.displayId != Display.DEFAULT_DISPLAY && it.name != "Built-in Screen" && it.name != "HiddenDisplay"}
    }

    fun releasePresentation() {
        pres?.dismiss()
        pres = null
    }

    fun releaseVD() {
        vd.release()
    }
}
class SecondaryDisplayPresentation(
    context: Context, display: Display, val parent: SecondaryDisplay
) : Presentation(context, display) {
    private lateinit var surfaceView: SurfaceView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

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

        setContentView(surfaceView) // Set SurfaceView as content
    }

    // Publicly accessible method to get the SurfaceHolder
    fun getSurfaceHolder(): SurfaceHolder {
        return surfaceView.holder
    }
}
