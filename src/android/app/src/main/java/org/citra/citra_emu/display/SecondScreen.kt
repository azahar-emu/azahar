import android.app.Presentation
import android.content.Context
import android.graphics.SurfaceTexture
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Display
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.display.SecondaryScreenLayout
import org.citra.citra_emu.features.settings.model.IntSetting


class SecondScreen(val context: Context) {
    private var pres:SecondScreenPresentation? = null
    private val displayManager = context.getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
    private val vd:VirtualDisplay;
    init {
        val st= SurfaceTexture(0);
        st.setDefaultBufferSize(1920,1080);
        val vdSurface = Surface(st);
        vd = displayManager.createVirtualDisplay("HiddenDisplay",1920,1080,320,vdSurface,DisplayManager.VIRTUAL_DISPLAY_FLAG_PRESENTATION)

    }
    fun updateSurface() {
        NativeLibrary.secondarySurfaceChanged(pres!!.getSurfaceHolder().surface)
    }

    fun destroySurface() {
        NativeLibrary.secondarySurfaceDestroyed()
    }

    fun updateDisplay() {
        val display = getCustomerDisplay();
        // release the presentation if appropriate
        if (pres != null && (IntSetting.SECONDARY_SCREEN_LAYOUT.int == SecondaryScreenLayout.NONE.int || display == null)) {
            releasePresentation();
        }
        if (pres == null || pres?.display != display) {
            // clearly changing
            pres?.dismiss()
            if (display != null && IntSetting.SECONDARY_SCREEN_LAYOUT.int != SecondaryScreenLayout.NONE.int) {
                pres = SecondScreenPresentation(context, display, this)
                pres?.show()
                // when the pres is created it will call updateSurface itself
            }else {
                pres = SecondScreenPresentation(context, vd.display, this)
                pres?.show()
            }
        }

    }

    private fun getCustomerDisplay(): Display? {
        val displays = displayManager?.displays;
        return displayManager?.displays?.firstOrNull { it.isValid && it.displayId != Display.DEFAULT_DISPLAY && it.name!="HiddenDisplay"}
    }

    fun releasePresentation() {
        pres?.dismiss()
        pres = null
    }

    fun releaseVD() {
        vd.release()
    }

}
class SecondScreenPresentation(
    context: Context,
    display: Display,
    val parent: SecondScreen
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
                holder: SurfaceHolder,
                format: Int,
                width: Int,
                height: Int
            ) {
            //    Handler(Looper.getMainLooper()).postDelayed({
                    // Now call Vulkan surface creation
                    parent.updateSurface()
          //      }, 200)
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
