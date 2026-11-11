// Copyright 2025-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

package org.citra.citra_emu.fragments

import android.Manifest
import android.app.Dialog
import android.content.DialogInterface
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.RequiresApi
import androidx.core.content.ContextCompat
import androidx.fragment.app.DialogFragment
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.citra.citra_emu.R
import org.citra.citra_emu.ui.main.MainActivity
import org.citra.citra_emu.utils.BuildUtil

@RequiresApi(Build.VERSION_CODES.CINNAMON_BUN)
class GrantMissingLanPermissionFragment : DialogFragment() {
    private lateinit var mainActivity: MainActivity

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        BuildUtil.assertNotGooglePlay()
        mainActivity = requireActivity() as MainActivity

        isCancelable = false

        return MaterialAlertDialogBuilder(requireContext())
            .setTitle(R.string.missing_permissions)
            .setMessage(R.string.lan_permission_lost)
            .setPositiveButton(android.R.string.ok) { _: DialogInterface, _: Int ->
                permissionLauncher.launch(Manifest.permission.ACCESS_LOCAL_NETWORK)
            }
            .show()
    }

    private fun openAndroidAppSettings() {
        if (ContextCompat.checkSelfPermission(
                mainActivity,
                Manifest.permission.ACCESS_LOCAL_NETWORK
            ) != PackageManager.PERMISSION_GRANTED
        ) {
            val intent =
                Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS)
            val uri = Uri.fromParts("package", mainActivity.packageName, null)
            intent.data = uri
            startActivity(intent)
        }
    }
    private val permissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { isGranted ->
            if (isGranted) {
                return@registerForActivityResult
            }

            Toast.makeText(mainActivity, R.string.lan_permission_manual, Toast.LENGTH_LONG)
                .show()
            openAndroidAppSettings()
        }

    companion object {
        const val TAG = "GrantMissingLanPermissionFragment"

        fun newInstance(): GrantMissingLanPermissionFragment = GrantMissingLanPermissionFragment()
    }
}
