// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.fragments

import android.app.Dialog
import android.os.Bundle
import android.view.View
import android.widget.ProgressBar
import androidx.fragment.app.DialogFragment
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import androidx.lifecycle.Lifecycle
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.launch
import org.citra.citra_emu.R
import org.citra.citra_emu.viewmodel.CompressProgressDialogViewModel
import org.citra.citra_emu.NativeLibrary

class CompressProgressDialogFragment : DialogFragment() {
    private lateinit var progressBar: ProgressBar
    private var outputPath: String? = null
    private var isCompressing: Boolean = true

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        arguments?.let {
            isCompressing = it.getBoolean(ARG_IS_COMPRESSING, true)
            outputPath = it.getString(ARG_OUTPUT_PATH)
        }
    }

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        val view = layoutInflater.inflate(R.layout.dialog_compress_progress, null)
        progressBar = view.findViewById(R.id.compress_progress)
        val label = view.findViewById<android.widget.TextView>(R.id.compress_label)
        label.text = if (isCompressing) getString(R.string.compressing) else getString(R.string.decompressing)

        isCancelable = false
        progressBar.isIndeterminate = true

        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                combine(CompressProgressDialogViewModel.total, CompressProgressDialogViewModel.progress) { total, progress ->
                    total to progress
                }.collectLatest { (total, progress) ->
                    if (total <= 0) {
                        progressBar.isIndeterminate = true
                        label.visibility = View.GONE
                    } else {
                        progressBar.isIndeterminate = false
                        label.visibility = View.VISIBLE
                        progressBar.max = total
                        progressBar.setProgress(progress, true)
                    }
                }
            }
        }

        val builder = MaterialAlertDialogBuilder(requireContext())
            .setView(view)
            .setCancelable(false)
            .setNegativeButton(android.R.string.cancel) { _: android.content.DialogInterface, _: Int ->
                outputPath?.let { path ->
                    NativeLibrary.deleteDocument(path)
                }
            }

        return builder.show()
    }

    companion object {
        const val TAG = "CompressProgressDialog"
        private const val ARG_IS_COMPRESSING = "isCompressing"
        private const val ARG_OUTPUT_PATH = "outputPath"

        fun newInstance(isCompressing: Boolean, outputPath: String?): CompressProgressDialogFragment {
            val frag = CompressProgressDialogFragment()
            val args = Bundle()
            args.putBoolean(ARG_IS_COMPRESSING, isCompressing)
            args.putString(ARG_OUTPUT_PATH, outputPath)
            frag.arguments = args
            return frag
        }
    }
}
