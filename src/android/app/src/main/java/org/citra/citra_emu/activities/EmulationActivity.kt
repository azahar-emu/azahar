// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.activities

import android.Manifest.permission
import android.annotation.SuppressLint
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Bundle
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.Window
import android.view.WindowManager
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.appcompat.app.AppCompatActivity
import androidx.core.net.toUri
import androidx.core.os.BundleCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.navigation.fragment.NavHostFragment
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.NativeLibrary.ButtonState
import org.citra.citra_emu.R
import org.citra.citra_emu.camera.StillImageCameraHelper.OnFilePickerResult
import org.citra.citra_emu.contracts.OpenFileResultContract
import org.citra.citra_emu.databinding.ActivityEmulationBinding
import org.citra.citra_emu.display.ScreenAdjustmentUtil
import org.citra.citra_emu.display.SecondaryDisplay
import org.citra.citra_emu.features.input.GamepadHelper
import org.citra.citra_emu.features.input.HotkeyUtility
import org.citra.citra_emu.features.settings.model.BooleanSetting
import org.citra.citra_emu.features.settings.model.IntSetting
import org.citra.citra_emu.features.settings.model.Settings
import org.citra.citra_emu.fragments.EmulationFragment
import org.citra.citra_emu.fragments.MessageDialogFragment
import org.citra.citra_emu.model.Game
import org.citra.citra_emu.utils.BuildUtil
import org.citra.citra_emu.utils.FileBrowserHelper
import org.citra.citra_emu.utils.EmulationLifecycleUtil
import org.citra.citra_emu.utils.EmulationMenuSettings
import org.citra.citra_emu.utils.Log
import org.citra.citra_emu.utils.RefreshRateUtil
import org.citra.citra_emu.utils.ThemeUtil
import org.citra.citra_emu.viewmodel.EmulationViewModel
import org.citra.citra_emu.features.settings.utils.SettingsFile
import kotlin.math.abs

class EmulationActivity : AppCompatActivity() {
    var isActivityRecreated = false
    val emulationViewModel: EmulationViewModel by viewModels()
    private lateinit var binding: ActivityEmulationBinding
    private lateinit var screenAdjustmentUtil: ScreenAdjustmentUtil
    private lateinit var hotkeyUtility: HotkeyUtility
    private lateinit var secondaryDisplay: SecondaryDisplay

    private val onShutdown = Runnable {
        if (intent.getBooleanExtra("launched_from_shortcut", false)) {
            finishAffinity()
        } else {
            this.finish()
        }
    }

    private val emulationFragment: EmulationFragment
        get() {
            val navHostFragment =
                supportFragmentManager.findFragmentById(R.id.fragment_container) as NavHostFragment
            return navHostFragment.getChildFragmentManager().fragments.last() as EmulationFragment
        }

    private var isEmulationRunning: Boolean = false

    override fun onCreate(savedInstanceState: Bundle?) {
        requestWindowFeature(Window.FEATURE_NO_TITLE)

        RefreshRateUtil.enforceRefreshRate(this, sixtyHz = true)

        ThemeUtil.setTheme(this)
        val game = try {
            intent.extras?.let { extras ->
                BundleCompat.getParcelable(extras, "game", Game::class.java)
            } ?: run {
                Log.error("[EmulationActivity] Missing game data in intent extras")
                return
            }
        } catch (e: Exception) {
            Log.error("[EmulationActivity] Failed to retrieve game data: ${e.message}")
            return
        }
        // load global settings if for some reason they aren't (should be loaded in MainActivity)
        if (Settings.settings.getAllGlobal().isEmpty()) {
            SettingsFile.loadSettings(Settings.settings)
        }
        // load per-game settings
        SettingsFile.loadSettings(Settings.settings, String.format("%016X", game.titleId))

        super.onCreate(savedInstanceState)

        secondaryDisplay = SecondaryDisplay(this, Settings.settings)
        secondaryDisplay.updateDisplay()

        binding = ActivityEmulationBinding.inflate(layoutInflater)
        screenAdjustmentUtil = ScreenAdjustmentUtil(this, windowManager, Settings.settings)
        hotkeyUtility = HotkeyUtility(screenAdjustmentUtil, this, Settings.settings)
        setContentView(binding.root)

        val navHostFragment =
            supportFragmentManager.findFragmentById(R.id.fragment_container) as NavHostFragment
        val navController = navHostFragment.navController
        navController.setGraph(R.navigation.emulation_navigation, intent.extras)

        isActivityRecreated = savedInstanceState != null

        // Set these options now so that the SurfaceView the game renders into is the right size.
        enableFullscreenImmersive()

        // Override Citra core INI with the one set by our in game menu
        NativeLibrary.swapScreens(
            EmulationMenuSettings.swapScreens,
            windowManager.defaultDisplay.rotation
        )

        EmulationLifecycleUtil.addShutdownHook(onShutdown)

        isEmulationRunning = true
        instance = this

        applyOrientationSettings() // Check for orientation settings at startup

        NativeLibrary.playTimeManagerStart(game.titleId)
    }

    // On some devices, the system bars will not disappear on first boot or after some
    // rotations. Here we set full screen immersive repeatedly in onResume and in
    // onWindowFocusChanged to prevent the unwanted status bar state.
    override fun onResume() {
        super.onResume()
        enableFullscreenImmersive()
        applyOrientationSettings()
    }

    override fun onStop() {
        secondaryDisplay.releasePresentation()
        super.onStop()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        enableFullscreenImmersive()
    }

    public override fun onRestart() {
        super.onRestart()
        secondaryDisplay.updateDisplay()
        NativeLibrary.reloadCameraDevices()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putBoolean("isEmulationRunning", isEmulationRunning)
    }

    override fun onRestoreInstanceState(savedInstanceState: Bundle) {
        super.onRestoreInstanceState(savedInstanceState)
        isEmulationRunning = savedInstanceState.getBoolean("isEmulationRunning", false)
    }

    override fun onDestroy() {
        EmulationLifecycleUtil.removeHook(onShutdown)
        NativeLibrary.playTimeManagerStop()
        isEmulationRunning = false
        instance = null
        secondaryDisplay.releasePresentation()
        secondaryDisplay.releaseVD()

        Settings.settings.removePerGameSettings()

        super.onDestroy()
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<String>,
        grantResults: IntArray
    ) {
        when (requestCode) {
            NativeLibrary.REQUEST_CODE_NATIVE_CAMERA -> {
                if (grantResults[0] != PackageManager.PERMISSION_GRANTED &&
                    shouldShowRequestPermissionRationale(permission.CAMERA)
                ) {
                    MessageDialogFragment.newInstance(
                        R.string.camera,
                        R.string.camera_permission_needed
                    ).show(supportFragmentManager, MessageDialogFragment.TAG)
                }
                NativeLibrary.cameraPermissionResult(
                    grantResults[0] == PackageManager.PERMISSION_GRANTED
                )
            }

            NativeLibrary.REQUEST_CODE_NATIVE_MIC -> {
                if (grantResults[0] != PackageManager.PERMISSION_GRANTED &&
                    shouldShowRequestPermissionRationale(permission.RECORD_AUDIO)
                ) {
                    MessageDialogFragment.newInstance(
                        R.string.microphone,
                        R.string.microphone_permission_needed
                    ).show(supportFragmentManager, MessageDialogFragment.TAG)
                }
                NativeLibrary.micPermissionResult(
                    grantResults[0] == PackageManager.PERMISSION_GRANTED
                )
            }

            else -> super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        }
    }

    fun onEmulationStarted() {
        emulationViewModel.setEmulationStarted(true)
        Toast.makeText(
            applicationContext,
            getString(R.string.emulation_menu_help),
            Toast.LENGTH_LONG
        ).show()
    }

    fun enableFullscreenImmersive() {
        val attributes = window.attributes

        attributes.layoutInDisplayCutoutMode =
            if (Settings.settings.get(BooleanSetting.EXPAND_TO_CUTOUT_AREA)) {
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
            } else {
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_NEVER
            }

        window.attributes = attributes

        WindowCompat.setDecorFitsSystemWindows(window, false)

        WindowInsetsControllerCompat(window, window.decorView).let { controller ->
            controller.hide(WindowInsetsCompat.Type.systemBars())
            controller.systemBarsBehavior =
                WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
    }

    fun applyOrientationSettings() {
        val orientationOption = Settings.settings.get(IntSetting.ORIENTATION_OPTION)
        screenAdjustmentUtil.changeActivityOrientation(orientationOption)
    }

    // Gets button presses
    @Suppress("DEPRECATION")
    @SuppressLint("GestureBackNavigation")
    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        // TODO: Move this check into native code - prevents crash if input pressed before starting emulation
        if (!NativeLibrary.isRunning()) {
            return false
        }

        if (emulationFragment.isDrawerOpen()) {
            return super.dispatchKeyEvent(event)
        }

        when (event.action) {
            KeyEvent.ACTION_DOWN -> {
                // On some devices, the back gesture / button press is not intercepted by androidx
                // and fails to open the emulation menu. So we're stuck running deprecated code to
                // cover for either a fault on androidx's side or in OEM skins (MIUI at least)

                if (event.keyCode == KeyEvent.KEYCODE_BACK) {
                    // If the hotkey is pressed, we don't want to open the drawer
                    if (!hotkeyUtility.hotkeyIsPressed) {
                        onBackPressed()
                        return true
                    }
                }
                return hotkeyUtility.handleKeyPress(if (event.keyCode == 0) event.scanCode else event.keyCode, event.device.descriptor)
            }
            KeyEvent.ACTION_UP -> {
                return hotkeyUtility.handleKeyRelease(if (event.keyCode == 0) event.scanCode else event.keyCode, event.device.descriptor)
            }
            else -> {
                return false
            }
        }
    }

    private fun onAmiiboSelected(selectedFile: String) {
        val success = NativeLibrary.loadAmiibo(selectedFile)
        if (!success) {
            Log.error("[EmulationActivity] Failed to load Amiibo file: $selectedFile")
            MessageDialogFragment.newInstance(
                R.string.amiibo_load_error,
                R.string.amiibo_load_error_message
            ).show(supportFragmentManager, MessageDialogFragment.TAG)
        }
    }

    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        // TODO: Move this check into native code - prevents crash if input pressed before starting emulation
        if (!NativeLibrary.isRunning() ||
            (event.source and InputDevice.SOURCE_CLASS_JOYSTICK == 0) ||
            emulationFragment.isDrawerOpen()
        ) {
            return super.dispatchGenericMotionEvent(event)
        }

        // Don't attempt to do anything if we are disconnecting a device.
        if (event.actionMasked == MotionEvent.ACTION_CANCEL) {
            return true
        }
        val device = event.device
        val manager = emulationViewModel.settings.inputMappingManager

        val stickAccumulator = HashMap<Int, Pair<Float, Float>>()

        for (range in device.motionRanges) {
            val axis = range.axis
            val origValue = event.getAxisValue(axis)
            var value = GamepadHelper.scaleAxis(device, axis, origValue)
            if (value > -0.1f && value < 0.1f) value = 0f

            val axisPair = Pair(axis, if (value >= 0f) 1 else -1)

            // special case where the axis value is 0 - we need to send releases to both directions
            if (value == 0f) {
                listOf(Pair(axis, 1), Pair(axis, -1)).forEach { zeroPair ->
                    manager.getOutAxesForAxis(zeroPair).forEach { (outAxis, outDir) ->
                        val component =
                            GamepadHelper.getJoystickComponent(outAxis) ?: return@forEach
                        val current =
                            stickAccumulator.getOrDefault(component.joystickType, Pair(0f, 0f))
                        stickAccumulator[component.joystickType] = if (component.isVertical) {
                            Pair(current.first, 0f)
                        } else {
                            Pair(0f, current.second)
                        }
                    }
                    manager.getOutButtonsForAxis(zeroPair).forEach { outButton ->
                        NativeLibrary.onGamePadEvent(
                            device.descriptor,
                            outButton,
                            ButtonState.RELEASED
                        )
                    }
                }
                continue
            }
            // Axis to Axis mappings
            manager.getOutAxesForAxis(axisPair).forEach { (outAxis, outDir) ->
                val component = GamepadHelper.getJoystickComponent(outAxis) ?: return@forEach
                val current =
                    stickAccumulator.getOrDefault(component.joystickType, Pair(0f, 0f))
                val contribution = abs(value) * outDir
                stickAccumulator[component.joystickType] = if (component.isVertical) {
                    Pair(current.first, contribution)
                } else {
                    Pair(contribution, current.second)
                }
            }

            // Axis to Button mappings
            manager.getOutButtonsForAxis(axisPair).forEach { button ->
                val mapping = manager.getMappingForButton(button)
                if (abs(value) > (mapping?.threshold ?: 0.5f)) {
                    hotkeyUtility.handleKeyPress(button, device.descriptor)
                    NativeLibrary.onGamePadEvent(device.descriptor, button, ButtonState.PRESSED)
                } else {
                    NativeLibrary.onGamePadEvent(
                        device.descriptor,
                        button,
                        ButtonState.RELEASED
                    )
                }
            }
        }

        stickAccumulator.forEach { (outAxis, value) ->
            NativeLibrary.onGamePadMoveEvent(device.descriptor, outAxis, value.first, value.second)
        }

        return true
    }

    val openAmiiboFileLauncher =
        registerForActivityResult(OpenFileResultContract()) { result: Intent? ->
            if (result == null) return@registerForActivityResult
            val selectedFiles = FileBrowserHelper.getSelectedFiles(
                result, applicationContext, listOf<String>("bin")
            ) ?: return@registerForActivityResult
            if (BuildUtil.isGooglePlayBuild) {
                onAmiiboSelected(selectedFiles[0])
            } else {
                val fileUri = selectedFiles[0].toUri()
                val nativePath = "!" + NativeLibrary.getNativePath(fileUri)
                onAmiiboSelected(nativePath)
            }
        }

    val openImageLauncher =
        registerForActivityResult(ActivityResultContracts.PickVisualMedia()) { result: Uri? ->
            if (result == null) {
                return@registerForActivityResult
            }

            OnFilePickerResult(result.toString())
        }

    companion object {
        private var instance: EmulationActivity? = null

        fun isRunning(): Boolean {
            return instance?.isEmulationRunning ?: false
        }
    }
}
