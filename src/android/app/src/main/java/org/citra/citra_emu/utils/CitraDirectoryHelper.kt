// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import android.content.Intent
import android.net.Uri
import androidx.fragment.app.FragmentActivity
import androidx.lifecycle.ViewModelProvider
import org.citra.citra_emu.fragments.CitraDirectoryDialogFragment
import org.citra.citra_emu.fragments.CopyDirProgressDialog
import org.citra.citra_emu.model.SetupCallback
import org.citra.citra_emu.viewmodel.HomeViewModel

/**
 * Citra directory initialization ui flow controller.
 */
class CitraDirectoryHelper(private val fragmentActivity: FragmentActivity, private val lostPermission: Boolean) {
    fun showCitraDirectoryDialog(result: Uri, callback: SetupCallback? = null, buttonState: () -> Unit) {
        val citraDirectoryDialog = CitraDirectoryDialogFragment.newInstance(
            fragmentActivity,
            result.toString(),
            CitraDirectoryDialogFragment.Listener { moveData: Boolean, path: Uri ->
                val previous = PermissionsHandler.citraDirectory
                // Do noting if user select the previous path.
                if (path == previous && !lostPermission) {
                    return@Listener
                }

                val takeFlags = Intent.FLAG_GRANT_WRITE_URI_PERMISSION or
                        Intent.FLAG_GRANT_READ_URI_PERMISSION
                fragmentActivity.contentResolver.takePersistableUriPermission(
                    path,
                    takeFlags
                )
                if (!moveData || previous.toString().isEmpty()) {
                    initializeCitraDirectory(path)
                    buttonState()
                    val viewModel = ViewModelProvider(fragmentActivity)[HomeViewModel::class.java]
                    viewModel.setUserDir(fragmentActivity, path.path!!)
                    viewModel.setPickingUserDir(false)
                    return@Listener
                }

                // If user check move data, show copy progress dialog.
                CopyDirProgressDialog.newInstance(fragmentActivity, previous, path, callback)
                    ?.show(fragmentActivity.supportFragmentManager, CopyDirProgressDialog.TAG)
            })
        citraDirectoryDialog.show(
            fragmentActivity.supportFragmentManager,
            CitraDirectoryDialogFragment.TAG
        )
    }

    companion object {
        fun initializeCitraDirectory(path: Uri) {
            PermissionsHandler.setCitraDirectory(path.toString())
            DirectoryInitialization.resetCitraDirectoryState()
            DirectoryInitialization.start()
        }
    }
}
