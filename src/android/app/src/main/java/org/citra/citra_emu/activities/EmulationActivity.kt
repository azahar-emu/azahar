// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

package org.citra.citra_emu.activities

import android.Manifest.permission
import android.annotation.SuppressLint
import android.content.Intent
import android.content.SharedPreferences
import android.content.pm.ActivityInfo
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
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
import androidx.navigation.NavOptions
import androidx.navigation.fragment.NavHostFragment
import androidx.preference.PreferenceManager
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.camera.StillImageCameraHelper.onFilePickerResult
import org.citra.citra_emu.contracts.OpenFileResultContract
import org.citra.citra_emu.databinding.ActivityEmulationBinding
import org.citra.citra_emu.dialogs.NetPlayDialog
import org.citra.citra_emu.display.ScreenAdjustmentUtil
import org.citra.citra_emu.display.SecondaryDisplay
import org.citra.citra_emu.features.hotkeys.HotkeyUtility
import org.citra.citra_emu.features.settings.model.BooleanSetting
import org.citra.citra_emu.features.settings.model.CitraSettings
import org.citra.citra_emu.features.settings.model.GraphicsAPI
import org.citra.citra_emu.features.settings.model.IntSetting
import org.citra.citra_emu.features.settings.model.TextureFilter
import org.citra.citra_emu.features.settings.model.view.InputBindingSetting
import org.citra.citra_emu.features.settings.utils.SettingsFile
import org.citra.citra_emu.fragments.EmulationFragment
import org.citra.citra_emu.fragments.MessageDialogFragment
import org.citra.citra_emu.model.Game
import org.citra.citra_emu.ui.main.MainActivity
import org.citra.citra_emu.utils.BuildUtil
import org.citra.citra_emu.utils.CitraDirectoryUtils
import org.citra.citra_emu.utils.ControllerMappingHelper
import org.citra.citra_emu.utils.DirectoryInitialization
import org.citra.citra_emu.utils.EmulationLifecycleUtil
import org.citra.citra_emu.utils.EmulationMenuSettings
import org.citra.citra_emu.utils.FileBrowserHelper
import org.citra.citra_emu.utils.GameHelper
import org.citra.citra_emu.utils.Log
import org.citra.citra_emu.utils.NetPlayManager
import org.citra.citra_emu.utils.PermissionsHandler
import org.citra.citra_emu.utils.RefreshRateUtil
import org.citra.citra_emu.utils.ThemeUtil
import org.citra.citra_emu.viewmodel.EmulationViewModel

class EmulationActivity : AppCompatActivity() {
    private val preferences: SharedPreferences
        get() = PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)
    var isActivityRecreated = false
    val emulationViewModel: EmulationViewModel by viewModels()
    private lateinit var binding: ActivityEmulationBinding
    private lateinit var screenAdjustmentUtil: ScreenAdjustmentUtil
    private lateinit var hotkeyUtility: HotkeyUtility
    lateinit var secondaryDisplayManager: SecondaryDisplay

    private val onShutdown = Runnable {
        if (intent.getBooleanExtra("launchedFromShortcut", false)) {
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

    private var isRotationBlocked: Boolean = true
    private var isEmulationRunning: Boolean = false
    private var isEmulationReady: Boolean = false

    private fun ensureUserDirectoryReady(): Boolean {
        if (DirectoryInitialization.areCitraDirectoriesReady()) return true
        CitraDirectoryUtils.attemptAutomaticUpdateDirectory() // Lime3DS -> Azahar
        if (CitraDirectoryUtils.needToUpdateManually()) return false
        if (!PermissionsHandler.hasWriteAccess(applicationContext)) return false
        return DirectoryInitialization.start() ==
            DirectoryInitialization.DirectoryInitializationState.CITRA_DIRECTORIES_INITIALIZED
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        Log.info("[EmulationActivity] intent=$intent")
        Log.info(
            "[EmulationActivity] extras=${try {
                intent.extras?.keySet()?.joinToString()
            } catch (
                e: Exception
            ) {
                "unreadable: ${e.message}"
            }}"
        )

        requestWindowFeature(Window.FEATURE_NO_TITLE)

        RefreshRateUtil.enforceRefreshRate(this, sixtyHz = true)

        ThemeUtil.setTheme(this)
        val game = getGameFromIntent(intent)

        if (!ensureUserDirectoryReady()) {
            super.onCreate(null) // null: don't restore fragments into an activity we're killing
            secondaryDisplayManager = SecondaryDisplay(this)
            startActivity(
                Intent(this, MainActivity::class.java).apply {
                    addFlags(Intent.FLAG_ACTIVITY_CLEAR_TASK or Intent.FLAG_ACTIVITY_NEW_TASK)
                }
            )
            finish()
            return
        }

        screenAdjustmentUtil = ScreenAdjustmentUtil(this, windowManager)

        // Block orientation until emulation is ready to prevent unneccesary
        // surface recreation until the renderer is ready.
        isRotationBlocked = true
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LOCKED

        // load per-game settings.
        // TODO: come up with a system for homebrew which all have titleId = 0
        if (game != null && game.titleId != 0L) loadGameSettings(game)

        super.onCreate(savedInstanceState)

        secondaryDisplayManager = SecondaryDisplay(this)
        secondaryDisplayManager.updateDisplay()

        NativeLibrary.initMultiplayer()

        binding = ActivityEmulationBinding.inflate(layoutInflater)
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
        val displayRotation = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            display.rotation
        } else {
            @Suppress("DEPRECATION")
            windowManager.defaultDisplay.rotation
        }
        NativeLibrary.swapScreens(
            EmulationMenuSettings.swapScreens,
            displayRotation
        )

        EmulationLifecycleUtil.addShutdownHook(onShutdown)

        isEmulationRunning = true
        instance = this

        applyOrientationSettings() // Check for orientation settings at startup

        // TODO: all 3dsx homebrew will have titleId = 0, so make this use something different
        if (game != null && game.titleId != 0L) NativeLibrary.playTimeManagerStart(game.titleId)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        val launchUri = getLaunchUri(intent)
        if (launchUri != null && launchUri != Uri.EMPTY && launchUri == getLaunchUri(this.intent)) {
            Log.info("[EmulationActivity] Ignoring relaunch of the running game")
            return
        }
        setIntent(intent)

        NativeLibrary.stopEmulation()
        NativeLibrary.playTimeManagerStop()

        isEmulationReady = false
        isRotationBlocked = true
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LOCKED
        emulationViewModel.setEmulationStarted(false)

        val game = getGameFromIntent(intent)
        loadGameSettings(game)

        if (game != null && game.titleId != 0L) {
            NativeLibrary.playTimeManagerStart(game.titleId)
        }

        enableFullscreenImmersive()

        val navHostFragment =
            supportFragmentManager.findFragmentById(R.id.fragment_container) as NavHostFragment
        navHostFragment.navController.navigate(
            R.id.emulationFragment,
            intent.extras,
            NavOptions.Builder().setPopUpTo(R.id.emulationFragment, true).build()
        )
    }

    private fun loadGameSettings(game: Game?) {
        // don't let the previous game's overrides leak in
        CitraSettings.activeSettings.changeToGlobalContext()
        if (CitraSettings.activeSettings.getAllGlobal().isEmpty()) {
            SettingsFile.loadSettings(CitraSettings.activeSettings)
        }
        if (game != null && game.titleId != 0L) {
            SettingsFile.loadSettings(
                CitraSettings.activeSettings,
                String.format("%016X", game.titleId)
            )
        }

        if (CitraSettings.activeSettings.get(IntSetting.GRAPHICS_API) == GraphicsAPI.VULKAN.int &&
            CitraSettings.activeSettings.get(IntSetting.TEXTURE_FILTER) == TextureFilter.ANIME4K.int
        ) {
            CitraSettings.activeSettings.setByCurrentState(
                IntSetting.TEXTURE_FILTER,
                TextureFilter.NOFILTER.int
            )
            SettingsFile.saveSetting(IntSetting.TEXTURE_FILTER, CitraSettings.activeSettings)
            showAnime4kDialog()
        }
    }

    private fun getLaunchUri(intent: Intent): Uri? = try {
        val game = intent.extras?.let { extras ->
            BundleCompat.getParcelable(extras, "game", Game::class.java)
        }
        if (game != null) {
            game.launchIntent.data
        } else {
            intent.data ?: intent.getStringExtra("SelectedGame")?.toUri()
        }
    } catch (e: Exception) {
        Log.error("[EmulationActivity] Failed to parse URI from intent: ${e.message}")
        null
    }

    /** creates or loads a Game object from an intent */
    private fun getGameFromIntent(intent: Intent): Game? {
        val parcelable = try {
            intent.extras?.let { BundleCompat.getParcelable(it, "game", Game::class.java) }
        } catch (e: Exception) {
            Log.error("[EmulationActivity] Failed to retrieve game data: ${e.message}")
            null
        }

        if (parcelable != null) return parcelable

        val uri: Uri = intent.data
            ?: intent.getStringExtra("SelectedGame")?.toUri()
            ?: return null

        try {
            if (BuildUtil.isGooglePlayBuild || uri.toString().startsWith("!")) {
                return GameHelper.getGame(
                    uri,
                    isInstalled = false,
                    addedToLibrary = false,
                    mediaType = Game.MediaType.GAME_CARD
                )
            } else {
                val fd = contentResolver.openFileDescriptor(uri, "r")
                if (fd != null) {
                    return GameHelper.getGame(
                        "fd://${fd.fd}".toUri(),
                        isInstalled = false,
                        addedToLibrary = false,
                        mediaType = Game.MediaType.GAME_CARD
                    )
                } else {
                    return null
                }
            }
        } catch (e: Exception) {
            Log.error("[EmulationActivity] Failed to resolve game from intent: ${e.message}")
            return null
        }
    }

    // On some devices, the system bars will not disappear on first boot or after some
    // rotations. Here we set full screen immersive repeatedly in onResume and in
    // onWindowFocusChanged to prevent the unwanted status bar state.
    override fun onResume() {
        enableFullscreenImmersive()
        if (isEmulationReady) {
            // If emulation is ready then unblock rotation
            isRotationBlocked = false
            applyOrientationSettings()
            emulationViewModel.setEmulationStarted(true)
        } else {
            if (!isRotationBlocked) {
                applyOrientationSettings()
            }
        }
        super.onResume()
    }

    override fun onStop() {
        secondaryDisplayManager.releasePresentation()
        super.onStop()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        enableFullscreenImmersive()
        super.onWindowFocusChanged(hasFocus)
    }

    public override fun onRestart() {
        super.onRestart()
        secondaryDisplayManager.updateDisplay()
        NativeLibrary.reloadCameraDevices()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putBoolean("isEmulationRunning", isEmulationRunning)
        outState.putBoolean("isEmulationReady", isEmulationReady)
        outState.putBoolean("isRotationBlocked", isRotationBlocked)
    }

    override fun onRestoreInstanceState(savedInstanceState: Bundle) {
        super.onRestoreInstanceState(savedInstanceState)
        isEmulationRunning = savedInstanceState.getBoolean("isEmulationRunning", false)
        isEmulationReady = savedInstanceState.getBoolean("isEmulationReady", false)
        isRotationBlocked = savedInstanceState.getBoolean("isRotationBlocked", isRotationBlocked)
    }

    override fun onDestroy() {
        EmulationLifecycleUtil.removeHook(onShutdown)
        NativeLibrary.playTimeManagerStop()
        isEmulationRunning = false
        instance = null
        secondaryDisplayManager.releasePresentation()
        secondaryDisplayManager.releaseVD()

        CitraSettings.activeSettings.changeToGlobalContext()

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
        isEmulationReady = true
        if (isRotationBlocked) {
            isRotationBlocked = false
            applyOrientationSettings()
        }
        Toast.makeText(
            applicationContext,
            getString(R.string.emulation_menu_help),
            Toast.LENGTH_LONG
        ).show()
    }

    fun displayMultiplayerDialog() {
        val dialog = NetPlayDialog(this)
        dialog.show()
    }

    fun addNetPlayMessages(type: Int, msg: String) {
        NetPlayManager.addNetPlayMessage(type, msg)
    }

    private fun enableFullscreenImmersive() {
        val attributes = window.attributes

        attributes.layoutInDisplayCutoutMode =
            if (CitraSettings.activeSettings.get(BooleanSetting.EXPAND_TO_CUTOUT_AREA)) {
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
        requestedOrientation = CitraSettings.activeSettings.get(IntSetting.ORIENTATION_OPTION)
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
                return hotkeyUtility.handleKeyPress(event)
            }

            KeyEvent.ACTION_UP -> {
                return hotkeyUtility.handleKeyRelease(event)
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
            val inverted = preferences.getBoolean(
                InputBindingSetting.getInputAxisInvertedKey(axis),
                false
            )
            if (nextMapping == -1 || guestOrientation == -1) {
                // Axis is unmapped
                continue
            }
            if ((value > 0f && value < 0.1f) || (value < 0f && value > -0.1f)) {
                // Skip joystick wobble
                value = 0f
            }
            if (inverted) value = -value

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
                NativeLibrary.TOUCHSCREEN_DEVICE,
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
                NativeLibrary.TOUCHSCREEN_DEVICE,
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
                NativeLibrary.TOUCHSCREEN_DEVICE,
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
                NativeLibrary.TOUCHSCREEN_DEVICE,
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
                NativeLibrary.TOUCHSCREEN_DEVICE,
                NativeLibrary.ButtonType.DPAD_LEFT,
                NativeLibrary.ButtonState.RELEASED
            )
            NativeLibrary.onGamePadEvent(
                NativeLibrary.TOUCHSCREEN_DEVICE,
                NativeLibrary.ButtonType.DPAD_RIGHT,
                NativeLibrary.ButtonState.RELEASED
            )
        }
        if (axisValuesDPad[0] < 0f) {
            NativeLibrary.onGamePadEvent(
                NativeLibrary.TOUCHSCREEN_DEVICE,
                NativeLibrary.ButtonType.DPAD_LEFT,
                NativeLibrary.ButtonState.PRESSED
            )
            NativeLibrary.onGamePadEvent(
                NativeLibrary.TOUCHSCREEN_DEVICE,
                NativeLibrary.ButtonType.DPAD_RIGHT,
                NativeLibrary.ButtonState.RELEASED
            )
        }
        if (axisValuesDPad[0] > 0f) {
            NativeLibrary.onGamePadEvent(
                NativeLibrary.TOUCHSCREEN_DEVICE,
                NativeLibrary.ButtonType.DPAD_LEFT,
                NativeLibrary.ButtonState.RELEASED
            )
            NativeLibrary.onGamePadEvent(
                NativeLibrary.TOUCHSCREEN_DEVICE,
                NativeLibrary.ButtonType.DPAD_RIGHT,
                NativeLibrary.ButtonState.PRESSED
            )
        }
        if (axisValuesDPad[1] == 0f) {
            NativeLibrary.onGamePadEvent(
                NativeLibrary.TOUCHSCREEN_DEVICE,
                NativeLibrary.ButtonType.DPAD_UP,
                NativeLibrary.ButtonState.RELEASED
            )
            NativeLibrary.onGamePadEvent(
                NativeLibrary.TOUCHSCREEN_DEVICE,
                NativeLibrary.ButtonType.DPAD_DOWN,
                NativeLibrary.ButtonState.RELEASED
            )
        }
        if (axisValuesDPad[1] < 0f) {
            NativeLibrary.onGamePadEvent(
                NativeLibrary.TOUCHSCREEN_DEVICE,
                NativeLibrary.ButtonType.DPAD_UP,
                NativeLibrary.ButtonState.PRESSED
            )
            NativeLibrary.onGamePadEvent(
                NativeLibrary.TOUCHSCREEN_DEVICE,
                NativeLibrary.ButtonType.DPAD_DOWN,
                NativeLibrary.ButtonState.RELEASED
            )
        }
        if (axisValuesDPad[1] > 0f) {
            NativeLibrary.onGamePadEvent(
                NativeLibrary.TOUCHSCREEN_DEVICE,
                NativeLibrary.ButtonType.DPAD_UP,
                NativeLibrary.ButtonState.RELEASED
            )
            NativeLibrary.onGamePadEvent(
                NativeLibrary.TOUCHSCREEN_DEVICE,
                NativeLibrary.ButtonType.DPAD_DOWN,
                NativeLibrary.ButtonState.PRESSED
            )
        }
        return true
    }

    fun showAnime4kDialog() {
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.incompatible_texture_filter)
            .setMessage(R.string.incompatible_texture_filter_description)
            .setPositiveButton(android.R.string.ok, null)
            .setCancelable(false)
            .show()
    }

    val openAmiiboFileLauncher =
        registerForActivityResult(OpenFileResultContract()) { result: Intent? ->
            if (result == null) return@registerForActivityResult
            val selectedFiles = FileBrowserHelper.getSelectedFiles(
                result,
                applicationContext,
                listOf<String>("bin")
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

            onFilePickerResult(result.toString())
        }

    companion object {
        private var instance: EmulationActivity? = null

        fun isRunning(): Boolean = instance?.isEmulationRunning ?: false
    }
}
