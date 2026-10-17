// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

package org.citra.citra_emu.features.settings.ui

import android.content.Context
import android.content.SharedPreferences
import android.content.res.Resources
import android.hardware.camera2.CameraAccessException
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.os.Build
import android.text.TextUtils
import androidx.preference.PreferenceManager
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import kotlin.math.roundToInt
import org.citra.citra_emu.BuildConfig
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.R
import org.citra.citra_emu.display.ScreenLayout
import org.citra.citra_emu.display.StereoMode
import org.citra.citra_emu.display.StereoWhichDisplay
import org.citra.citra_emu.features.settings.model.AbstractSetting
import org.citra.citra_emu.features.settings.model.BooleanSetting
import org.citra.citra_emu.features.settings.model.CitraSettings
import org.citra.citra_emu.features.settings.model.FloatSetting
import org.citra.citra_emu.features.settings.model.IntListSetting
import org.citra.citra_emu.features.settings.model.IntSetting
import org.citra.citra_emu.features.settings.model.StringSetting
import org.citra.citra_emu.features.settings.model.view.DateTimeSetting
import org.citra.citra_emu.features.settings.model.view.HeaderSetting
import org.citra.citra_emu.features.settings.model.view.InputBindingSetting
import org.citra.citra_emu.features.settings.model.view.MultiChoiceSetting
import org.citra.citra_emu.features.settings.model.view.RunnableSetting
import org.citra.citra_emu.features.settings.model.view.SettingsItem
import org.citra.citra_emu.features.settings.model.view.SingleChoiceSetting
import org.citra.citra_emu.features.settings.model.view.SliderSetting
import org.citra.citra_emu.features.settings.model.view.StringInputSetting
import org.citra.citra_emu.features.settings.model.view.StringSingleChoiceSetting
import org.citra.citra_emu.features.settings.model.view.SubmenuSetting
import org.citra.citra_emu.features.settings.model.view.SwitchSetting
import org.citra.citra_emu.features.settings.utils.SettingsFile
import org.citra.citra_emu.fragments.ResetSettingsDialogFragment
import org.citra.citra_emu.utils.BirthdayMonth
import org.citra.citra_emu.utils.BuildUtil
import org.citra.citra_emu.utils.GraphicsUtil
import org.citra.citra_emu.utils.Log
import org.citra.citra_emu.utils.SystemSaveGame
import org.citra.citra_emu.utils.ThemeUtil

class SettingsFragmentPresenter(
    private val fragmentView: SettingsFragmentView,
    private val editingSettings: CitraSettings
) {
    private var menuTag: String? = null
    private lateinit var gameId: String
    private var settingsList: ArrayList<SettingsItem>? = null

    private val settingsActivity get() = fragmentView.activityView as SettingsActivity

    private lateinit var settingsAdapter: SettingsAdapter

    private lateinit var preferences: SharedPreferences

    fun onCreate(menuTag: String, gameId: String) {
        this.gameId = gameId
        this.menuTag = menuTag
    }

    fun onViewCreated(settingsAdapter: SettingsAdapter) {
        this.settingsAdapter = settingsAdapter
        preferences = PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)
        loadSettingsList()
    }

    fun loadSettingsList() {
        if (!TextUtils.isEmpty(gameId)) {
            settingsActivity.setToolbarTitle("Application Settings: $gameId")
        }
        val sl = ArrayList<SettingsItem>()
        if (menuTag == null) {
            return
        }
        when (menuTag) {
            SettingsFile.FILE_NAME_CONFIG -> addConfigSettings(sl)

            CitraSettings.SECTION_CORE -> addGeneralSettings(sl)

            CitraSettings.SECTION_SYSTEM -> addSystemSettings(sl)

            CitraSettings.SECTION_CAMERA -> addCameraSettings(sl)

            CitraSettings.SECTION_CONTROLS -> addControlsSettings(sl)

            CitraSettings.SECTION_RENDERER -> addGraphicsSettings(sl)

            CitraSettings.SECTION_LAYOUT -> addLayoutSettings(sl)

            CitraSettings.SECTION_NETWORK -> addNetworkSettings(sl)

            CitraSettings.SECTION_AUDIO -> addAudioSettings(sl)

            CitraSettings.SECTION_DEBUG -> addDebugSettings(sl)

            CitraSettings.SECTION_THEME -> addThemeSettings(sl)

            CitraSettings.SECTION_CUSTOM_LANDSCAPE -> addCustomLandscapeSettings(sl)

            CitraSettings.SECTION_CUSTOM_PORTRAIT -> addCustomPortraitSettings(sl)

            CitraSettings.SECTION_PERFORMANCE_OVERLAY -> addPerformanceOverlaySettings(sl)

            else -> {
                fragmentView.showToastMessage("Unimplemented menu", false)
                return
            }
        }
        settingsList = sl
        fragmentView.showSettingsList(settingsList!!)
    }

    /** Returns the portrait mode width */
    private fun getDimensions(): IntArray {
        val dm = Resources.getSystem().displayMetrics
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            val wm = settingsActivity.windowManager.maximumWindowMetrics
            val height = wm.bounds.height().coerceAtLeast(dm.heightPixels)
            val width = wm.bounds.width().coerceAtLeast(dm.widthPixels)
            intArrayOf(width, height)
        } else {
            intArrayOf(dm.widthPixels, dm.heightPixels)
        }
    }

    private fun getSmallerDimension(): Int = getDimensions().min()

    private fun getLargerDimension(): Int = getDimensions().max()

    private fun addConfigSettings(sl: ArrayList<SettingsItem>) {
        settingsActivity.setToolbarTitle(settingsActivity.getString(R.string.preferences_settings))
        sl.apply {
            add(
                SubmenuSetting(
                    R.string.preferences_general,
                    0,
                    R.drawable.ic_general_settings,
                    CitraSettings.SECTION_CORE
                )
            )
            add(
                SubmenuSetting(
                    R.string.preferences_system,
                    0,
                    R.drawable.ic_system_settings,
                    CitraSettings.SECTION_SYSTEM
                )
            )
            add(
                SubmenuSetting(
                    R.string.preferences_camera,
                    0,
                    R.drawable.ic_camera_settings,
                    CitraSettings.SECTION_CAMERA
                )
            )
            add(
                SubmenuSetting(
                    R.string.preferences_controls,
                    0,
                    R.drawable.ic_controls_settings,
                    CitraSettings.SECTION_CONTROLS
                )
            )
            add(
                SubmenuSetting(
                    R.string.preferences_graphics,
                    0,
                    R.drawable.ic_graphics,
                    CitraSettings.SECTION_RENDERER
                )
            )
            add(
                SubmenuSetting(
                    R.string.preferences_layout,
                    0,
                    R.drawable.ic_fit_screen,
                    CitraSettings.SECTION_LAYOUT
                )
            )
            add(
                SubmenuSetting(
                    R.string.preferences_network,
                    0,
                    R.drawable.ic_network,
                    CitraSettings.SECTION_NETWORK
                )
            )
            add(
                SubmenuSetting(
                    R.string.preferences_audio,
                    0,
                    R.drawable.ic_audio,
                    CitraSettings.SECTION_AUDIO
                )
            )
            add(
                SubmenuSetting(
                    R.string.preferences_debug,
                    0,
                    R.drawable.ic_code,
                    CitraSettings.SECTION_DEBUG
                )
            )

            add(
                RunnableSetting(
                    R.string.reset_to_default,
                    0,
                    false,
                    R.drawable.ic_restore,
                    {
                        ResetSettingsDialogFragment().show(
                            settingsActivity.supportFragmentManager,
                            ResetSettingsDialogFragment.TAG
                        )
                    }
                )
            )
        }
    }

    private fun addGeneralSettings(sl: ArrayList<SettingsItem>) {
        settingsActivity.setToolbarTitle(settingsActivity.getString(R.string.preferences_general))
        sl.apply {
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.USE_FRAME_LIMIT,
                    R.string.frame_limit_enable,
                    R.string.frame_limit_enable_description,
                    BooleanSetting.USE_FRAME_LIMIT.key,
                    BooleanSetting.USE_FRAME_LIMIT.defaultValue
                )
            )
            add(
                SliderSetting(
                    editingSettings,
                    IntSetting.FRAME_LIMIT,
                    R.string.frame_limit_slider,
                    R.string.frame_limit_slider_description,
                    1,
                    200,
                    "%",
                    IntSetting.FRAME_LIMIT.key,
                    IntSetting.FRAME_LIMIT.defaultValue.toFloat()
                )
            )
            add(
                SliderSetting(
                    editingSettings,
                    IntSetting.TURBO_LIMIT,
                    R.string.turbo_limit,
                    R.string.turbo_limit_description,
                    100,
                    400,
                    "%",
                    IntSetting.TURBO_LIMIT.key,
                    IntSetting.TURBO_LIMIT.defaultValue.toFloat()
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.CHECK_FOR_UPDATES,
                    R.string.check_for_updates,
                    R.string.check_for_updates_description,
                    BooleanSetting.CHECK_FOR_UPDATES.key,
                    BooleanSetting.CHECK_FOR_UPDATES.defaultValue,
                    isEnabled = !BuildConfig.DEBUG
                )
            )
            if (!BuildUtil.isGooglePlayBuild) {
                add(
                    SingleChoiceSetting(
                        editingSettings,
                        IntSetting.UPDATE_CHECK_CHANNEL,
                        R.string.update_check_channel,
                        R.string.update_check_channel_description,
                        R.array.updateCheckChannels,
                        R.array.updateCheckChannelsValues,
                        IntSetting.UPDATE_CHECK_CHANNEL.key,
                        IntSetting.UPDATE_CHECK_CHANNEL.defaultValue,
                        isEnabled = (
                            !BuildConfig.DEBUG &&
                                editingSettings.get(BooleanSetting.CHECK_FOR_UPDATES)
                            )
                    )
                )
                add(
                    SwitchSetting(
                        editingSettings,
                        BooleanSetting.ANDROID_HIDE_IMAGES,
                        R.string.android_hide_images,
                        R.string.android_hide_images_description,
                        BooleanSetting.ANDROID_HIDE_IMAGES.key,
                        BooleanSetting.ANDROID_HIDE_IMAGES.defaultValue
                    )
                )
            }
        }
    }

    private var countryCompatibilityChanged = true

    private fun checkCountryCompatibility() {
        if (countryCompatibilityChanged) {
            countryCompatibilityChanged = false
            val compatFlags = SystemSaveGame.getCountryCompatibility(
                editingSettings.get(IntSetting.EMULATED_REGION)
            )
            if (compatFlags != 0) {
                var message = ""
                if (compatFlags and 1 != 0) {
                    message += settingsAdapter.context.getString(R.string.region_mismatch_emulated)
                }
                if (compatFlags and 2 != 0) {
                    if (message.isNotEmpty()) message += "\n\n"
                    message += settingsAdapter.context.getString(R.string.region_mismatch_console)
                }
                MaterialAlertDialogBuilder(settingsAdapter.context)
                    .setTitle(R.string.region_mismatch)
                    .setMessage(message)
                    .setPositiveButton(android.R.string.ok, null)
                    .show()
            }
        }
    }

    @OptIn(ExperimentalStdlibApi::class)
    private fun addSystemSettings(sl: ArrayList<SettingsItem>) {
        settingsActivity.setToolbarTitle(settingsActivity.getString(R.string.preferences_system))
        sl.apply {
            add(HeaderSetting(R.string.emulation_settings))
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.NEW_3DS,
                    R.string.new_3ds,
                    0,
                    BooleanSetting.NEW_3DS.key,
                    BooleanSetting.NEW_3DS.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.LLE_APPLETS,
                    R.string.lle_applets,
                    0,
                    BooleanSetting.LLE_APPLETS.key,
                    BooleanSetting.LLE_APPLETS.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.REQUIRED_ONLINE_LLE_MODULES,
                    R.string.enable_required_online_lle_modules,
                    R.string.enable_required_online_lle_modules_desc,
                    BooleanSetting.REQUIRED_ONLINE_LLE_MODULES.key,
                    BooleanSetting.REQUIRED_ONLINE_LLE_MODULES.defaultValue
                )
            )
            add(HeaderSetting(R.string.profile_settings))
            add(
                SingleChoiceSetting(
                    editingSettings,
                    null,
                    R.string.emulated_region,
                    0,
                    R.array.regionNames,
                    R.array.regionValues,
                    getValue = {
                        val ret = editingSettings.get(IntSetting.EMULATED_REGION)
                        checkCountryCompatibility()
                        ret
                    },
                    setValue = {
                        editingSettings.setByScope(IntSetting.EMULATED_REGION, it)
                        countryCompatibilityChanged = true
                        checkCountryCompatibility()
                    }
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.APPLY_REGION_FREE_PATCH,
                    R.string.apply_region_free_patch,
                    R.string.apply_region_free_patch_desc,
                    BooleanSetting.APPLY_REGION_FREE_PATCH.key,
                    BooleanSetting.APPLY_REGION_FREE_PATCH.defaultValue
                )
            )
            var index = -1
            val countries = settingsActivity.resources.getStringArray(R.array.countries)
                .mapNotNull {
                    index++
                    if (it.isNotEmpty()) it to index.toString() else null
                }
            add(
                StringSingleChoiceSetting(
                    editingSettings,
                    null,
                    R.string.country,
                    0,
                    countries.map { it.first }.toTypedArray(),
                    countries.map { it.second }.toTypedArray(),
                    getValue = {
                        val ret = SystemSaveGame.getCountryCode()
                        checkCountryCompatibility()
                        ret.toString()
                    },
                    setValue = {
                        SystemSaveGame.setCountryCode(it.toShort())
                        countryCompatibilityChanged = true
                        checkCountryCompatibility()
                    }
                )
            )
            add(
                SingleChoiceSetting(
                    editingSettings,
                    null,
                    R.string.emulated_language,
                    0,
                    R.array.languageNames,
                    R.array.languageValues,
                    getValue = { SystemSaveGame.getSystemLanguage() },
                    setValue = { SystemSaveGame.setSystemLanguage(it) }
                )
            )
            add(
                StringInputSetting(
                    editingSettings,
                    null,
                    R.string.username,
                    0,
                    null,
                    "AZAHAR",
                    10,
                    getValue = { SystemSaveGame.getUsername() },
                    setValue = { SystemSaveGame.setUsername(it) }
                )
            )
            add(
                SliderSetting(
                    editingSettings,
                    null,
                    R.string.play_coins,
                    0,
                    0,
                    300,
                    "",
                    getValue = { SystemSaveGame.getPlayCoins().toFloat() },
                    setValue = { SystemSaveGame.setPlayCoins(it.roundToInt()) }
                )
            )
            add(
                SliderSetting(
                    editingSettings,
                    IntSetting.STEPS_PER_HOUR,
                    R.string.steps_per_hour,
                    R.string.steps_per_hour_description,
                    0,
                    65535,
                    " steps",
                    IntSetting.STEPS_PER_HOUR.key,
                    IntSetting.STEPS_PER_HOUR.defaultValue.toFloat()
                )
            )
            add(
                RunnableSetting(
                    R.string.console_id,
                    0,
                    false,
                    0,
                    { settingsAdapter.onClickRegenerateConsoleId() },
                    { "0x${SystemSaveGame.getConsoleId().toHexString().uppercase()}" }
                )
            )
            add(
                RunnableSetting(
                    R.string.mac_address,
                    0,
                    false,
                    0,
                    { settingsAdapter.onClickRegenerateMAC() },
                    { SystemSaveGame.getMac() }
                )
            )

            add(HeaderSetting(R.string.birthday))
            add(
                SingleChoiceSetting(
                    editingSettings,
                    null,
                    R.string.birthday_month,
                    0,
                    R.array.months,
                    R.array.monthValues,
                    getValue = { SystemSaveGame.getBirthday()[0].toInt() },
                    setValue = {
                        val value = it.toShort()
                        val birthdayDay = SystemSaveGame.getBirthday()[1]
                        val daysInNewMonth = BirthdayMonth.getMonthFromCode(value)?.days ?: 31
                        if (daysInNewMonth < birthdayDay) {
                            SystemSaveGame.setBirthday(value, 1)
                            settingsAdapter.notifyDataSetChanged()
                        } else {
                            SystemSaveGame.setBirthday(value, birthdayDay)
                        }
                    }
                )
            )

            val birthdayMonth = SystemSaveGame.getBirthday()[0]
            val daysInMonth = BirthdayMonth.getMonthFromCode(birthdayMonth)?.days ?: 31
            val dayArray = Array(daysInMonth) { "${it + 1}" }
            add(
                StringSingleChoiceSetting(
                    editingSettings,
                    null,
                    R.string.birthday_day,
                    0,
                    dayArray,
                    dayArray,
                    getValue = { SystemSaveGame.getBirthday()[1].toString() },
                    setValue = {
                        val value = it.toShort()
                        val birthdayMonth = SystemSaveGame.getBirthday()[0]
                        val daysInNewMonth =
                            BirthdayMonth.getMonthFromCode(birthdayMonth)?.days ?: 31
                        if (value > daysInNewMonth) {
                            SystemSaveGame.setBirthday(birthdayMonth, 1)
                        } else {
                            SystemSaveGame.setBirthday(birthdayMonth, value)
                        }
                    }

                )
            )

            add(HeaderSetting(R.string.clock))
            add(
                SingleChoiceSetting(
                    editingSettings,
                    IntSetting.INIT_CLOCK,
                    R.string.init_clock,
                    R.string.init_clock_description,
                    R.array.systemClockNames,
                    R.array.systemClockValues,
                    IntSetting.INIT_CLOCK.key,
                    IntSetting.INIT_CLOCK.defaultValue
                )
            )
            add(
                DateTimeSetting(
                    editingSettings,
                    StringSetting.INIT_TIME,
                    R.string.simulated_clock,
                    R.string.simulated_clock_description,
                    StringSetting.INIT_TIME.key,
                    StringSetting.INIT_TIME.defaultValue
                )
            )

            add(HeaderSetting(R.string.plugin_loader))
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.PLUGIN_LOADER,
                    R.string.plugin_loader,
                    R.string.plugin_loader_description,
                    BooleanSetting.PLUGIN_LOADER.key,
                    BooleanSetting.PLUGIN_LOADER.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.ALLOW_PLUGIN_LOADER,
                    R.string.allow_plugin_loader,
                    R.string.allow_plugin_loader_description,
                    BooleanSetting.ALLOW_PLUGIN_LOADER.key,
                    BooleanSetting.ALLOW_PLUGIN_LOADER.defaultValue
                )
            )
            add(HeaderSetting(R.string.storage))
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.COMPRESS_INSTALLED_CIA_CONTENT,
                    R.string.compress_cia_installs,
                    R.string.compress_cia_installs_description,
                    BooleanSetting.COMPRESS_INSTALLED_CIA_CONTENT.key,
                    BooleanSetting.COMPRESS_INSTALLED_CIA_CONTENT.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.ASYNC_FS_OPERATIONS,
                    R.string.async_fs_operations,
                    R.string.async_fs_operations_description,
                    BooleanSetting.ASYNC_FS_OPERATIONS.key,
                    BooleanSetting.ASYNC_FS_OPERATIONS.defaultValue
                )
            )
        }
    }

    private fun addCameraSettings(sl: ArrayList<SettingsItem>) {
        settingsActivity.setToolbarTitle(settingsActivity.getString(R.string.camera))

        // Get the camera IDs
        val cameraManager =
            settingsActivity.getSystemService(Context.CAMERA_SERVICE) as CameraManager?
        val supportedCameraNameList = ArrayList<String>()
        val supportedCameraIdList = ArrayList<String>()
        if (cameraManager != null) {
            try {
                for (id in cameraManager.cameraIdList) {
                    val characteristics = cameraManager.getCameraCharacteristics(id)
                    if (characteristics.get(CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL) ==
                        CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL_LEGACY
                    ) {
                        continue // Legacy cameras cannot be used with the NDK
                    }
                    supportedCameraIdList.add(id)
                    val facing = characteristics.get(CameraCharacteristics.LENS_FACING)
                    var stringId: Int = R.string.camera_facing_external
                    when (facing) {
                        CameraCharacteristics.LENS_FACING_FRONT ->
                            stringId =
                                R.string.camera_facing_front

                        CameraCharacteristics.LENS_FACING_BACK ->
                            stringId =
                                R.string.camera_facing_back

                        CameraCharacteristics.LENS_FACING_EXTERNAL ->
                            stringId =
                                R.string.camera_facing_external
                    }
                    supportedCameraNameList.add(
                        String.format("%1\$s (%2\$s)", id, settingsActivity.getString(stringId))
                    )
                }
            } catch (e: CameraAccessException) {
                Log.error("Couldn't retrieve camera list")
                e.printStackTrace()
            }
        }

        // Create the names and values for display
        val cameraDeviceNameList =
            settingsActivity.resources.getStringArray(R.array.cameraDeviceNames).toMutableList()
        cameraDeviceNameList.addAll(supportedCameraNameList)
        val cameraDeviceValueList =
            settingsActivity.resources.getStringArray(R.array.cameraDeviceValues).toMutableList()
        cameraDeviceValueList.addAll(supportedCameraIdList)

        val haveCameraDevices = supportedCameraIdList.isNotEmpty()

        val imageSourceNames =
            settingsActivity.resources.getStringArray(R.array.cameraImageSourceNames)
        val imageSourceValues =
            settingsActivity.resources.getStringArray(R.array.cameraImageSourceValues)
        if (!haveCameraDevices) {
            // Remove the last entry (ndk / Device Camera)
            imageSourceNames.copyOfRange(0, imageSourceNames.size - 1)
            imageSourceValues.copyOfRange(0, imageSourceValues.size - 1)
        }

        sl.apply {
            add(HeaderSetting(R.string.inner_camera))
            add(
                StringSingleChoiceSetting(
                    editingSettings,
                    StringSetting.CAMERA_INNER_NAME,
                    R.string.image_source,
                    R.string.image_source_description,
                    imageSourceNames,
                    imageSourceValues,
                    StringSetting.CAMERA_INNER_NAME.key,
                    StringSetting.CAMERA_INNER_NAME.defaultValue
                )
            )
            if (haveCameraDevices) {
                add(
                    StringSingleChoiceSetting(
                        editingSettings,
                        StringSetting.CAMERA_INNER_CONFIG,
                        R.string.camera_device,
                        R.string.camera_device_description,
                        cameraDeviceNameList.toTypedArray(),
                        cameraDeviceValueList.toTypedArray(),
                        StringSetting.CAMERA_INNER_CONFIG.key,
                        StringSetting.CAMERA_INNER_CONFIG.defaultValue
                    )
                )
            }
            add(
                SingleChoiceSetting(
                    editingSettings,
                    IntSetting.CAMERA_INNER_FLIP,
                    R.string.image_flip,
                    0,
                    R.array.cameraFlipNames,
                    R.array.cameraDeviceValues,
                    IntSetting.CAMERA_INNER_FLIP.key,
                    IntSetting.CAMERA_INNER_FLIP.defaultValue
                )
            )

            add(HeaderSetting(R.string.outer_left_camera))
            add(
                StringSingleChoiceSetting(
                    editingSettings,
                    StringSetting.CAMERA_OUTER_LEFT_NAME,
                    R.string.image_source,
                    R.string.image_source_description,
                    imageSourceNames,
                    imageSourceValues,
                    StringSetting.CAMERA_OUTER_LEFT_NAME.key,
                    StringSetting.CAMERA_OUTER_LEFT_NAME.defaultValue
                )
            )
            if (haveCameraDevices) {
                add(
                    StringSingleChoiceSetting(
                        editingSettings,
                        StringSetting.CAMERA_OUTER_LEFT_CONFIG,
                        R.string.camera_device,
                        R.string.camera_device_description,
                        cameraDeviceNameList.toTypedArray(),
                        cameraDeviceValueList.toTypedArray(),
                        StringSetting.CAMERA_OUTER_LEFT_CONFIG.key,
                        StringSetting.CAMERA_OUTER_LEFT_CONFIG.defaultValue
                    )
                )
            }
            add(
                SingleChoiceSetting(
                    editingSettings,
                    IntSetting.CAMERA_OUTER_LEFT_FLIP,
                    R.string.image_flip,
                    0,
                    R.array.cameraFlipNames,
                    R.array.cameraDeviceValues,
                    IntSetting.CAMERA_OUTER_LEFT_FLIP.key,
                    IntSetting.CAMERA_OUTER_LEFT_FLIP.defaultValue
                )
            )

            add(HeaderSetting(R.string.outer_right_camera))
            add(
                StringSingleChoiceSetting(
                    editingSettings,
                    StringSetting.CAMERA_OUTER_RIGHT_NAME,
                    R.string.image_source,
                    R.string.image_source_description,
                    imageSourceNames,
                    imageSourceValues,
                    StringSetting.CAMERA_OUTER_RIGHT_NAME.key,
                    StringSetting.CAMERA_OUTER_RIGHT_NAME.defaultValue
                )
            )
            if (haveCameraDevices) {
                add(
                    StringSingleChoiceSetting(
                        editingSettings,
                        StringSetting.CAMERA_OUTER_RIGHT_CONFIG,
                        R.string.camera_device,
                        R.string.camera_device_description,
                        cameraDeviceNameList.toTypedArray(),
                        cameraDeviceValueList.toTypedArray(),
                        StringSetting.CAMERA_OUTER_RIGHT_CONFIG.key,
                        StringSetting.CAMERA_OUTER_RIGHT_CONFIG.defaultValue
                    )
                )
            }
            add(
                SingleChoiceSetting(
                    editingSettings,
                    IntSetting.CAMERA_OUTER_RIGHT_FLIP,
                    R.string.image_flip,
                    0,
                    R.array.cameraFlipNames,
                    R.array.cameraDeviceValues,
                    IntSetting.CAMERA_OUTER_RIGHT_FLIP.key,
                    IntSetting.CAMERA_OUTER_RIGHT_FLIP.defaultValue
                )
            )
        }
    }

    private fun addControlsSettings(sl: ArrayList<SettingsItem>) {
        settingsActivity.setToolbarTitle(settingsActivity.getString(R.string.preferences_controls))

        sl.apply {
            add(
                RunnableSetting(
                    R.string.controller_auto_map,
                    R.string.controller_auto_map_description,
                    true,
                    R.drawable.ic_controller,
                    { settingsAdapter.onClickAutoMap() },
                    onLongClick = { settingsAdapter.onLongClickAutoMap() }
                )
            )

            add(HeaderSetting(R.string.generic_buttons))
            CitraSettings.buttonKeys.forEachIndexed { i: Int, key: String ->
                val button = getInputObject(key)
                add(InputBindingSetting(button, CitraSettings.buttonTitles[i]))
            }

            add(HeaderSetting(R.string.controller_circlepad))
            CitraSettings.circlePadKeys.forEachIndexed { i: Int, key: String ->
                val button = getInputObject(key)
                add(InputBindingSetting(button, CitraSettings.axisTitles[i]))
            }

            add(HeaderSetting(R.string.controller_c))
            CitraSettings.cStickKeys.forEachIndexed { i: Int, key: String ->
                val button = getInputObject(key)
                add(InputBindingSetting(button, CitraSettings.axisTitles[i]))
            }

            add(
                HeaderSetting(
                    R.string.controller_dpad_axis,
                    R.string.controller_dpad_axis_description
                )
            )
            CitraSettings.dPadAxisKeys.forEachIndexed { i: Int, key: String ->
                val button = getInputObject(key)
                add(InputBindingSetting(button, CitraSettings.axisTitles[i]))
            }
            add(
                HeaderSetting(
                    R.string.controller_dpad_button,
                    R.string.controller_dpad_button_description
                )
            )
            CitraSettings.dPadButtonKeys.forEachIndexed { i: Int, key: String ->
                val button = getInputObject(key)
                add(InputBindingSetting(button, CitraSettings.dPadTitles[i]))
            }

            add(HeaderSetting(R.string.controller_triggers))
            CitraSettings.triggerKeys.forEachIndexed { i: Int, key: String ->
                val button = getInputObject(key)
                add(InputBindingSetting(button, CitraSettings.triggerTitles[i]))
            }

            add(HeaderSetting(R.string.controller_hotkeys, R.string.controller_hotkeys_description))
            CitraSettings.hotKeys.forEachIndexed { i: Int, key: String ->
                val button = getInputObject(key)
                add(InputBindingSetting(button, CitraSettings.hotkeyTitles[i]))
            }

            add(HeaderSetting(R.string.miscellaneous))
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.USE_ARTIC_BASE_CONTROLLER,
                    R.string.use_artic_base_controller,
                    R.string.use_artic_base_controller_description,
                    BooleanSetting.USE_ARTIC_BASE_CONTROLLER.key,
                    BooleanSetting.USE_ARTIC_BASE_CONTROLLER.defaultValue
                )
            )

            add(
                MultiChoiceSetting(
                    editingSettings,
                    IntListSetting.COMBO_BUTTON_BUTTONS,
                    R.string.combo_button_settings,
                    R.string.combo_button_settings_description,
                    R.array.comboOptions,
                    R.array.comboOptionValues,
                    IntListSetting.COMBO_BUTTON_BUTTONS.key,
                    IntListSetting.COMBO_BUTTON_BUTTONS.defaultValue
                )
            )
        }
    }

    private fun getInputObject(key: String): AbstractSetting<String> =
        object : AbstractSetting<String> {
            override val key = key
            override val section = CitraSettings.SECTION_CONTROLS
            override val isRuntimeEditable = true
            override val defaultValue = ""
            override fun valueFromString(string: String): String = string
            override fun valueToString(value: String): String = value
            // TODO: make input mappings also work per-game, which will be easy if we move
            //  them to config files
        }
    private fun addGraphicsSettings(sl: ArrayList<SettingsItem>) {
        settingsActivity.setToolbarTitle(settingsActivity.getString(R.string.preferences_graphics))
        sl.apply {
            add(HeaderSetting(R.string.renderer))
            add(
                SingleChoiceSetting(
                    editingSettings,
                    IntSetting.GRAPHICS_API,
                    R.string.graphics_api,
                    0,
                    R.array.graphicsApiNames,
                    R.array.graphicsApiValues,
                    IntSetting.GRAPHICS_API.key,
                    IntSetting.GRAPHICS_API.defaultValue,
                    isEnabled = !GraphicsUtil.isUsingAngleForOpenGL(),
                    disabledMessage = R.string.setting_disabled_description_angle
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.SPIRV_SHADER_GEN,
                    R.string.spirv_shader_gen,
                    R.string.spirv_shader_gen_description,
                    BooleanSetting.SPIRV_SHADER_GEN.key,
                    BooleanSetting.SPIRV_SHADER_GEN.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.DISABLE_SPIRV_OPTIMIZER,
                    R.string.disable_spirv_optimizer,
                    R.string.disable_spirv_optimizer_description,
                    BooleanSetting.DISABLE_SPIRV_OPTIMIZER.key,
                    BooleanSetting.DISABLE_SPIRV_OPTIMIZER.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.ASYNC_SHADERS,
                    R.string.async_shaders,
                    R.string.async_shaders_description,
                    BooleanSetting.ASYNC_SHADERS.key,
                    BooleanSetting.ASYNC_SHADERS.defaultValue
                )
            )
            add(
                SingleChoiceSetting(
                    editingSettings,
                    IntSetting.RESOLUTION_FACTOR,
                    R.string.internal_resolution,
                    R.string.internal_resolution_description,
                    R.array.resolutionFactorNames,
                    R.array.resolutionFactorValues,
                    IntSetting.RESOLUTION_FACTOR.key,
                    IntSetting.RESOLUTION_FACTOR.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.USE_INTEGER_SCALING,
                    R.string.use_integer_scaling,
                    R.string.use_integer_scaling_description,
                    BooleanSetting.USE_INTEGER_SCALING.key,
                    BooleanSetting.USE_INTEGER_SCALING.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.LINEAR_FILTERING,
                    R.string.linear_filtering,
                    R.string.linear_filtering_description,
                    BooleanSetting.LINEAR_FILTERING.key,
                    BooleanSetting.LINEAR_FILTERING.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.SHADERS_ACCURATE_MUL,
                    R.string.shaders_accurate_mul,
                    R.string.shaders_accurate_mul_description,
                    BooleanSetting.SHADERS_ACCURATE_MUL.key,
                    BooleanSetting.SHADERS_ACCURATE_MUL.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.DISK_SHADER_CACHE,
                    R.string.use_disk_shader_cache,
                    R.string.use_disk_shader_cache_description,
                    BooleanSetting.DISK_SHADER_CACHE.key,
                    BooleanSetting.DISK_SHADER_CACHE.defaultValue
                )
            )
            add(
                SingleChoiceSetting(
                    editingSettings,
                    IntSetting.TEXTURE_FILTER,
                    R.string.texture_filter_name,
                    R.string.texture_filter_description,
                    R.array.textureFilterNames,
                    R.array.textureFilterValues,
                    IntSetting.TEXTURE_FILTER.key,
                    IntSetting.TEXTURE_FILTER.defaultValue
                )
            )
            add(
                SliderSetting(
                    editingSettings,
                    IntSetting.DELAY_RENDER_THREAD_US,
                    R.string.delay_render_thread,
                    R.string.delay_render_thread_description,
                    0,
                    16000,
                    " μs",
                    IntSetting.DELAY_RENDER_THREAD_US.key,
                    IntSetting.DELAY_RENDER_THREAD_US.defaultValue.toFloat()
                )
            )

            add(HeaderSetting(R.string.stereoscopy))
            add(
                SingleChoiceSetting(
                    editingSettings,
                    IntSetting.RENDER_3D_WHICH_DISPLAY,
                    R.string.render_3d_which_display,
                    R.string.render_3d_which_display_description,
                    R.array.render3dWhichDisplay,
                    R.array.render3dDisplayValues,
                    IntSetting.RENDER_3D_WHICH_DISPLAY.key,
                    IntSetting.RENDER_3D_WHICH_DISPLAY.defaultValue
                )
            )
            add(
                SingleChoiceSetting(
                    editingSettings,
                    IntSetting.STEREOSCOPIC_3D_MODE,
                    R.string.render3d,
                    R.string.render3d_description,
                    R.array.render3dModes,
                    R.array.render3dValues,
                    IntSetting.STEREOSCOPIC_3D_MODE.key,
                    IntSetting.STEREOSCOPIC_3D_MODE.defaultValue,
                    isEnabled =
                        editingSettings.get(IntSetting.RENDER_3D_WHICH_DISPLAY) !=
                            StereoWhichDisplay.NONE.int
                )
            )

            add(
                SliderSetting(
                    editingSettings,
                    IntSetting.STEREOSCOPIC_3D_DEPTH,
                    R.string.factor3d,
                    R.string.factor3d_description,
                    0,
                    255,
                    "%",
                    IntSetting.STEREOSCOPIC_3D_DEPTH.key,
                    IntSetting.STEREOSCOPIC_3D_DEPTH.defaultValue.toFloat()
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.DISABLE_RIGHT_EYE_RENDER,
                    R.string.disable_right_eye_render,
                    R.string.disable_right_eye_render_description,
                    BooleanSetting.DISABLE_RIGHT_EYE_RENDER.key,
                    BooleanSetting.DISABLE_RIGHT_EYE_RENDER.defaultValue
                )
            )

            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.SWAP_EYES_3D,
                    R.string.swap_eyes_3d,
                    R.string.swap_eyes_3d_description,
                    BooleanSetting.SWAP_EYES_3D.key,
                    BooleanSetting.SWAP_EYES_3D.defaultValue,
                    isEnabled =
                        editingSettings.get(IntSetting.RENDER_3D_WHICH_DISPLAY) !=
                            StereoWhichDisplay.NONE.int
                )
            )

            add(HeaderSetting(R.string.cardboard_vr))
            add(
                SliderSetting(
                    editingSettings,
                    IntSetting.CARDBOARD_SCREEN_SIZE,
                    R.string.cardboard_screen_size,
                    R.string.cardboard_screen_size_description,
                    30,
                    100,
                    "%",
                    IntSetting.CARDBOARD_SCREEN_SIZE.key,
                    IntSetting.CARDBOARD_SCREEN_SIZE.defaultValue.toFloat(),
                    isEnabled =
                        editingSettings.get(IntSetting.STEREOSCOPIC_3D_MODE) ==
                            StereoMode.CARDBOARD_VR.int
                )
            )
            add(
                SliderSetting(
                    editingSettings,
                    IntSetting.CARDBOARD_X_SHIFT,
                    R.string.cardboard_x_shift,
                    R.string.cardboard_x_shift_description,
                    -100,
                    100,
                    "%",
                    IntSetting.CARDBOARD_X_SHIFT.key,
                    IntSetting.CARDBOARD_X_SHIFT.defaultValue.toFloat(),
                    isEnabled =
                        editingSettings.get(IntSetting.STEREOSCOPIC_3D_MODE) ==
                            StereoMode.CARDBOARD_VR.int
                )
            )
            add(
                SliderSetting(
                    editingSettings,
                    IntSetting.CARDBOARD_Y_SHIFT,
                    R.string.cardboard_y_shift,
                    R.string.cardboard_y_shift_description,
                    -100,
                    100,
                    "%",
                    IntSetting.CARDBOARD_Y_SHIFT.key,
                    IntSetting.CARDBOARD_Y_SHIFT.defaultValue.toFloat(),
                    isEnabled =
                        editingSettings.get(IntSetting.STEREOSCOPIC_3D_MODE) ==
                            StereoMode.CARDBOARD_VR.int
                )
            )

            add(HeaderSetting(R.string.utility))
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.DUMP_TEXTURES,
                    R.string.dump_textures,
                    R.string.dump_textures_description,
                    BooleanSetting.DUMP_TEXTURES.key,
                    BooleanSetting.DUMP_TEXTURES.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.CUSTOM_TEXTURES,
                    R.string.custom_textures,
                    R.string.custom_textures_description,
                    BooleanSetting.CUSTOM_TEXTURES.key,
                    BooleanSetting.CUSTOM_TEXTURES.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.ASYNC_CUSTOM_LOADING,
                    R.string.async_custom_loading,
                    R.string.async_custom_loading_description,
                    BooleanSetting.ASYNC_CUSTOM_LOADING.key,
                    BooleanSetting.ASYNC_CUSTOM_LOADING.defaultValue
                )
            )

            add(HeaderSetting(R.string.advanced))
            add(
                SingleChoiceSetting(
                    editingSettings,
                    IntSetting.TEXTURE_SAMPLING,
                    R.string.texture_sampling_name,
                    R.string.texture_sampling_description,
                    R.array.textureSamplingNames,
                    R.array.textureSamplingValues,
                    IntSetting.TEXTURE_SAMPLING.key,
                    IntSetting.TEXTURE_SAMPLING.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.USE_SKIP_DUPLICATE_FRAMES,
                    R.string.use_skip_duplicate_frames,
                    R.string.use_skip_duplicate_frames_description,
                    BooleanSetting.USE_SKIP_DUPLICATE_FRAMES.key,
                    BooleanSetting.USE_SKIP_DUPLICATE_FRAMES.defaultValue
                )
            )

            // Disabled until custom texture implementation gets rewrite, current one overloads RAM
            // and crashes Citra.
            // add(
            //     SwitchSetting(
            //         BooleanSetting.PRELOAD_TEXTURES,
            //         R.string.preload_textures,
            //         R.string.preload_textures_description,
            //         BooleanSetting.PRELOAD_TEXTURES.key,
            //         BooleanSetting.PRELOAD_TEXTURES.defaultValue
            //     )
            // )
        }
    }

    private fun addLayoutSettings(sl: ArrayList<SettingsItem>) {
        settingsActivity.setToolbarTitle(settingsActivity.getString(R.string.preferences_layout))
        sl.apply {
            add(
                SingleChoiceSetting(
                    editingSettings,
                    IntSetting.ORIENTATION_OPTION,
                    R.string.layout_screen_orientation,
                    0,
                    R.array.screenOrientations,
                    R.array.screenOrientationValues,
                    IntSetting.ORIENTATION_OPTION.key,
                    IntSetting.ORIENTATION_OPTION.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.EXPAND_TO_CUTOUT_AREA,
                    R.string.expand_to_cutout_area,
                    R.string.expand_to_cutout_area_description,
                    BooleanSetting.EXPAND_TO_CUTOUT_AREA.key,
                    BooleanSetting.EXPAND_TO_CUTOUT_AREA.defaultValue
                )
            )
            add(
                SingleChoiceSetting(
                    editingSettings,
                    IntSetting.SCREEN_LAYOUT,
                    R.string.emulation_switch_screen_layout,
                    0,
                    R.array.landscapeLayouts,
                    R.array.landscapeLayoutValues,
                    IntSetting.SCREEN_LAYOUT.key,
                    IntSetting.SCREEN_LAYOUT.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.UPRIGHT_SCREEN,
                    R.string.emulation_rotate_upright,
                    0,
                    BooleanSetting.UPRIGHT_SCREEN.key,
                    BooleanSetting.UPRIGHT_SCREEN.defaultValue
                )
            )
            add(
                MultiChoiceSetting(
                    editingSettings,
                    IntListSetting.LAYOUTS_TO_CYCLE,
                    R.string.layouts_to_cycle,
                    R.string.layouts_to_cycle_description,
                    R.array.landscapeLayouts,
                    R.array.landscapeLayoutValues,
                    IntListSetting.LAYOUTS_TO_CYCLE.key,
                    IntListSetting.LAYOUTS_TO_CYCLE.defaultValue
                )
            )
            add(
                SingleChoiceSetting(
                    editingSettings,
                    IntSetting.PORTRAIT_SCREEN_LAYOUT,
                    R.string.emulation_switch_portrait_layout,
                    0,
                    R.array.portraitLayouts,
                    R.array.portraitLayoutValues,
                    IntSetting.PORTRAIT_SCREEN_LAYOUT.key,
                    IntSetting.PORTRAIT_SCREEN_LAYOUT.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.ENABLE_SECONDARY_DISPLAY,
                    R.string.emulation_secondary_display_enable,
                    R.string.emulation_secondary_display_enable_description,
                    BooleanSetting.ENABLE_SECONDARY_DISPLAY.key,
                    BooleanSetting.ENABLE_SECONDARY_DISPLAY.defaultValue
                )
            )
            add(
                SingleChoiceSetting(
                    editingSettings,
                    IntSetting.SECONDARY_DISPLAY_LAYOUT,
                    R.string.emulation_switch_secondary_layout,
                    R.string.emulation_switch_secondary_layout_description,
                    R.array.secondaryLayouts,
                    R.array.secondaryLayoutValues,
                    IntSetting.SECONDARY_DISPLAY_LAYOUT.key,
                    IntSetting.SECONDARY_DISPLAY_LAYOUT.defaultValue,
                    editingSettings.get(BooleanSetting.ENABLE_SECONDARY_DISPLAY)
                )
            )
            add(
                SingleChoiceSetting(
                    editingSettings,
                    IntSetting.ASPECT_RATIO,
                    R.string.emulation_aspect_ratio,
                    0,
                    R.array.aspectRatioNames,
                    R.array.aspectRatioValues,
                    IntSetting.ASPECT_RATIO.key,
                    IntSetting.ASPECT_RATIO.defaultValue,
                    isEnabled =
                        editingSettings.get(IntSetting.SCREEN_LAYOUT) ==
                            ScreenLayout.SINGLE_SCREEN.int
                )
            )
            add(
                SingleChoiceSetting(
                    editingSettings,
                    IntSetting.SMALL_SCREEN_POSITION,
                    R.string.emulation_small_screen_position,
                    R.string.small_screen_position_description,
                    R.array.smallScreenPositions,
                    R.array.smallScreenPositionValues,
                    IntSetting.SMALL_SCREEN_POSITION.key,
                    IntSetting.SMALL_SCREEN_POSITION.defaultValue
                )
            )
            add(
                SliderSetting(
                    editingSettings,
                    IntSetting.SCREEN_GAP,
                    R.string.screen_gap,
                    R.string.screen_gap_description,
                    0,
                    480,
                    "px",
                    IntSetting.SCREEN_GAP.key,
                    IntSetting.SCREEN_GAP.defaultValue.toFloat()
                )
            )
            add(
                SliderSetting(
                    editingSettings,
                    FloatSetting.LARGE_SCREEN_PROPORTION,
                    R.string.large_screen_proportion,
                    R.string.large_screen_proportion_description,
                    1,
                    5,
                    "",
                    FloatSetting.LARGE_SCREEN_PROPORTION.key,
                    FloatSetting.LARGE_SCREEN_PROPORTION.defaultValue
                )
            )
            add(
                SliderSetting(
                    editingSettings,
                    FloatSetting.SECOND_SCREEN_OPACITY,
                    R.string.second_screen_opacity,
                    R.string.second_screen_opacity_description,
                    0,
                    100,
                    "%",
                    FloatSetting.SECOND_SCREEN_OPACITY.key,
                    FloatSetting.SECOND_SCREEN_OPACITY.defaultValue,
                    0,
                    isEnabled =
                        editingSettings.get(IntSetting.SCREEN_LAYOUT) ==
                            ScreenLayout.CUSTOM_LAYOUT.int
                )
            )
            add(HeaderSetting(R.string.bg_color, R.string.bg_color_description))
            add(
                SliderSetting(
                    editingSettings,
                    FloatSetting.BACKGROUND_RED,
                    R.string.bg_red,
                    0,
                    0,
                    255,
                    "",
                    decimals = 0
                )
            )
            add(
                SliderSetting(
                    editingSettings,
                    FloatSetting.BACKGROUND_GREEN,
                    R.string.bg_green,
                    0,
                    0,
                    255,
                    "",
                    decimals = 0
                )
            )
            add(
                SliderSetting(
                    editingSettings,
                    FloatSetting.BACKGROUND_BLUE,
                    R.string.bg_blue,
                    0,
                    0,
                    255,
                    "",
                    decimals = 0
                )
            )
            add(
                SubmenuSetting(
                    R.string.performance_overlay_options,
                    R.string.performance_overlay_options_description,
                    R.drawable.ic_stats,
                    CitraSettings.SECTION_PERFORMANCE_OVERLAY
                )
            )
            add(
                SubmenuSetting(
                    R.string.emulation_landscape_custom_layout,
                    0,
                    R.drawable.ic_fit_screen,
                    CitraSettings.SECTION_CUSTOM_LANDSCAPE
                )
            )
            add(
                SubmenuSetting(
                    R.string.emulation_portrait_custom_layout,
                    0,
                    R.drawable.ic_portrait_fit_screen,
                    CitraSettings.SECTION_CUSTOM_PORTRAIT
                )
            )
        }
    }

    private fun addPerformanceOverlaySettings(sl: ArrayList<SettingsItem>) {
        settingsActivity.setToolbarTitle(
            settingsActivity.getString(R.string.performance_overlay_options)
        )
        sl.apply {
            add(HeaderSetting(R.string.visibility))

            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.PERF_OVERLAY_ENABLE,
                    R.string.performance_overlay_enable,
                    0,
                    BooleanSetting.PERF_OVERLAY_ENABLE.key,
                    BooleanSetting.PERF_OVERLAY_ENABLE.defaultValue
                )
            )

            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.PERF_OVERLAY_BACKGROUND,
                    R.string.performance_overlay_background,
                    R.string.performance_overlay_background_description,
                    BooleanSetting.PERF_OVERLAY_BACKGROUND.key,
                    BooleanSetting.PERF_OVERLAY_BACKGROUND.defaultValue
                )
            )

            add(
                SingleChoiceSetting(
                    editingSettings,
                    IntSetting.PERFORMANCE_OVERLAY_POSITION,
                    R.string.performance_overlay_position,
                    R.string.performance_overlay_position_description,
                    R.array.statsPosition,
                    R.array.statsPositionValues
                )
            )

            add(HeaderSetting(R.string.information))

            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.PERF_OVERLAY_SHOW_FPS,
                    R.string.performance_overlay_show_fps,
                    R.string.performance_overlay_show_fps_description,
                    BooleanSetting.PERF_OVERLAY_SHOW_FPS.key,
                    BooleanSetting.PERF_OVERLAY_SHOW_FPS.defaultValue
                )
            )

            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.PERF_OVERLAY_SHOW_FRAMETIME,
                    R.string.performance_overlay_show_frametime,
                    R.string.performance_overlay_show_frametime_description,
                    BooleanSetting.PERF_OVERLAY_SHOW_FRAMETIME.key,
                    BooleanSetting.PERF_OVERLAY_SHOW_FRAMETIME.defaultValue
                )
            )

            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.PERF_OVERLAY_SHOW_SPEED,
                    R.string.performance_overlay_show_speed,
                    R.string.performance_overlay_show_speed_description,
                    BooleanSetting.PERF_OVERLAY_SHOW_SPEED.key,
                    BooleanSetting.PERF_OVERLAY_SHOW_SPEED.defaultValue
                )
            )

            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.PERF_OVERLAY_SHOW_APP_RAM_USAGE,
                    R.string.performance_overlay_show_app_ram_usage,
                    R.string.performance_overlay_show_app_ram_usage_description,
                    BooleanSetting.PERF_OVERLAY_SHOW_APP_RAM_USAGE.key,
                    BooleanSetting.PERF_OVERLAY_SHOW_APP_RAM_USAGE.defaultValue
                )
            )

            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.PERF_OVERLAY_SHOW_AVAILABLE_RAM,
                    R.string.performance_overlay_show_available_ram,
                    R.string.performance_overlay_show_available_ram_description,
                    BooleanSetting.PERF_OVERLAY_SHOW_AVAILABLE_RAM.key,
                    BooleanSetting.PERF_OVERLAY_SHOW_AVAILABLE_RAM.defaultValue
                )
            )

            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.PERF_OVERLAY_SHOW_BATTERY_TEMP,
                    R.string.performance_overlay_show_battery_temp,
                    R.string.performance_overlay_show_battery_temp_description,
                    BooleanSetting.PERF_OVERLAY_SHOW_BATTERY_TEMP.key,
                    BooleanSetting.PERF_OVERLAY_SHOW_BATTERY_TEMP.defaultValue
                )
            )
        }
    }

    private fun addCustomLandscapeSettings(sl: ArrayList<SettingsItem>) {
        settingsActivity.setToolbarTitle(
            settingsActivity.getString(R.string.emulation_landscape_custom_layout)
        )
        sl.apply {
            add(HeaderSetting(R.string.emulation_top_screen))
            add(
                SliderSetting(
                    editingSettings,
                    IntSetting.LANDSCAPE_TOP_X,
                    R.string.emulation_custom_layout_x,
                    0,
                    0,
                    getLargerDimension(),
                    "px",
                    IntSetting.LANDSCAPE_TOP_X.key,
                    IntSetting.LANDSCAPE_TOP_X.defaultValue.toFloat()
                )
            )
            add(
                SliderSetting(
                    editingSettings,
                    IntSetting.LANDSCAPE_TOP_Y,
                    R.string.emulation_custom_layout_y,
                    0,
                    0,
                    getSmallerDimension(),
                    "px",
                    IntSetting.LANDSCAPE_TOP_Y.key,
                    IntSetting.LANDSCAPE_TOP_Y.defaultValue.toFloat()
                )
            )
            add(
                SliderSetting(
                    editingSettings,
                    IntSetting.LANDSCAPE_TOP_WIDTH,
                    R.string.emulation_custom_layout_width,
                    0,
                    0,
                    getLargerDimension(),
                    "px",
                    IntSetting.LANDSCAPE_TOP_WIDTH.key,
                    IntSetting.LANDSCAPE_TOP_WIDTH.defaultValue.toFloat()
                )
            )
            add(
                SliderSetting(
                    editingSettings,
                    IntSetting.LANDSCAPE_TOP_HEIGHT,
                    R.string.emulation_custom_layout_height,
                    0,
                    0,
                    getSmallerDimension(),
                    "px",
                    IntSetting.LANDSCAPE_TOP_HEIGHT.key,
                    IntSetting.LANDSCAPE_TOP_HEIGHT.defaultValue.toFloat()
                )
            )
            add(HeaderSetting(R.string.emulation_bottom_screen))
            add(
                SliderSetting(
                    editingSettings,
                    IntSetting.LANDSCAPE_BOTTOM_X,
                    R.string.emulation_custom_layout_x,
                    0,
                    0,
                    getLargerDimension(),
                    "px",
                    IntSetting.LANDSCAPE_BOTTOM_X.key,
                    IntSetting.LANDSCAPE_BOTTOM_X.defaultValue.toFloat()
                )
            )
            add(
                SliderSetting(
                    editingSettings,
                    IntSetting.LANDSCAPE_BOTTOM_Y,
                    R.string.emulation_custom_layout_y,
                    0,
                    0,
                    getSmallerDimension(),
                    "px",
                    IntSetting.LANDSCAPE_BOTTOM_Y.key,
                    IntSetting.LANDSCAPE_BOTTOM_Y.defaultValue.toFloat()
                )
            )
            add(
                SliderSetting(
                    editingSettings,
                    IntSetting.LANDSCAPE_BOTTOM_WIDTH,
                    R.string.emulation_custom_layout_width,
                    0,
                    0,
                    getLargerDimension(),
                    "px",
                    IntSetting.LANDSCAPE_BOTTOM_WIDTH.key,
                    IntSetting.LANDSCAPE_BOTTOM_WIDTH.defaultValue.toFloat()
                )
            )
            add(
                SliderSetting(
                    editingSettings,
                    IntSetting.LANDSCAPE_BOTTOM_HEIGHT,
                    R.string.emulation_custom_layout_height,
                    0,
                    0,
                    getSmallerDimension(),
                    "px",
                    IntSetting.LANDSCAPE_BOTTOM_HEIGHT.key,
                    IntSetting.LANDSCAPE_BOTTOM_HEIGHT.defaultValue.toFloat()
                )
            )
        }
    }

    private fun addCustomPortraitSettings(sl: ArrayList<SettingsItem>) {
        settingsActivity.setToolbarTitle(
            settingsActivity.getString(R.string.emulation_portrait_custom_layout)
        )
        sl.apply {
            add(HeaderSetting(R.string.emulation_top_screen))
            add(
                SliderSetting(
                    editingSettings,
                    IntSetting.PORTRAIT_TOP_X,
                    R.string.emulation_custom_layout_x,
                    0,
                    0,
                    getSmallerDimension(),
                    "px",
                    IntSetting.PORTRAIT_TOP_X.key,
                    IntSetting.PORTRAIT_TOP_X.defaultValue.toFloat()
                )
            )
            add(
                SliderSetting(
                    editingSettings,
                    IntSetting.PORTRAIT_TOP_Y,
                    R.string.emulation_custom_layout_y,
                    0,
                    0,
                    getLargerDimension(),
                    "px",
                    IntSetting.PORTRAIT_TOP_Y.key,
                    IntSetting.PORTRAIT_TOP_Y.defaultValue.toFloat()
                )
            )
            add(
                SliderSetting(
                    editingSettings,
                    IntSetting.PORTRAIT_TOP_WIDTH,
                    R.string.emulation_custom_layout_width,
                    0,
                    0,
                    getSmallerDimension(),
                    "px",
                    IntSetting.PORTRAIT_TOP_WIDTH.key,
                    IntSetting.PORTRAIT_TOP_WIDTH.defaultValue.toFloat()
                )
            )
            add(
                SliderSetting(
                    editingSettings,
                    IntSetting.PORTRAIT_TOP_HEIGHT,
                    R.string.emulation_custom_layout_height,
                    0,
                    0,
                    getLargerDimension(),
                    "px",
                    IntSetting.PORTRAIT_TOP_HEIGHT.key,
                    IntSetting.PORTRAIT_TOP_HEIGHT.defaultValue.toFloat()
                )
            )
            add(HeaderSetting(R.string.emulation_bottom_screen))
            add(
                SliderSetting(
                    editingSettings,
                    IntSetting.PORTRAIT_BOTTOM_X,
                    R.string.emulation_custom_layout_x,
                    0,
                    0,
                    getSmallerDimension(),
                    "px",
                    IntSetting.PORTRAIT_BOTTOM_X.key,
                    IntSetting.PORTRAIT_BOTTOM_X.defaultValue.toFloat()
                )
            )
            add(
                SliderSetting(
                    editingSettings,
                    IntSetting.PORTRAIT_BOTTOM_Y,
                    R.string.emulation_custom_layout_y,
                    0,
                    0,
                    getLargerDimension(),
                    "px",
                    IntSetting.PORTRAIT_BOTTOM_Y.key,
                    IntSetting.PORTRAIT_BOTTOM_Y.defaultValue.toFloat()
                )
            )
            add(
                SliderSetting(
                    editingSettings,
                    IntSetting.PORTRAIT_BOTTOM_WIDTH,
                    R.string.emulation_custom_layout_width,
                    0,
                    0,
                    getSmallerDimension(),
                    "px",
                    IntSetting.PORTRAIT_BOTTOM_WIDTH.key,
                    IntSetting.PORTRAIT_BOTTOM_WIDTH.defaultValue.toFloat()
                )
            )
            add(
                SliderSetting(
                    editingSettings,
                    IntSetting.PORTRAIT_BOTTOM_HEIGHT,
                    R.string.emulation_custom_layout_height,
                    0,
                    0,
                    getLargerDimension(),
                    "px",
                    IntSetting.PORTRAIT_BOTTOM_HEIGHT.key,
                    IntSetting.PORTRAIT_BOTTOM_HEIGHT.defaultValue.toFloat()
                )
            )
        }
    }

    private fun addNetworkSettings(sl: ArrayList<SettingsItem>) {
        settingsActivity.setToolbarTitle(settingsActivity.getString(R.string.preferences_network))
        sl.apply {
            add(
                StringInputSetting(
                    editingSettings,
                    StringSetting.WEB_API_URL,
                    R.string.web_api_url,
                    R.string.web_api_url_description,
                    StringSetting.WEB_API_URL.key,
                    StringSetting.WEB_API_URL.defaultValue
                )
            )
            add(
                StringInputSetting(
                    editingSettings,
                    StringSetting.NETWORK_TOKEN,
                    R.string.network_token,
                    R.string.network_token_description,
                    StringSetting.NETWORK_TOKEN.key,
                    StringSetting.NETWORK_TOKEN.defaultValue
                )
            )
        }
    }

    private fun addAudioSettings(sl: ArrayList<SettingsItem>) {
        settingsActivity.setToolbarTitle(settingsActivity.getString(R.string.preferences_audio))
        sl.apply {
            add(
                SliderSetting(
                    editingSettings,
                    FloatSetting.AUDIO_VOLUME,
                    R.string.audio_volume,
                    0,
                    0,
                    100,
                    "%",
                    FloatSetting.AUDIO_VOLUME.key,
                    FloatSetting.AUDIO_VOLUME.defaultValue,
                    decimals = 0
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.ENABLE_AUDIO_STRETCHING,
                    R.string.audio_stretch,
                    R.string.audio_stretch_description,
                    BooleanSetting.ENABLE_AUDIO_STRETCHING.key,
                    BooleanSetting.ENABLE_AUDIO_STRETCHING.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.ENABLE_REALTIME_AUDIO,
                    R.string.realtime_audio,
                    R.string.realtime_audio_description,
                    BooleanSetting.ENABLE_REALTIME_AUDIO.key,
                    BooleanSetting.ENABLE_REALTIME_AUDIO.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.SIMULATE_HEADPHONES_PLUGGED,
                    R.string.simulate_headphones_plugged,
                    R.string.simulate_headphones_plugged_description,
                    BooleanSetting.SIMULATE_HEADPHONES_PLUGGED.key,
                    BooleanSetting.SIMULATE_HEADPHONES_PLUGGED.defaultValue
                )
            )
            add(
                SingleChoiceSetting(
                    editingSettings,
                    IntSetting.AUDIO_INPUT_TYPE,
                    R.string.audio_input_type,
                    0,
                    R.array.audioInputTypeNames,
                    R.array.audioInputTypeValues,
                    IntSetting.AUDIO_INPUT_TYPE.key,
                    IntSetting.AUDIO_INPUT_TYPE.defaultValue
                )
            )
            add(
                SingleChoiceSetting(
                    editingSettings,
                    null,
                    R.string.sound_output_mode,
                    0,
                    R.array.soundOutputModes,
                    R.array.soundOutputModeValues,
                    getValue = { SystemSaveGame.getSoundOutputMode() },
                    setValue = { SystemSaveGame.setSoundOutputMode(it) }
                )
            )
        }
    }

    private fun addDebugSettings(sl: ArrayList<SettingsItem>) {
        settingsActivity.setToolbarTitle(settingsActivity.getString(R.string.preferences_debug))
        sl.apply {
            add(HeaderSetting(R.string.debug_warning))
            add(
                SliderSetting(
                    editingSettings,
                    IntSetting.CPU_CLOCK_SPEED,
                    R.string.cpu_clock_speed,
                    0,
                    25,
                    400,
                    "%",
                    IntSetting.CPU_CLOCK_SPEED.key,
                    IntSetting.CPU_CLOCK_SPEED.defaultValue.toFloat()
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.CPU_JIT,
                    R.string.cpu_jit,
                    R.string.cpu_jit_description,
                    BooleanSetting.CPU_JIT.key,
                    BooleanSetting.CPU_JIT.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.USE_FASTINTERP,
                    R.string.use_fastinterp,
                    R.string.use_fastinterp_description,
                    BooleanSetting.USE_FASTINTERP.key,
                    BooleanSetting.USE_FASTINTERP.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.HW_SHADER,
                    R.string.hw_shaders,
                    R.string.hw_shaders_description,
                    BooleanSetting.HW_SHADER.key,
                    BooleanSetting.HW_SHADER.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.SHADER_JIT,
                    R.string.shader_jit,
                    R.string.shader_jit_description,
                    BooleanSetting.SHADER_JIT.key,
                    BooleanSetting.SHADER_JIT.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.VSYNC,
                    R.string.vsync,
                    R.string.vsync_description,
                    BooleanSetting.VSYNC.key,
                    BooleanSetting.VSYNC.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.SIMULATE_3DS_GPU_TIMINGS,
                    R.string.simulate_3ds_gpu_timings,
                    R.string.simulate_3ds_gpu_timings_description,
                    BooleanSetting.SIMULATE_3DS_GPU_TIMINGS.key,
                    BooleanSetting.SIMULATE_3DS_GPU_TIMINGS.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.DEBUG_RENDERER,
                    R.string.renderer_debug,
                    R.string.renderer_debug_description,
                    BooleanSetting.DEBUG_RENDERER.key,
                    BooleanSetting.DEBUG_RENDERER.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.INSTANT_DEBUG_LOG,
                    R.string.instant_debug_log,
                    R.string.instant_debug_log_description,
                    BooleanSetting.INSTANT_DEBUG_LOG.key,
                    BooleanSetting.INSTANT_DEBUG_LOG.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.ENABLE_RPC_SERVER,
                    R.string.enable_rpc_server,
                    R.string.enable_rpc_server_desc,
                    BooleanSetting.ENABLE_RPC_SERVER.key,
                    BooleanSetting.ENABLE_RPC_SERVER.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.TOGGLE_UNIQUE_DATA_CONSOLE_TYPE,
                    R.string.toggle_unique_data_console_type,
                    R.string.toggle_unique_data_console_type_desc,
                    BooleanSetting.TOGGLE_UNIQUE_DATA_CONSOLE_TYPE.key,
                    BooleanSetting.TOGGLE_UNIQUE_DATA_CONSOLE_TYPE.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.DELAY_START_LLE_MODULES,
                    R.string.delay_start_lle_modules,
                    R.string.delay_start_lle_modules_description,
                    BooleanSetting.DELAY_START_LLE_MODULES.key,
                    BooleanSetting.DELAY_START_LLE_MODULES.defaultValue
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    BooleanSetting.DETERMINISTIC_ASYNC_OPERATIONS,
                    R.string.deterministic_async_operations,
                    R.string.deterministic_async_operations_description,
                    BooleanSetting.DETERMINISTIC_ASYNC_OPERATIONS.key,
                    BooleanSetting.DETERMINISTIC_ASYNC_OPERATIONS.defaultValue
                )
            )
        }
    }

    private fun addThemeSettings(sl: ArrayList<SettingsItem>) {
        settingsActivity.setToolbarTitle(settingsActivity.getString(R.string.preferences_theme))
        sl.apply {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                add(
                    SwitchSetting(
                        editingSettings,
                        null,
                        R.string.material_you,
                        R.string.material_you_description,
                        getValue = {
                            preferences.getBoolean(CitraSettings.PREF_MATERIAL_YOU, false)
                        },
                        setValue = {
                            preferences.edit()
                                .putBoolean(CitraSettings.PREF_MATERIAL_YOU, it)
                                .apply()
                            settingsActivity.recreate()
                        }
                    )
                )
            }
            add(
                SingleChoiceSetting(
                    editingSettings,
                    null,
                    R.string.static_theme_color,
                    R.string.static_theme_color_description,
                    R.array.staticThemeNames,
                    R.array.staticThemeValues,
                    getValue = {
                        preferences.getInt(CitraSettings.PREF_STATIC_THEME_COLOR, 0)
                    },
                    setValue = {
                        preferences.edit()
                            .putInt(CitraSettings.PREF_STATIC_THEME_COLOR, it)
                            .apply()
                        settingsActivity.recreate()
                    }
                )
            )
            add(
                SingleChoiceSetting(
                    editingSettings,
                    null,
                    R.string.change_theme_mode,
                    0,
                    R.array.themeModeEntries,
                    R.array.themeModeValues,
                    getValue = {
                        preferences.getInt(CitraSettings.PREF_THEME_MODE, -1)
                    },
                    setValue = {
                        preferences.edit()
                            .putInt(CitraSettings.PREF_THEME_MODE, it)
                            .apply()
                        ThemeUtil.setThemeMode(settingsActivity)
                        settingsActivity.recreate()
                    }
                )
            )
            add(
                SwitchSetting(
                    editingSettings,
                    null,
                    R.string.use_black_backgrounds,
                    R.string.use_black_backgrounds_description,
                    getValue = {
                        preferences.getBoolean(CitraSettings.PREF_BLACK_BACKGROUNDS, false)
                    },
                    setValue = {
                        preferences.edit()
                            .putBoolean(CitraSettings.PREF_BLACK_BACKGROUNDS, it)
                            .apply()
                        settingsActivity.recreate()
                    }
                )
            )
        }
    }
}
