// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.activities

import android.Manifest.permission
import android.annotation.SuppressLint
import android.content.Intent
import android.content.SharedPreferences
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
import androidx.core.os.BundleCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.navigation.fragment.NavHostFragment
import androidx.preference.PreferenceManager
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.camera.StillImageCameraHelper.OnFilePickerResult
import org.citra.citra_emu.contracts.OpenFileResultContract
import org.citra.citra_emu.databinding.ActivityEmulationBinding
import org.citra.citra_emu.display.ScreenAdjustmentUtil
import org.citra.citra_emu.features.hotkeys.HotkeyUtility
import org.citra.citra_emu.features.settings.model.BooleanSetting
import org.citra.citra_emu.features.settings.model.IntSetting
import org.citra.citra_emu.features.settings.model.SettingsViewModel
import org.citra.citra_emu.features.settings.model.view.InputBindingSetting
import org.citra.citra_emu.fragments.EmulationFragment
import org.citra.citra_emu.fragments.MessageDialogFragment
import org.citra.citra_emu.model.Game
import org.citra.citra_emu.utils.ControllerMappingHelper
import org.citra.citra_emu.utils.FileBrowserHelper
import org.citra.citra_emu.utils.EmulationLifecycleUtil
import org.citra.citra_emu.utils.EmulationMenuSettings
import org.citra.citra_emu.utils.Log
import org.citra.citra_emu.utils.ThemeUtil
import org.citra.citra_emu.viewmodel.EmulationViewModel

class EmulationActivity : AppCompatActivity() {
    private val preferences: SharedPreferences
        get() = PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)
    var isActivityRecreated = false
    private val emulationViewModel: EmulationViewModel by viewModels()
    val settingsViewModel: SettingsViewModel by viewModels()

    private lateinit var binding: ActivityEmulationBinding
    private lateinit var screenAdjustmentUtil: ScreenAdjustmentUtil
    private lateinit var hotkeyUtility: HotkeyUtility

    private val emulationFragment: EmulationFragment
        get() {
            val navHostFragment =
                supportFragmentManager.findFragmentById(R.id.fragment_container) as NavHostFragment
            return navHostFragment.getChildFragmentManager().fragments.last() as EmulationFragment
        }

    private var isEmulationRunning: Boolean = false

    override fun onCreate(savedInstanceState: Bundle?) {
        requestWindowFeature(Window.FEATURE_NO_TITLE)

        ThemeUtil.setTheme(this)

        settingsViewModel.settings.loadSettings()

        super.onCreate(savedInstanceState)

        binding = ActivityEmulationBinding.inflate(layoutInflater)
        screenAdjustmentUtil = ScreenAdjustmentUtil(this, windowManager, settingsViewModel.settings)
        hotkeyUtility = HotkeyUtility(screenAdjustmentUtil, this)
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

        EmulationLifecycleUtil.addShutdownHook(hook = {
            if (intent.getBooleanExtra("launched_from_shortcut", false)) {
                finishAffinity()
            } else {
                this.finish()
            }
        })

        isEmulationRunning = true
        instance = this

        applyOrientationSettings() // Check for orientation settings at startup

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

        NativeLibrary.playTimeManagerStart(game.titleId)
    }

    // On some devices, the system bars will not disappear on first boot or after some
    // rotations. Here we set full screen immersive repeatedly in onResume and in
    // onWindowFocusChanged to prevent the unwanted status bar state.
    override fun onResume() {
        super.onResume()
        enableFullscreenImmersive()
        applyOrientationSettings() // Check for orientation settings changes on runtime
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        enableFullscreenImmersive()
    }

    public override fun onRestart() {
        super.onRestart()
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
        EmulationLifecycleUtil.clear()
        NativeLibrary.playTimeManagerStop()
        isEmulationRunning = false
        instance = null
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

    private fun enableFullscreenImmersive() {
        val attributes = window.attributes

        attributes.layoutInDisplayCutoutMode =
            if (BooleanSetting.EXPAND_TO_CUTOUT_AREA.boolean) {
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

    private fun applyOrientationSettings() {
        val orientationOption = IntSetting.ORIENTATION_OPTION.int
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

        val button =
            preferences.getInt(InputBindingSetting.getInputButtonKey(event.keyCode), event.keyCode)
        val action: Int = when (event.action) {
            KeyEvent.ACTION_DOWN -> {
                hotkeyUtility.handleHotkey(button)

                // On some devices, the back gesture / button press is not intercepted by androidx
                // and fails to open the emulation menu. So we're stuck running deprecated code to
                // cover for either a fault on androidx's side or in OEM skins (MIUI at least)
                if (event.keyCode == KeyEvent.KEYCODE_BACK) {
                    // If the hotkey is pressed, we don't want to open the drawer
                    if (!hotkeyUtility.HotkeyIsPressed) {
                        onBackPressed()
                    }
                }

                // Normal key events.
                NativeLibrary.ButtonState.PRESSED
            }

            KeyEvent.ACTION_UP -> {
                hotkeyUtility.HotkeyIsPressed = false
                NativeLibrary.ButtonState.RELEASED
            }
            else -> return false
        }
        val input = event.device
            ?: // Controller was disconnected
            return false
        return NativeLibrary.onGamePadEvent(input.descriptor, button, action)
    }

    private fun onAmiiboSelected(selectedFile: String) {
        val success = NativeLibrary.loadAmiibo(selectedFile)
        if (!success) {
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
            emulationFragment.isDrawerOpen()) {
            return super.dispatchGenericMotionEvent(event)
        }

        // Don't attempt to do anything if we are disconnecting a device.
        if (event.actionMasked == MotionEvent.ACTION_CANCEL) {
            return true
        }
        val input = event.device
        val motions = input.motionRanges
        val axisValuesCirclePad = floatArrayOf(0.0f, 0.0f)
        val axisValuesCStick = floatArrayOf(0.0f, 0.0f)
        val axisValuesDPad = floatArrayOf(0.0f, 0.0f)
        var isTriggerPressedLMapped = false
        var isTriggerPressedRMapped = false
        var isTriggerPressedZLMapped = false
        var isTriggerPressedZRMapped = false
        var isTriggerPressedL = false
        var isTriggerPressedR = false
        var isTriggerPressedZL = false
        var isTriggerPressedZR = false
        for (range in motions) {
            val axis = range.axis
            val origValue = event.getAxisValue(axis)
            var value = ControllerMappingHelper.scaleAxis(input, axis, origValue)
            val nextMapping =
                preferences.getInt(InputBindingSetting.getInputAxisButtonKey(axis), -1)
            val guestOrientation =
                preferences.getInt(InputBindingSetting.getInputAxisOrientationKey(axis), -1)
            if (nextMapping == -1 || guestOrientation == -1) {
                // Axis is unmapped
                continue
            }
            if (value > 0f && value < 0.1f || value < 0f && value > -0.1f) {
                // Skip joystick wobble
                value = 0f
            }
            when (nextMapping) {
                NativeLibrary.ButtonType.STICK_LEFT -> {
                    axisValuesCirclePad[guestOrientation] = value
                }

                NativeLibrary.ButtonType.STICK_C -> {
                    axisValuesCStick[guestOrientation] = value
                }

                NativeLibrary.ButtonType.DPAD -> {
                    axisValuesDPad[guestOrientation] = value
                }

                NativeLibrary.ButtonType.TRIGGER_L -> {
                    isTriggerPressedLMapped = true
                    isTriggerPressedL = value != 0f
                }

                NativeLibrary.ButtonType.TRIGGER_R -> {
                    isTriggerPressedRMapped = true
                    isTriggerPressedR = value != 0f
                }

                NativeLibrary.ButtonType.BUTTON_ZL -> {
                    isTriggerPressedZLMapped = true
                    isTriggerPressedZL = value != 0f
                }

                NativeLibrary.ButtonType.BUTTON_ZR -> {
                    isTriggerPressedZRMapped = true
                    isTriggerPressedZR = value != 0f
                }
            }
        }

        // Circle-Pad and C-Stick status
        NativeLibrary.onGamePadMoveEvent(
            input.descriptor,
            NativeLibrary.ButtonType.STICK_LEFT,
            axisValuesCirclePad[0],
            axisValuesCirclePad[1]
        )
        NativeLibrary.onGamePadMoveEvent(
            input.descriptor,
            NativeLibrary.ButtonType.STICK_C,
            axisValuesCStick[0],
            axisValuesCStick[1]
        )

        // Triggers L/R and ZL/ZR
        if (isTriggerPressedLMapped) {
            NativeLibrary.onGamePadEvent(
                NativeLibrary.TouchScreenDevice,
                NativeLibrary.ButtonType.TRIGGER_L,
                if (isTriggerPressedL) {
                    NativeLibrary.ButtonState.PRESSED
                } else {
                    NativeLibrary.ButtonState.RELEASED
                }
            )
        }
        if (isTriggerPressedRMapped) {
            NativeLibrary.onGamePadEvent(
                NativeLibrary.TouchScreenDevice,
                NativeLibrary.ButtonType.TRIGGER_R,
                if (isTriggerPressedR) {
                    NativeLibrary.ButtonState.PRESSED
                } else {
                    NativeLibrary.ButtonState.RELEASED
                }
            )
        }
        if (isTriggerPressedZLMapped) {
            NativeLibrary.onGamePadEvent(
                NativeLibrary.TouchScreenDevice,
                NativeLibrary.ButtonType.BUTTON_ZL,
                if (isTriggerPressedZL) {
                    NativeLibrary.ButtonState.PRESSED
                } else {
                    NativeLibrary.ButtonState.RELEASED
                }
            )
        }
        if (isTriggerPressedZRMapped) {
            NativeLibrary.onGamePadEvent(
                NativeLibrary.TouchScreenDevice,
                NativeLibrary.ButtonType.BUTTON_ZR,
                if (isTriggerPressedZR) {
                    NativeLibrary.ButtonState.PRESSED
                } else {
                    NativeLibrary.ButtonState.RELEASED
                }
            )
        }

        // Work-around to allow D-pad axis to be bound to emulated buttons
        if (axisValuesDPad[0] == 0f) {
            NativeLibrary.onGamePadEvent(
                NativeLibrary.TouchScreenDevice,
                NativeLibrary.ButtonType.DPAD_LEFT,
                NativeLibrary.ButtonState.RELEASED
            )
            NativeLibrary.onGamePadEvent(
                NativeLibrary.TouchScreenDevice,
                NativeLibrary.ButtonType.DPAD_RIGHT,
                NativeLibrary.ButtonState.RELEASED
            )
        }
        if (axisValuesDPad[0] < 0f) {
            NativeLibrary.onGamePadEvent(
                NativeLibrary.TouchScreenDevice,
                NativeLibrary.ButtonType.DPAD_LEFT,
                NativeLibrary.ButtonState.PRESSED
            )
            NativeLibrary.onGamePadEvent(
                NativeLibrary.TouchScreenDevice,
                NativeLibrary.ButtonType.DPAD_RIGHT,
                NativeLibrary.ButtonState.RELEASED
            )
        }
        if (axisValuesDPad[0] > 0f) {
            NativeLibrary.onGamePadEvent(
                NativeLibrary.TouchScreenDevice,
                NativeLibrary.ButtonType.DPAD_LEFT,
                NativeLibrary.ButtonState.RELEASED
            )
            NativeLibrary.onGamePadEvent(
                NativeLibrary.TouchScreenDevice,
                NativeLibrary.ButtonType.DPAD_RIGHT,
                NativeLibrary.ButtonState.PRESSED
            )
        }
        if (axisValuesDPad[1] == 0f) {
            NativeLibrary.onGamePadEvent(
                NativeLibrary.TouchScreenDevice,
                NativeLibrary.ButtonType.DPAD_UP,
                NativeLibrary.ButtonState.RELEASED
            )
            NativeLibrary.onGamePadEvent(
                NativeLibrary.TouchScreenDevice,
                NativeLibrary.ButtonType.DPAD_DOWN,
                NativeLibrary.ButtonState.RELEASED
            )
        }
        if (axisValuesDPad[1] < 0f) {
            NativeLibrary.onGamePadEvent(
                NativeLibrary.TouchScreenDevice,
                NativeLibrary.ButtonType.DPAD_UP,
                NativeLibrary.ButtonState.PRESSED
            )
            NativeLibrary.onGamePadEvent(
                NativeLibrary.TouchScreenDevice,
                NativeLibrary.ButtonType.DPAD_DOWN,
                NativeLibrary.ButtonState.RELEASED
            )
        }
        if (axisValuesDPad[1] > 0f) {
            NativeLibrary.onGamePadEvent(
                NativeLibrary.TouchScreenDevice,
                NativeLibrary.ButtonType.DPAD_UP,
                NativeLibrary.ButtonState.RELEASED
            )
            NativeLibrary.onGamePadEvent(
                NativeLibrary.TouchScreenDevice,
                NativeLibrary.ButtonType.DPAD_DOWN,
                NativeLibrary.ButtonState.PRESSED
            )
        }
        return true
    }

    val openFileLauncher =
        registerForActivityResult(OpenFileResultContract()) { result: Intent? ->
            if (result == null) return@registerForActivityResult
            val selectedFiles = FileBrowserHelper.getSelectedFiles(
                result, applicationContext, listOf<String>("bin")
            ) ?: return@registerForActivityResult
            onAmiiboSelected(selectedFiles[0])
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
