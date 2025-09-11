package org.citra.citra_emu.display

import android.app.Activity
import android.content.Context
import android.hardware.display.DisplayManager
import android.view.Display
import org.citra.citra_emu.features.settings.model.IntSetting
import org.citra.citra_emu.display.SecondaryDisplayLayout

/**
 * Utility object that tracks where the application was initially launched and
 * provides helper functions to retrieve the logical "internal" and "external"
 * displays. If the app was started on an external monitor the roles of the
 * displays are swapped so that the secondary presentation always appears on the
 * opposite screen.
 */
object DisplayHelper {
    private var launchedOnExternal: Boolean? = null

    /**
     * Inspect the Activity's display on creation and remember if the
     * application was launched on an external monitor. This method should be
     * called from every Activity's onCreate.
     */
    fun checkLaunchDisplay(activity: Activity) {
        if (launchedOnExternal == null) {
            val displayId = activity.display?.displayId ?: Display.DEFAULT_DISPLAY
            launchedOnExternal = displayId != Display.DEFAULT_DISPLAY
        }
    }

    /** Whether the application was initially launched on an external display. */
    fun isLaunchedOnExternal(): Boolean = launchedOnExternal == true

    private fun getPresentationDisplay(dm: DisplayManager): Display? {
        return dm.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)
            .firstOrNull { it.displayId != Display.DEFAULT_DISPLAY && it.name != "Built-in Screen" && it.name != "HiddenDisplay" }
    }

    /**
     * Return the display that should be considered the internal one according
     * to the launch state.
     */
    fun getInternalDisplay(context: Context): Display? {
        val dm = context.getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
        return if (isLaunchedOnExternal()) {
            getPresentationDisplay(dm) ?: dm.getDisplay(Display.DEFAULT_DISPLAY)
        } else {
            dm.getDisplay(Display.DEFAULT_DISPLAY)
        }
    }

    /**
     * Return the display that should be considered the external one according
     * to the launch state.
     */
    fun getExternalDisplay(context: Context): Display? {
        val dm = context.getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
        return if (isLaunchedOnExternal()) {
            dm.getDisplay(Display.DEFAULT_DISPLAY)
        } else {
            getPresentationDisplay(dm)
        }
    }

    /**
     * Returns true if the primary display is currently showing the 3DS bottom
     * screen.
     */
    fun isBottomOnPrimary(): Boolean {
        val layout = SecondaryDisplayLayout.from(IntSetting.SECONDARY_DISPLAY_LAYOUT.int)
        return layout != SecondaryDisplayLayout.BOTTOM_SCREEN &&
            layout != SecondaryDisplayLayout.SIDE_BY_SIDE
    }

    /** Returns true if the secondary presentation hosts the 3DS bottom screen. */
    fun isBottomOnSecondary(): Boolean = !isBottomOnPrimary()
}
