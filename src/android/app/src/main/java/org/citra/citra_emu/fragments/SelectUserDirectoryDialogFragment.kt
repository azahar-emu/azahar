// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.fragments

import android.app.Dialog
import android.content.DialogInterface
import android.os.Bundle
import androidx.fragment.app.DialogFragment
import androidx.fragment.app.FragmentActivity
import androidx.lifecycle.ViewModelProvider
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.citra.citra_emu.R
import org.citra.citra_emu.ui.main.MainActivity
import org.citra.citra_emu.viewmodel.HomeViewModel

class SelectUserDirectoryDialogFragment : DialogFragment() {
    private lateinit var mainActivity: MainActivity

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        mainActivity = requireActivity() as MainActivity

        isCancelable = false

        return MaterialAlertDialogBuilder(requireContext())
            .setTitle(R.string.select_citra_user_folder)
            .setMessage(R.string.selecting_user_directory_without_write_permissions)
            .setPositiveButton(android.R.string.ok) { _: DialogInterface, _: Int ->
                mainActivity?.openCitraDirectoryLostPermission?.launch(null)
            }
            .show()
    }

    companion object {
        const val TAG = "SelectUserDirectoryDialogFragment"

        fun newInstance(activity: FragmentActivity): SelectUserDirectoryDialogFragment {
            ViewModelProvider(activity)[HomeViewModel::class.java].setPickingUserDir(true)
            return SelectUserDirectoryDialogFragment()
        }
    }
}
