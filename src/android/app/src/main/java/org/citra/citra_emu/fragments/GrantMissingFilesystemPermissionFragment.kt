// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.fragments

import android.Manifest
import android.app.Dialog
import android.content.DialogInterface
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.RequiresApi
import androidx.fragment.app.DialogFragment
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.citra.citra_emu.R
import org.citra.citra_emu.ui.main.MainActivity
class GrantMissingFilesystemPermissionFragment : DialogFragment() {
    private lateinit var mainActivity: MainActivity

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        mainActivity = requireActivity() as MainActivity

        isCancelable = false

        val requestPermissionFunction =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                {
                    manageExternalStoragePermissionLauncher.launch(
                        Intent(
                            Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                            Uri.fromParts("package", mainActivity.packageName, null)
                        )
                    )
                }
            } else {
                { permissionLauncher.launch(Manifest.permission.WRITE_EXTERNAL_STORAGE) }
            }



        return MaterialAlertDialogBuilder(requireContext())
            .setTitle(R.string.filesystem_permission_warning)
            .setMessage(R.string.filesystem_permission_lost)
            .setPositiveButton(android.R.string.ok) { _: DialogInterface, _: Int ->
                requestPermissionFunction()
            }
            .show()
    }

    @RequiresApi(Build.VERSION_CODES.R)
    private val manageExternalStoragePermissionLauncher =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) {
            if (Environment.isExternalStorageManager()) {
                return@registerForActivityResult
            }
        }

    private val permissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { isGranted ->
            if (isGranted) {
                return@registerForActivityResult
            }
        }

    companion object {
        const val TAG = "GrantMissingFilesystemPermissionFragment"

        fun newInstance(): GrantMissingFilesystemPermissionFragment {
            return GrantMissingFilesystemPermissionFragment()
        }
    }
}
