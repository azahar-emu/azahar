package org.citra.citra_emu.features.settings.ui

import android.app.AlertDialog
import android.content.Context
import android.content.SharedPreferences
import android.graphics.drawable.Drawable
import android.view.LayoutInflater
import android.view.View
import androidx.core.content.ContextCompat
import org.citra.citra_emu.R
import org.citra.citra_emu.databinding.DialogControllerQuickConfigBinding
import org.citra.citra_emu.features.settings.model.view.InputBindingSetting
import org.citra.citra_emu.features.settings.utils.InputBindingBase

class ControllerQuickConfigDialog(
    private var context: Context,
    buttons: ArrayList<List<String>>,
    titles: ArrayList<List<Int>>,
    private var preferences: SharedPreferences,
    adapter: SettingsAdapter
) {
    private var isWaiting = false;
    private var index = 0
    val inflater = LayoutInflater.from(context)
    private lateinit var quickConfigBinding: DialogControllerQuickConfigBinding
    var dialog: AlertDialog? = null
    private var boundVerticalDpadAxis = false
    private var boundHorizontalDpadAxis = false

    var allButtons = arrayListOf<String>()
    var allTitles = arrayListOf<Int>()

    private val inputHandler = object : InputBindingBase() {
        private val AXIS_WAIT_TIME = 400L
        private val BUTTON_WAIT_TIME = 100L

        private val handler = android.os.Handler(android.os.Looper.getMainLooper())

        override fun onAxisCaptured() {
            boundHorizontalDpadAxis =
                boundHorizontalDpadAxis || setting != null && setting!!.isVerticalAxis()
            boundVerticalDpadAxis =
                boundVerticalDpadAxis || setting != null && setting!!.isHorizontalAxis()
            onInputCaptured(AXIS_WAIT_TIME)
        }

        override fun onButtonCaptured() {
            onInputCaptured(BUTTON_WAIT_TIME)
        }

        private fun onInputCaptured(waitTime: Long) {
            if (isWaiting) {
                return
            }
            quickConfigBinding.root.cancelPendingInputEvents()
            index++
            setting?.let { settingsList.add(it) }
            isWaiting = true
            quickConfigBinding.currentMappingTitle.text = context.getString(R.string.controller_quick_config_wait)

            // the changed item after each setting is one more than the index, since the Quick Configure button is position 1
            adapter.notifyItemChanged(index + 1)

            // clear listeners during the waiting period, they will get reset once ready for input
            dialog?.setOnKeyListener(null)
            quickConfigBinding.root.setOnGenericMotionListener(null)

            // Wait before preparing the next input
            handler.postDelayed({
                isWaiting = false
                prepareUIforIndex()
            }, waitTime)
        }

        override fun getCurrentSetting() = setting
    }

    init {
        buttons.forEach { group ->
            group.forEach { button ->
                allButtons.add(button)
            }
        }
        titles.forEach { group ->
            group.forEach { title ->
                allTitles.add(title)
            }
        }
    }

    fun show() {
        val builder: AlertDialog.Builder = AlertDialog.Builder(context)
        quickConfigBinding = DialogControllerQuickConfigBinding.inflate(inflater)
        builder
            .setView(quickConfigBinding.root)
            .setTitle(context.getString(R.string.controller_quick_config))
            .setPositiveButton(context.getString(R.string.next)) { _, _ -> }
            .setNegativeButton(context.getString(R.string.close)) { dialog, which ->
                dialog.dismiss()
            }

        dialog = builder.create()
        dialog?.show()

        quickConfigBinding.root.requestFocus()
        quickConfigBinding.root.setOnFocusChangeListener { v, hasFocus ->
            if (!hasFocus) v.requestFocus()
        }

        // Prepare the first element
        prepareUIforIndex()

        val nextButton = dialog?.getButton(AlertDialog.BUTTON_POSITIVE)
        nextButton?.setOnClickListener {
            if (setting != null) setting!!.removeOldMapping()
            index++
            prepareUIforIndex()
        }
    }

    private fun prepareUIforIndex() {
        if (index >= allButtons.size) {
            dialog?.dismiss()
            return
        }
        setting = InputBindingSetting(
            InputBindingSetting.getInputObject(allButtons[index], preferences),
            allTitles[index]
        )
        // skip the dpad buttons if the corresponding axis is mapped
        while (setting != null && (
                    setting!!.isVerticalButton() && boundVerticalDpadAxis ||
                            setting!!.isHorizontalButton() && boundHorizontalDpadAxis)
        ) {
            index++
            if (index >= allButtons.size) {
                dialog?.dismiss()
                return
            }
            setting = InputBindingSetting(
                InputBindingSetting.getInputObject(
                    allButtons[index],
                    preferences
                ), allTitles[index]
            )

        }
        // show the previous key, if this isn't the first key
        if (index > 0) {
            quickConfigBinding.lastMappingIcon.visibility = View.VISIBLE
            quickConfigBinding.lastMappingDescription.visibility = View.VISIBLE
        }

        // change the button layout for the last button
        if (index == allButtons.size - 1) {
            dialog?.getButton(AlertDialog.BUTTON_POSITIVE)?.text =
                context.getString(R.string.finish)
            dialog?.getButton(AlertDialog.BUTTON_NEGATIVE)?.visibility = View.GONE
        }

        // set all the icons and text
        var lastTitle = setting?.value ?: ""
        if (lastTitle.isBlank()) {
            lastTitle = context.getString(R.string.unassigned)
        }
        quickConfigBinding.lastMappingDescription.text = lastTitle
        quickConfigBinding.lastMappingIcon.setImageDrawable(quickConfigBinding.currentMappingIcon.drawable)

        quickConfigBinding.currentMappingTitle.text = calculateTitle()
        quickConfigBinding.currentMappingDescription.text = setting?.value
        quickConfigBinding.currentMappingIcon.setImageDrawable(getIcon())

        // reset all the handlers
        if (setting!!.isButtonMappingSupported()) {
            dialog?.setOnKeyListener { _, _, event -> inputHandler.onKeyEvent(event) }
        }
        if (setting!!.isAxisMappingSupported()) {
            quickConfigBinding.root.setOnGenericMotionListener { _, event ->
                inputHandler.onMotionEvent(
                    event
                )
            }
        }
        inputHandler.reset()

    }

    private fun calculateTitle(): String {
        val inputTypeId = when {
            setting!!.isCirclePad() -> R.string.controller_circlepad
            setting!!.isCStick() -> R.string.controller_c
            setting!!.isDPad() -> R.string.controller_dpad
            setting!!.isTrigger() -> R.string.controller_trigger
            else -> R.string.button
        }

        val nameId = setting?.nameId?.let { context.getString(it) }

        return String.format(
            context.getString(R.string.input_dialog_title),
            context.getString(inputTypeId),
            nameId
        )
    }

    private fun getIcon(): Drawable? {
        val id = when {
            setting!!.isCirclePad() -> R.drawable.stick_main
            setting!!.isCStick() -> R.drawable.stick_c
            setting!!.isDPad() -> R.drawable.dpad
            else -> {
                val resourceTitle = context.resources.getResourceEntryName(setting!!.nameId)
                if (resourceTitle.startsWith("direction")) {
                    R.drawable.dpad
                } else {
                    context.resources.getIdentifier(resourceTitle, "drawable", context.packageName)
                }
            }
        }
        return ContextCompat.getDrawable(context, id)
    }

    private var setting: InputBindingSetting? = null

    private var settingsList = arrayListOf<InputBindingSetting>()
}