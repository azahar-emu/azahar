// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

package org.citra.citra_emu.activities

import android.Manifest.permission
import android.annotation.SuppressLint
import android.content.Intent
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
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.navigation.NavOptions
import androidx.navigation.fragment.NavHostFragment
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import kotlin.math.abs
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.NativeLibrary.ButtonState
import org.citra.citra_emu.R
import org.citra.citra_emu.camera.StillImageCameraHelper.onFilePickerResult
import org.citra.citra_emu.contracts.OpenFileResultContract
import org.citra.citra_emu.databinding.ActivityEmulationBinding
import org.citra.citra_emu.dialogs.NetPlayDialog
import org.citra.citra_emu.display.ScreenAdjustmentUtil
import org.citra.citra_emu.display.SecondaryDisplay
import org.citra.citra_emu.features.input.GamepadEventHandler
import org.citra.citra_emu.features.input.GamepadHelper
import org.citra.citra_emu.features.input.InputMappingManager
import org.citra.citra_emu.features.settings.model.BooleanSetting
import org.citra.citra_emu.features.settings.model.CitraSettings
import org.citra.citra_emu.features.settings.model.GraphicsAPI
import org.citra.citra_emu.features.settings.model.IntSetting
import org.citra.citra_emu.features.settings.model.TextureFilter
import org.citra.citra_emu.features.settings.utils.SettingsFile
import org.citra.citra_emu.fragments.EmulationFragment
import org.citra.citra_emu.fragments.MessageDialogFragment
import org.citra.citra_emu.model.Game
import org.citra.citra_emu.ui.main.MainActivity
import org.citra.citra_emu.utils.BuildUtil
import org.citra.citra_emu.utils.CitraDirectoryUtils
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
    var isActivityRecreated = false
    val emulationViewModel: EmulationViewModel by viewModels()
    private lateinit var binding: ActivityEmulationBinding
    private lateinit var screenAdjustmentUtil: ScreenAdjustmentUtil
    private lateinit var gamepadEventHandler: GamepadEventHandler
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
        requestWindowFeature(Window.FEATURE_NO_TITLE)

        RefreshRateUtil.enforceRefreshRate(this, sixtyHz = true)

        ThemeUtil.setTheme(this)

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

        super.onCreate(savedInstanceState)

        isActivityRecreated = savedInstanceState != null
        if (!isActivityRecreated) processGame = null

        processGame = processGame ?: GameHelper.getGameFromIntent(this, intent)
        val game = processGame

        loadGameSettings(game)

        secondaryDisplayManager = SecondaryDisplay(this)
        secondaryDisplayManager.updateDisplay()

        NativeLibrary.initMultiplayer()

        binding = ActivityEmulationBinding.inflate(layoutInflater)
        gamepadEventHandler = GamepadEventHandler(screenAdjustmentUtil, this)
        setContentView(binding.root)

        val navHostFragment =
            supportFragmentManager.findFragmentById(R.id.fragment_container) as NavHostFragment
        val navController = navHostFragment.navController
        navController.setGraph(R.navigation.emulation_navigation, intent.extras)

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
        val launchUri = GameHelper.getLaunchUri(intent)
        if (launchUri != null && launchUri != Uri.EMPTY &&
            launchUri == GameHelper.getLaunchUri(this.intent)
        ) {
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

        processGame = GameHelper.getGameFromIntent(this, intent)
        val game = processGame
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
        isEmulationRunning = true
        instance = this
    }

    private fun loadGameSettings(game: Game?) {
        if (!CitraSettings.defaultsSynced) {
            CitraSettings.syncDefaultsToNative()
            NativeLibrary.reloadSettings()
        }

        // load global settings from file if this is a fresh launch from a launcher
        if (CitraSettings.activeSettings.getAllGlobal().isEmpty()) {
            SettingsFile.loadGlobalSettings(CitraSettings.activeSettings)
        }
        val gameId = if (game == null ||
            game.titleId == 0L
        ) {
            null
        } else {
            String.format("%016X", game.titleId)
        }

        // if activeSettings already matches our current game, skip this
        if (CitraSettings.activeSettings.gameId != gameId) {
            CitraSettings.activeSettings.changeToGlobalContext()
            if (gameId != null) {
                CitraSettings.activeSettings.gameId = gameId
                SettingsFile.loadCustomSettings(
                    CitraSettings.activeSettings,
                    gameId
                )
            }
        }

        if (CitraSettings.activeSettings.get(IntSetting.GRAPHICS_API) == GraphicsAPI.VULKAN.int &&
            CitraSettings.activeSettings.get(IntSetting.TEXTURE_FILTER) == TextureFilter.ANIME4K.int
        ) {
            if (CitraSettings.activeSettings.hasOverride(IntSetting.GRAPHICS_API)) {
                // in this case vulkan is a per-app override, so update the per-app setting
                // which will happen because the settings context is per-app at this point.
                CitraSettings.activeSettings.setBySettingsContext(
                    IntSetting.TEXTURE_FILTER,
                    TextureFilter.NOFILTER.int
                )
            } else {
                // otherwise write global if texture filter is global, override if override
                CitraSettings.activeSettings.setByCurrentState(
                    IntSetting.TEXTURE_FILTER,
                    TextureFilter.NOFILTER.int
                )
            }
            SettingsFile.saveSetting(IntSetting.TEXTURE_FILTER, CitraSettings.activeSettings)
            NativeLibrary.reloadSettings()
            showAnime4kDialog()
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

        if (isFinishing) {
            CitraSettings.activeSettings.changeToGlobalContext()
            processGame = null
            NativeLibrary.stopEmulation()
        }

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
                    if (!gamepadEventHandler.hotkeyIsPressed) {
                        onBackPressed()
                        return true
                    }
                }
                return gamepadEventHandler.handleKeyPress(
                    if (event.keyCode ==
                        0
                    ) {
                        event.scanCode
                    } else {
                        event.keyCode
                    },
                    event.device.descriptor
                )
            }

            KeyEvent.ACTION_UP -> {
                return gamepadEventHandler.handleKeyRelease(
                    if (event.keyCode ==
                        0
                    ) {
                        event.scanCode
                    } else {
                        event.keyCode
                    },
                    event.device.descriptor
                )
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
                    InputMappingManager.getOutAxesForAxis(zeroPair).forEach { (outAxis, outDir) ->
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
                    InputMappingManager.getOutButtonsForAxis(zeroPair).forEach { outButton ->
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
            InputMappingManager.getOutAxesForAxis(axisPair).forEach { (outAxis, outDir) ->
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
            InputMappingManager.getOutButtonsForAxis(axisPair).forEach { button ->
                val mapping = InputMappingManager.getMappingForButton(button)
                if (abs(value) > (mapping?.threshold ?: 0.5f)) {
                    gamepadEventHandler.handleKeyPress(button, device.descriptor)
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
        var processGame: Game? = null
        fun isRunning(): Boolean = instance?.isEmulationRunning ?: false
    }
}
