// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.fragments

import android.app.Dialog
import android.os.Bundle
import android.view.LayoutInflater
import android.widget.ArrayAdapter
import android.widget.EditText
import android.widget.Spinner
import android.widget.Toast
import androidx.appcompat.widget.SwitchCompat
import androidx.fragment.app.DialogFragment
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.slider.Slider
import org.citra.citra_emu.R
import org.citra.citra_emu.display.DisplayProfile
import org.citra.citra_emu.display.DisplayProfileManager
import org.citra.citra_emu.display.ScreenLayout
import org.citra.citra_emu.display.StereoMode

/**
 * Dialog fragment for managing display profiles that automatically apply
 * 3D settings when specific external displays are connected.
 */
class DisplayProfilesDialogFragment : DialogFragment() {

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        val profiles = DisplayProfileManager.getProfiles()

        if (profiles.isEmpty()) {
            return showEmptyStateDialog()
        }

        return showProfileListDialog(profiles)
    }

    private fun showEmptyStateDialog(): Dialog {
        return MaterialAlertDialogBuilder(requireContext())
            .setTitle(R.string.display_profiles)
            .setMessage(R.string.display_profiles_empty)
            .setPositiveButton(R.string.display_profiles_add) { _, _ ->
                showAddEditProfileDialog(null)
            }
            .setNegativeButton(android.R.string.cancel, null)
            .create()
    }

    private fun showProfileListDialog(profiles: List<DisplayProfile>): Dialog {
        val profileNames = profiles.map { profile ->
            val status = if (profile.enabled) "" else " (disabled)"
            "${profile.profileName}$status - ${profile.matchPattern}"
        }.toTypedArray()

        return MaterialAlertDialogBuilder(requireContext())
            .setTitle(R.string.display_profiles)
            .setItems(profileNames) { _, which ->
                showProfileOptionsDialog(profiles[which])
            }
            .setPositiveButton(R.string.display_profiles_add) { _, _ ->
                showAddEditProfileDialog(null)
            }
            .setNegativeButton(android.R.string.cancel, null)
            .create()
    }

    private fun showProfileOptionsDialog(profile: DisplayProfile) {
        val ctx = requireContext()
        val fragmentMgr = parentFragmentManager
        val options = arrayOf(
            getString(R.string.display_profiles_edit),
            getString(R.string.display_profile_test),
            getString(R.string.display_profiles_delete)
        )

        MaterialAlertDialogBuilder(ctx)
            .setTitle(profile.profileName)
            .setItems(options) { _, which ->
                when (which) {
                    0 -> showAddEditProfileDialog(profile, ctx, fragmentMgr)
                    1 -> testProfile(profile, ctx)
                    2 -> confirmDeleteProfile(profile, ctx, fragmentMgr)
                }
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun showAddEditProfileDialog(
        existingProfile: DisplayProfile?,
        ctx: android.content.Context = requireContext(),
        fragmentMgr: androidx.fragment.app.FragmentManager = parentFragmentManager
    ) {
        val isEdit = existingProfile != null
        val inflater = LayoutInflater.from(ctx)
        val view = inflater.inflate(R.layout.dialog_display_profile, null)

        val nameEdit = view.findViewById<EditText>(R.id.edit_profile_name)
        val patternEdit = view.findViewById<EditText>(R.id.edit_match_pattern)
        val regexSwitch = view.findViewById<SwitchCompat>(R.id.switch_use_regex)
        val stereoSpinner = view.findViewById<Spinner>(R.id.spinner_stereo_mode)
        val depthSlider = view.findViewById<Slider>(R.id.slider_depth)
        val swapEyesSwitch = view.findViewById<SwitchCompat>(R.id.switch_swap_eyes)
        val layoutSpinner = view.findViewById<Spinner>(R.id.spinner_layout)
        val enabledSwitch = view.findViewById<SwitchCompat>(R.id.switch_enabled)

        // Setup spinners
        val stereoModes = arrayOf("Off", "Side by Side", "Side by Side (Full)", "Anaglyph", "Interlaced", "Reverse Interlaced", "Cardboard VR")
        stereoSpinner.adapter = ArrayAdapter(ctx, android.R.layout.simple_spinner_dropdown_item, stereoModes)

        val layouts = arrayOf("Original", "Single Screen", "Large Screen", "Side by Side", "Hybrid", "Custom")
        layoutSpinner.adapter = ArrayAdapter(ctx, android.R.layout.simple_spinner_dropdown_item, layouts)

        // Populate with existing values if editing
        if (existingProfile != null) {
            nameEdit.setText(existingProfile.profileName)
            patternEdit.setText(existingProfile.matchPattern)
            regexSwitch.isChecked = existingProfile.useRegex
            stereoSpinner.setSelection(existingProfile.stereoMode)
            depthSlider.value = existingProfile.factor3d.coerceIn(0, 255).toFloat()
            swapEyesSwitch.isChecked = existingProfile.swapEyes
            layoutSpinner.setSelection(existingProfile.layoutOption.coerceIn(0, 5))
            enabledSwitch.isChecked = existingProfile.enabled
        } else {
            // Defaults for new profile
            stereoSpinner.setSelection(StereoMode.SIDE_BY_SIDE.int)
            depthSlider.value = 100f
            enabledSwitch.isChecked = true
        }

        MaterialAlertDialogBuilder(ctx)
            .setTitle(if (isEdit) R.string.display_profiles_edit else R.string.display_profiles_add)
            .setView(view)
            .setPositiveButton(R.string.display_profile_save) { _, _ ->
                val name = nameEdit.text.toString().trim()
                val pattern = patternEdit.text.toString().trim()

                if (name.isEmpty()) {
                    Toast.makeText(ctx, R.string.display_profile_name_empty, Toast.LENGTH_SHORT).show()
                    return@setPositiveButton
                }
                if (pattern.isEmpty()) {
                    Toast.makeText(ctx, R.string.display_profile_pattern_empty, Toast.LENGTH_SHORT).show()
                    return@setPositiveButton
                }

                val newProfile = DisplayProfile(
                    profileName = name,
                    matchPattern = pattern,
                    useRegex = regexSwitch.isChecked,
                    stereoMode = stereoSpinner.selectedItemPosition,
                    factor3d = depthSlider.value.toInt(),
                    swapEyes = swapEyesSwitch.isChecked,
                    layoutOption = layoutSpinner.selectedItemPosition,
                    enabled = enabledSwitch.isChecked
                )

                if (isEdit) {
                    DisplayProfileManager.updateProfile(existingProfile!!.profileName, newProfile)
                } else {
                    DisplayProfileManager.addProfile(newProfile)
                }

                // Refresh the dialog
                dismiss()
                DisplayProfilesDialogFragment().show(fragmentMgr, TAG)
            }
            .setNegativeButton(R.string.display_profile_cancel, null)
            .show()
    }

    private fun testProfile(profile: DisplayProfile, ctx: android.content.Context = requireContext()) {
        DisplayProfileManager.saveSettingsSnapshot()
        DisplayProfileManager.applyProfile(profile)
        Toast.makeText(ctx, R.string.display_profile_test_applied, Toast.LENGTH_SHORT).show()

        MaterialAlertDialogBuilder(ctx)
            .setTitle(R.string.display_profile_test)
            .setMessage(profile.profileName)
            .setPositiveButton(R.string.display_profile_test_revert) { _, _ ->
                DisplayProfileManager.restoreSettingsSnapshot()
                Toast.makeText(ctx, R.string.display_profile_test_reverted, Toast.LENGTH_SHORT).show()
            }
            .setNegativeButton(R.string.display_profile_test_keep, null)
            .show()
    }

    private fun confirmDeleteProfile(
        profile: DisplayProfile,
        ctx: android.content.Context = requireContext(),
        fragmentMgr: androidx.fragment.app.FragmentManager = parentFragmentManager
    ) {
        MaterialAlertDialogBuilder(ctx)
            .setTitle(R.string.display_profiles_delete)
            .setMessage(ctx.getString(R.string.display_profiles_delete_confirm))
            .setPositiveButton(android.R.string.ok) { _, _ ->
                DisplayProfileManager.removeProfile(profile.profileName)
                // Refresh the dialog
                dismiss()
                DisplayProfilesDialogFragment().show(fragmentMgr, TAG)
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    companion object {
        const val TAG = "DisplayProfilesDialogFragment"
    }
}
