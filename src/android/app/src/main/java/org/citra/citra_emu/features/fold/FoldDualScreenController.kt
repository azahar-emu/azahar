package org.citra.citra_emu.features.fold

import android.annotation.SuppressLint
import android.app.Activity
import android.content.Context
import android.graphics.Bitmap
import android.os.Binder
import android.util.Log
import android.view.LayoutInflater
import android.view.MotionEvent
import android.view.View
import android.widget.ImageView
import androidx.core.content.ContextCompat
import androidx.preference.PreferenceManager
import androidx.window.area.WindowAreaCapability
import androidx.window.area.WindowAreaController
import androidx.window.area.WindowAreaInfo
import androidx.window.area.WindowAreaPresentationSessionCallback
import androidx.window.area.WindowAreaSessionPresenter
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R

class FoldDualScreenController(
    private val activity: Activity,
    private val scope: CoroutineScope,
    private val onButtonEvent: (buttonType: Int, state: Int) -> Unit
) : WindowAreaPresentationSessionCallback {

    private val windowAreaController = WindowAreaController.getOrCreate()
    private val executor = ContextCompat.getMainExecutor(activity)

    @Volatile
    private var presentationSession: WindowAreaSessionPresenter? = null
    private var coverArtwork: Bitmap? = null
    private var collectorJob: Job? = null
    private var isStartingSession = false
    private var coverView: View? = null


    fun init() {
        Log.e("FoldController", "Initialising activity: $activity")
        collectorJob?.cancel()
        isStartingSession = false

        collectorJob = scope.launch {
            try {
                windowAreaController.windowAreaInfos.collectLatest { areaInfos ->
                    Log.e("FoldController", "WindowAreaInfos received. Count = ${areaInfos.size}")

                    val availableRearArea = areaInfos.firstOrNull { info ->
                        val capability = info.getCapability(WindowAreaCapability.Operation.OPERATION_PRESENT_ON_AREA)
                        Log.e("FoldController", "Area: ${info.type}, Status: ${capability.status}")
                        capability.status == WindowAreaCapability.Status.WINDOW_AREA_STATUS_AVAILABLE
                    }

                    if (availableRearArea != null) {
                        Log.e("FoldController", "Target Area is available, requesting start")
                        activity.runOnUiThread {
                            startCoverControls(availableRearArea)
                        }
                    } else {
                        Log.e("FoldController", "No available window area for presentation currently")
                    }
                }
            } catch (e: CancellationException) {
            } catch (t: Throwable) {
                Log.e("FoldController", "Exception in windowAreaInfos flow", t)
            }
        }
    }

    fun setCoverArtwork(bitmap: Bitmap?) {
        coverArtwork = bitmap
    }

    @Synchronized
    private fun startCoverControls(area: WindowAreaInfo) {
        val token: Binder? = area.token
        if (token == null) {
            Log.e("FoldController", "startCoverControls aborted: targetAreaInfo is null")
            return
        }

        if (presentationSession != null) {
            Log.e("FoldController", "startCoverControls aborted: token is null")
            return
        }

        if (isStartingSession) {
            Log.e("FoldController", "startCoverControls: already starting or running a session")
            return
        }

        try {
            isStartingSession = true
            Log.e("FoldController", "Calling presentContentOnWindowArea with token: $token")
            windowAreaController.presentContentOnWindowArea(
                token = token,
                activity = activity,
                executor = executor,
                windowAreaPresentationSessionCallback = this
            )
        } catch (t: Throwable) {
            Log.e("FoldController", "Failed to start window area presentation", t)
            isStartingSession = false
        }
    }

    @Synchronized
    fun stopCoverControls() {
        Log.e("FoldController", "stopCoverControls called")
        collectorJob?.cancel()
        collectorJob = null
        try {
            presentationSession?.close()
        } catch (t: Throwable) {
            Log.e("FoldController", "Error closing session", t)
        } finally {
            presentationSession = null
            isStartingSession = false
            coverView = null
        }
    }

    override fun onSessionStarted(session: WindowAreaSessionPresenter) {
        Log.e("FoldController", "onSessionStarted success")
        presentationSession = session
        isStartingSession = false

        try {
            val coverView = LayoutInflater.from(session.context).inflate(R.layout.fold_cover_controls, null)
            coverView.isFocusable = false
            coverView.isFocusableInTouchMode = false

            val artworkImageView = coverView.findViewById<ImageView>(R.id.cover_artwork_view)
            if (coverArtwork != null) {
                artworkImageView?.setImageBitmap(coverArtwork)
            }

            bindShoulderTouchZones(coverView)
            this.coverView = coverView
            applySkin(coverView)
            session.setContentView(coverView)
            Log.e("FoldController", "setContentView applied to rear screen successfully")
        } catch (t: Throwable) {
            Log.e("FoldController", "Error setting cover view", t)
        }
    }

    override fun onSessionEnded(t: Throwable?) {
        Log.e("FoldController", "onSessionEnded called. Throwable: ${t?.message}")
        presentationSession = null
        isStartingSession = false
        coverView = null
    }

    override fun onContainerVisibilityChanged(isVisible: Boolean) {
        Log.e("FoldController", "onContainerVisibilityChanged: isVisible = $isVisible")
        if (!isVisible) {
            onButtonEvent(NativeLibrary.ButtonType.TRIGGER_L, NativeLibrary.ButtonState.RELEASED)
            onButtonEvent(NativeLibrary.ButtonType.TRIGGER_R, NativeLibrary.ButtonState.RELEASED)
        }
    }

    private fun bindShoulderTouchZones(rootView: View) {
        setupTouch(rootView.findViewById(R.id.touch_zone_l), NativeLibrary.ButtonType.TRIGGER_R)
        setupTouch(rootView.findViewById(R.id.touch_zone_r), NativeLibrary.ButtonType.TRIGGER_L)
    }

    @SuppressLint("ClickableViewAccessibility")
    private fun setupTouch(view: View?, buttonType: Int) {
        view?.setOnTouchListener { v, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                    v.isPressed = true
                    onButtonEvent(buttonType, NativeLibrary.ButtonState.PRESSED)
                    true
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                    v.isPressed = false
                    onButtonEvent(buttonType, NativeLibrary.ButtonState.RELEASED)
                    true
                }
                else -> false
            }
        }
    }

    fun refreshSkin() {
        activity.runOnUiThread { coverView?.let { applySkin(it) } }
    }

    private fun applySkin(root: View) {
        val skin = isSkinEnabled(activity)
        root.findViewById<View>(R.id.skin_group)?.visibility =
            if (skin) View.VISIBLE else View.GONE
        root.findViewById<View>(R.id.plain_group)?.visibility =
            if (skin) View.GONE else View.VISIBLE
    }

    companion object {
        private const val KEY_SKIN = "fold_cover_skin_enabled"

        // view starts with plain view, press toggle to get fancy view
        fun isSkinEnabled(context: Context): Boolean =
            PreferenceManager.getDefaultSharedPreferences(context).getBoolean(KEY_SKIN, false)

        fun saveSkinEnabled(context: Context, enabled: Boolean) {
            PreferenceManager.getDefaultSharedPreferences(context)
                .edit().putBoolean(KEY_SKIN, enabled).apply()
        }
    }
}
