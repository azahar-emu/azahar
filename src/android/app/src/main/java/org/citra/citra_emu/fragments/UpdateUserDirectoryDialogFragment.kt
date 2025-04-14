// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.fragments

import android.app.Dialog
import android.content.DialogInterface
import android.content.SharedPreferences
import android.net.Uri
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.widget.LinearLayout
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.TextView
import androidx.fragment.app.DialogFragment
import androidx.fragment.app.FragmentActivity
import androidx.lifecycle.ViewModelProvider
import androidx.preference.PreferenceManager
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.R
import org.citra.citra_emu.ui.main.MainActivity
import org.citra.citra_emu.utils.CitraDirectoryUtils
import org.citra.citra_emu.utils.DirectoryInitialization
import org.citra.citra_emu.utils.PermissionsHandler
import org.citra.citra_emu.viewmodel.HomeViewModel

class UpdateUserDirectoryDialogFragment : DialogFragment() {
    private lateinit var mainActivity: MainActivity

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        mainActivity = requireActivity() as MainActivity

        isCancelable = false
        val preferences: SharedPreferences =
            PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)
        val ld = preferences.getString("LIME3DS_DIRECTORY","")
        val cd = preferences.getString("CITRA_DIRECTORY","")
        val dialogView = LayoutInflater.from(requireContext())
            .inflate(R.layout.dialog_select_which_directory, null)

        val radioGroup = dialogView.findViewById<RadioGroup>(R.id.radioGroup)

        val choices = listOf(
            getString(R.string.keep_current_azahar_directory) to Uri.parse(cd).path,
            getString(R.string.use_prior_lime3ds_directory) to Uri.parse(ld).path
        )
        var selected = -1 // 0 = current, 1 = prior, -1 = no selection

        choices.forEachIndexed { index, (label, subtext) ->
            val container = LinearLayout(requireContext()).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(0, 16, 0, 16)
            }

            val radioButton = RadioButton(requireContext()).apply {
                text = label
                id = View.generateViewId()
            }

            val subTextView = TextView(requireContext()).apply {
                text = subtext
                setPadding(64, 4, 0, 0) // indent for visual hierarchy
                setTextAppearance(android.R.style.TextAppearance_Small)
            }

            container.addView(radioButton)
            container.addView(subTextView)
            radioGroup.addView(container)

            // RadioGroup expects RadioButtons directly, so we need to manage selection ourselves
            radioButton.setOnClickListener {
                selected = index
                // Manually uncheck others
                for (i in 0 until radioGroup.childCount) {
                    val child = radioGroup.getChildAt(i) as LinearLayout
                    val rb = child.getChildAt(0) as RadioButton
                    rb.isChecked = i == index
                }
            }
        }

        return MaterialAlertDialogBuilder(requireContext())
            .setTitle(R.string.select_citra_user_folder)
            .setView(dialogView)
            .setPositiveButton(android.R.string.ok) { _: DialogInterface, _: Int ->
                if (selected == 1) {
                    PermissionsHandler.setCitraDirectory(ld)
                }
                if (selected >= 0) {
                    CitraDirectoryUtils.removeLimeDirectoryPreference()
                    DirectoryInitialization.resetCitraDirectoryState()
                    DirectoryInitialization.start()
                }

                ViewModelProvider(mainActivity)[HomeViewModel::class.java].setPickingUserDir(false)
                ViewModelProvider(mainActivity)[HomeViewModel::class.java].setUserDir(this.requireActivity(),PermissionsHandler.citraDirectory.path!!)
            }
            .show()
    }

    companion object {
        const val TAG = "UpdateUserDirectoryDialogFragment"

        fun newInstance(activity: FragmentActivity): UpdateUserDirectoryDialogFragment {
            ViewModelProvider(activity)[HomeViewModel::class.java].setPickingUserDir(true)
            return UpdateUserDirectoryDialogFragment()
        }
    }
}
