// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.display

import kotlinx.serialization.Serializable

/**
 * Represents a display profile that can be automatically applied when a specific
 * external display is connected (e.g., Xreal glasses, external monitor).
 */
@Serializable
data class DisplayProfile(
    /** User-friendly name for this profile */
    val profileName: String,

    /** Pattern to match against the display name (substring match by default) */
    val matchPattern: String,

    /** If true, matchPattern is treated as a regex pattern */
    val useRegex: Boolean = false,

    /** Stereo rendering mode (matches StereoMode enum values) */
    val stereoMode: Int = StereoMode.OFF.int,

    /** 3D depth factor (0-255) */
    val factor3d: Int = 0,

    /** Whether to swap left/right eyes */
    val swapEyes: Boolean = false,

    /** Screen layout option (matches ScreenLayout enum values) */
    val layoutOption: Int = ScreenLayout.ORIGINAL.int,

    /** Which display to render 3D to (matches StereoWhichDisplay enum values) */
    val stereoWhichDisplay: Int = StereoWhichDisplay.PRIMARY_ONLY.int,

    /** Whether this profile is enabled */
    val enabled: Boolean = true
) {
    /**
     * Check if this profile matches the given display name.
     */
    fun matchesDisplay(displayName: String): Boolean {
        if (!enabled) return false

        return if (useRegex) {
            try {
                Regex(matchPattern, RegexOption.IGNORE_CASE).containsMatchIn(displayName)
            } catch (e: Exception) {
                // Invalid regex, fall back to substring match
                displayName.contains(matchPattern, ignoreCase = true)
            }
        } else {
            displayName.contains(matchPattern, ignoreCase = true)
        }
    }

    companion object {
        /**
         * Create a default profile for Xreal glasses
         */
        fun createXrealDefault(): DisplayProfile {
            return DisplayProfile(
                profileName = "Xreal Glasses",
                matchPattern = "XREAL",
                useRegex = false,
                stereoMode = StereoMode.SIDE_BY_SIDE.int,
                factor3d = 100,
                swapEyes = false,
                layoutOption = ScreenLayout.ORIGINAL.int,
                enabled = true
            )
        }
    }
}

/**
 * Snapshot of current display settings that can be restored when
 * the external display is disconnected.
 */
@Serializable
data class SettingsSnapshot(
    val stereoMode: Int,
    val factor3d: Int,
    val swapEyes: Boolean,
    val layoutOption: Int,
    val stereoWhichDisplay: Int = StereoWhichDisplay.NONE.int
)
