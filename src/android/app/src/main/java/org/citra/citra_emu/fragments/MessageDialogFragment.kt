// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.fragments

import android.app.Dialog
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import androidx.fragment.app.DialogFragment
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.citra.citra_emu.R

class MessageDialogFragment : DialogFragment() {
    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        val titleId = requireArguments().getInt(TITLE_ID)
        val descriptionId = requireArguments().getInt(DESCRIPTION_ID)
        val descriptionString = requireArguments().getString(DESCRIPTION_STRING) ?: ""
        val secondaryDescriptionString = requireArguments().getString(DESCRIPTION_STRING_2) ?: ""
        val helpLinkId = requireArguments().getInt(HELP_LINK)

        val dialog = MaterialAlertDialogBuilder(requireContext())
            .setPositiveButton(R.string.close, null)
            .setTitle(titleId)

        if (descriptionString.isNotEmpty()) {
            dialog.setMessage(if (secondaryDescriptionString.isNotEmpty()) {
                "$descriptionString\n\n$secondaryDescriptionString"
            } else {
                descriptionString
            })
        } else if (descriptionId != 0) {
            dialog.setMessage(descriptionId)
        }

        if (helpLinkId != 0) {
            dialog.setNeutralButton(R.string.learn_more) { _, _ ->
                openLink(getString(helpLinkId))
            }
        }

        return dialog.show()
    }

    private fun openLink(link: String) {
        val intent = Intent(Intent.ACTION_VIEW, Uri.parse(link))
        startActivity(intent)
    }

    companion object {
        const val TAG = "MessageDialogFragment"

        private const val TITLE_ID = "Title"
        private const val DESCRIPTION_ID = "Description"
        private const val DESCRIPTION_STRING = "Description_string"
        private const val DESCRIPTION_STRING_2 = "Description_string_2"
        private const val HELP_LINK = "Link"

        fun newInstance(
            titleId: Int,
            descriptionId: Int,
            helpLinkId: Int = 0
        ): MessageDialogFragment {
            val dialog = MessageDialogFragment()
            val bundle = Bundle()
            bundle.apply {
                putInt(TITLE_ID, titleId)
                putInt(DESCRIPTION_ID, descriptionId)
                putInt(HELP_LINK, helpLinkId)
            }
            dialog.arguments = bundle
            return dialog
        }

        fun newInstance(
            titleId: Int,
            description: String,
            secondaryDescription: String = "",
            helpLinkId: Int = 0
        ): MessageDialogFragment {
            val dialog = MessageDialogFragment()
            val bundle = Bundle()
            bundle.apply {
                putInt(TITLE_ID, titleId)
                putString(DESCRIPTION_STRING, description)
                putString(DESCRIPTION_STRING_2, secondaryDescription)
                putInt(HELP_LINK, helpLinkId)
            }
            dialog.arguments = bundle
            return dialog
        }
    }
}
