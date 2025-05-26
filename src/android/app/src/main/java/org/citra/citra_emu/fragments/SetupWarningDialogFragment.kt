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

class SetupWarningDialogFragment : DialogFragment() {
    private var titleIds: IntArray = intArrayOf()
    private var descriptionIds: IntArray = intArrayOf()
    private var helpLinkIds: IntArray = intArrayOf()
    private var page: Int = 0

    private lateinit var setupFragment: SetupFragment

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        titleIds = requireArguments().getIntArray(TITLES) ?: intArrayOf()
        descriptionIds = requireArguments().getIntArray(DESCRIPTIONS) ?: intArrayOf()
        helpLinkIds = requireArguments().getIntArray(HELP_LINKS) ?: intArrayOf()
        page = requireArguments().getInt(PAGE)

        setupFragment = requireParentFragment() as SetupFragment
    }

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        val builder = MaterialAlertDialogBuilder(requireContext())
            .setPositiveButton(R.string.warning_skip) { _: DialogInterface?, _: Int ->
                setupFragment.pageForward()
                setupFragment.setPageWarned(page)
            }
            .setNegativeButton(R.string.warning_cancel, null)

        // Message builder to build multiple strings into one
        val messageBuilder = StringBuilder()
        for (i in titleIds.indices) {
            if (titleIds[i] != 0) {
                messageBuilder.append(getString(titleIds[i])).append("\n\n")
            }
            if (descriptionIds[i] != 0) {
                messageBuilder.append(getString(descriptionIds[i])).append("\n\n")
            }
        }

        builder.setTitle("Warning")
        builder.setMessage(messageBuilder.toString().trim())

        if (helpLinkIds.any { it != 0 }) {
            builder.setNeutralButton(R.string.warning_help) { _: DialogInterface?, _: Int ->
                val helpLinkId = helpLinkIds.first { it != 0 }
                val helpLink = resources.getString(helpLinkId)
                val intent = Intent(Intent.ACTION_VIEW, Uri.parse(helpLink))
                startActivity(intent)
            }
        }

        return builder.show()
    }

    companion object {
        const val TAG = "SetupWarningDialogFragment"

        private const val TITLES = "Titles"
        private const val DESCRIPTIONS = "Descriptions"
        private const val HELP_LINKS = "HelpLinks"
        private const val PAGE = "Page"

        fun newInstance(
            titleIds: IntArray,
            descriptionIds: IntArray,
            helpLinkIds: IntArray,
            page: Int
        ): SetupWarningDialogFragment {
            val dialog = SetupWarningDialogFragment()
            val bundle = Bundle()
            bundle.apply {
                putIntArray(TITLES, titleIds)
                putIntArray(DESCRIPTIONS, descriptionIds)
                putIntArray(HELP_LINKS, helpLinkIds)
                putInt(PAGE, page)
            }
            dialog.arguments = bundle
            return dialog
        }
    }
}
