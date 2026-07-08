// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.fragments

import android.app.Dialog
import android.content.DialogInterface
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import androidx.fragment.app.DialogFragment
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.citra.citra_emu.R
import org.citra.citra_emu.ui.main.MainActivity
import org.citra.citra_emu.utils.BuildUtil

class UpdateAvailableNotificationFragment(checkForPrereleaseUpdatesOverride: Boolean) :
    DialogFragment() {
    private lateinit var mainActivity: MainActivity

    private val checkForPrereleaseUpdates = checkForPrereleaseUpdatesOverride

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        BuildUtil.assertNotGooglePlay()
        mainActivity = requireActivity() as MainActivity

        isCancelable = false

        return MaterialAlertDialogBuilder(requireContext())
            .setTitle(R.string.update_available)
            .setMessage(R.string.update_available_description)
            .setPositiveButton(android.R.string.ok) { _: DialogInterface, _: Int ->
                val updateLink: String = if (checkForPrereleaseUpdates) {
                    getString(R.string.prerelease_channel_update_link)
                } else {
                    getString(R.string.stable_channel_update_link)
                }
                val intent = Intent(
                    Intent.ACTION_VIEW,
                    Uri.parse(updateLink)
                )
                startActivity(intent)
            }
            .show()
    }

    companion object {
        const val TAG = "UpdateAvailableNotificationFragment"

        fun newInstance(checkForPrereleaseUpdates: Boolean): UpdateAvailableNotificationFragment {
            BuildUtil.assertNotGooglePlay()
            return UpdateAvailableNotificationFragment(checkForPrereleaseUpdates)
        }
    }
}
