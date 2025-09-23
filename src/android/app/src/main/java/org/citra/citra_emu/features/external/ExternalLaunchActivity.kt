// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.external

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.R
import org.citra.citra_emu.activities.EmulationActivity
import org.citra.citra_emu.features.settings.utils.SettingsFile
import org.citra.citra_emu.model.Game
import org.citra.citra_emu.utils.DirectoryInitialization
import org.citra.citra_emu.utils.GameHelper
import org.citra.citra_emu.utils.Log

class ExternalLaunchActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Ensure user directory is initialized
        DirectoryInitialization.start()

        val titleIdStr = intent.getStringExtra(EXTRA_TITLE_ID)
        val iniText = intent.getStringExtra(EXTRA_CONFIG_INI) ?: ""
        if (titleIdStr.isNullOrEmpty()) {
            finishWithResult(success = false)
            return
        }

        val titleId = parseTitleId(titleIdStr)
        if (titleId == null) {
            finishWithResult(success = false)
            return
        }

        // If existing per-game file exists, confirm overwrite
        val hasExisting = SettingsFile.customExists(String.format("%016X", titleId))
        if (hasExisting) {
            MaterialAlertDialogBuilder(this)
                .setTitle(R.string.application_settings)
                .setMessage(R.string.overwrite_custom_settings_prompt)
                .setPositiveButton(android.R.string.ok) { _, _ ->
                    proceedWithDriverCheckAndLaunch(titleId, iniText)
                }
                .setNegativeButton(android.R.string.cancel) { _, _ -> finishWithResult(success = false) }
                .setOnCancelListener { finishWithResult(success = false) }
                .show()
        } else {
            proceedWithDriverCheckAndLaunch(titleId, iniText)
        }
    }

    private fun proceedWithDriverCheckAndLaunch(titleId: Long, iniText: String) {
        val idHex = String.format("%016X", titleId)
        val requestedBackend = extractGraphicsApi(iniText)

        if (requestedBackend == GRAPHICS_BACKEND_VULKAN) {
            MaterialAlertDialogBuilder(this)
                .setTitle(R.string.custom_launch_backend_warning_title)
                .setMessage(R.string.custom_launch_backend_warning_message)
                .setPositiveButton(R.string.custom_launch_backend_option_use_vulkan) { _, _ ->
                    writeConfigAndLaunch(titleId, iniText, idHex)
                }
                .setNegativeButton(R.string.custom_launch_backend_option_use_opengl) { _, _ ->
                    val adjusted = replaceGraphicsApi(iniText, GRAPHICS_BACKEND_OPENGL)
                    Log.info("[ExternalLaunch] Falling back to OpenGL for external launch")
                    writeConfigAndLaunch(titleId, adjusted, idHex)
                }
                .setOnCancelListener { finishWithResult(success = false) }
                .show()
            return
        }

        writeConfigAndLaunch(titleId, iniText, idHex)
    }

    private fun writeConfigAndLaunch(titleId: Long, iniText: String, idHex: String) {
        SettingsFile.saveCustomFileRaw(idHex, iniText)

        val game = findGameByTitleId(titleId)
        if (game == null) {
            MaterialAlertDialogBuilder(this)
                .setTitle(R.string.custom_launch_missing_game_title)
                .setMessage(getString(R.string.custom_launch_missing_game_message, idHex))
                .setPositiveButton(android.R.string.ok) { _, _ -> finishWithResult(success = false) }
                .setOnCancelListener { finishWithResult(success = false) }
                .show()
            return
        }

        val launch = Intent(this, EmulationActivity::class.java)
        launch.putExtra("game", game)
        startActivity(launch)
        finishWithResult(success = true)
    }

    private fun findGameByTitleId(titleId: Long): Game? {
        // Try cached games first
        val prefs = androidx.preference.PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)
        val serialized = prefs.getStringSet(GameHelper.KEY_GAMES, emptySet()) ?: emptySet()
        if (serialized.isNotEmpty()) {
            val games = serialized.mapNotNull {
                try { kotlinx.serialization.json.Json.decodeFromString(org.citra.citra_emu.model.Game.serializer(), it) } catch (_: Exception) { null }
            }
            games.firstOrNull { it.titleId == titleId }?.let { return it }
        }
        // Fallback: rescan library
        return GameHelper.getGames().firstOrNull { it.titleId == titleId }
    }

    companion object {
        const val EXTRA_TITLE_ID = "title_id"
        const val EXTRA_CONFIG_INI = "config_ini"
        private const val GRAPHICS_BACKEND_OPENGL = 1
        private const val GRAPHICS_BACKEND_VULKAN = 2
    }

    private fun parseTitleId(raw: String?): Long? {
        if (raw.isNullOrBlank()) return null
        val trimmed = raw.trim()
        val withoutPrefix = if (trimmed.startsWith("0x", true)) trimmed.substring(2) else trimmed

        // Prefer hexadecimal interpretation – Title IDs are traditionally provided in hex.
        val hexValue = withoutPrefix.toLongOrNull(16)
        if (hexValue != null) return hexValue

        return withoutPrefix.toLongOrNull()
    }

    private fun extractGraphicsApi(config: String): Int? {
        val regex = Regex(
            "^\\s*graphics_api\\s*=\\s*(\\d+)\\s*$",
            setOf(RegexOption.IGNORE_CASE, RegexOption.MULTILINE)
        )
        val match = regex.find(config) ?: return null
        return match.groupValues.getOrNull(1)?.trim()?.toIntOrNull()
    }

    private fun replaceGraphicsApi(config: String, backend: Int): String {
        val regex = Regex(
            "^\\s*graphics_api\\s*=\\s*(\\d+)\\s*$",
            setOf(RegexOption.IGNORE_CASE, RegexOption.MULTILINE)
        )
        return regex.replace(config) { "graphics_api = $backend" }
    }

    private fun finishWithResult(success: Boolean) {
        setResult(if (success) RESULT_OK else RESULT_CANCELED)
        finish()
    }
}
