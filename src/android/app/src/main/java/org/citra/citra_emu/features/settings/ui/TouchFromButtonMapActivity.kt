// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.ui

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.view.ViewGroup.MarginLayoutParams
import android.widget.ArrayAdapter
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import androidx.preference.PreferenceManager
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.textfield.MaterialAutoCompleteTextView
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.databinding.ActivityTouchFromButtonBinding
import org.citra.citra_emu.databinding.ItemTouchBindingBinding
import org.citra.citra_emu.features.settings.model.view.InputBindingSetting
import org.citra.citra_emu.features.settings.utils.SettingsFile
import org.citra.citra_emu.utils.ThemeUtil
import org.ini4j.Wini

/**
 * Activity for editing Touch From Button mapping profiles.
 * Allows creating/deleting/renaming profiles and adding button→touch coordinate bindings.
 */
class TouchFromButtonMapActivity : AppCompatActivity() {
    private lateinit var binding: ActivityTouchFromButtonBinding

    companion object {
        const val MAX_TOUCH_BINDINGS = 10

        /**
         * Register all touch-from-button bindings for the active map profile
         * into SharedPreferences. Call this on emulation start to ensure the
         * dispatch loop can fire TOUCH_ONLY_N events.
         */
        @JvmStatic
        fun registerActiveMapBindings() {
            val preferences = PreferenceManager.getDefaultSharedPreferences(
                CitraApplication.appContext
            )
            val mapCount = NativeLibrary.getTouchFromButtonMapCount()
            if (mapCount == 0) return
            // Use map index 0 (active map is stored in settings, default 0)
            val activeIndex = 0.coerceIn(0, mapCount - 1)
            val bindsArray = NativeLibrary.getTouchFromButtonMapBinds(activeIndex)
            val editor = preferences.edit()
            for (bindStr in bindsArray) {
                val params = mutableMapOf<String, String>()
                for (part in bindStr.split(",")) {
                    val kv = part.split(":", limit = 2)
                    if (kv.size == 2) params[kv[0]] = kv[1]
                }
                val code = params["code"]?.toIntOrNull() ?: continue
                val srcKey = params["src_key"]?.toIntOrNull() ?: -1
                val srcAxis = params["src_axis"]?.toIntOrNull() ?: -1
                if (srcKey >= 0) {
                    val prefKey = InputBindingSetting.getInputButtonKey(srcKey)
                    val existing = try {
                        preferences.getStringSet(prefKey, mutableSetOf())?.toMutableSet()
                            ?: mutableSetOf()
                    } catch (e: ClassCastException) {
                        mutableSetOf()
                    }
                    existing.add(code.toString())
                    editor.putStringSet(prefKey, existing)
                } else if (srcAxis >= 0) {
                    val prefKey =
                        InputBindingSetting.getInputAxisButtonKey(srcAxis) + "_Touch"
                    editor.putInt(prefKey, code)
                }
            }
            editor.apply()
        }
    }

    // Data: list of map profiles, each containing a name and list of bindings
    // srcKey: Android KeyEvent keyCode that triggers this binding (-1 if axis-based)
    // srcAxis: Android MotionEvent axis ID that triggers this binding (-1 if key-based)
    // touchOnlySlot: the TOUCH_ONLY_N slot (1-10) allocated for this binding
    data class TouchBinding(
        val touchOnlySlot: Int,
        var x: Int,
        var y: Int,
        val srcKey: Int = -1,
        val srcAxis: Int = -1
    )
    data class TouchMap(var name: String, val bindings: MutableList<TouchBinding>)

    private val maps = mutableListOf<TouchMap>()
    private var currentMapIndex = 0

    private lateinit var bindingsAdapter: BindingsAdapter

    override fun onCreate(savedInstanceState: Bundle?) {
        ThemeUtil.setTheme(this)
        super.onCreate(savedInstanceState)

        binding = ActivityTouchFromButtonBinding.inflate(layoutInflater)
        setContentView(binding.root)

        WindowCompat.setDecorFitsSystemWindows(window, false)

        binding.toolbar.setNavigationOnClickListener { onBackPressedDispatcher.onBackPressed() }

        setInsets()

        loadMaps()
        registerCurrentMapBindings()
        setupMapSelector()
        setupBindingsList()
        setupPreview()
        setupButtons()
    }

    override fun onPause() {
        super.onPause()
        saveMaps()
        unregisterAllTouchBindings()
        registerCurrentMapBindings()
    }

    private fun loadMaps() {
        maps.clear()
        val mapCount = NativeLibrary.getTouchFromButtonMapCount()
        for (i in 0 until mapCount) {
            val name = NativeLibrary.getTouchFromButtonMapName(i)
            val bindsArray = NativeLibrary.getTouchFromButtonMapBinds(i)
            val bindings = mutableListOf<TouchBinding>()
            for (bindStr in bindsArray) {
                val binding = parseBinding(bindStr)
                if (binding != null) {
                    bindings.add(binding)
                }
            }
            maps.add(TouchMap(name, bindings))
        }
        if (maps.isEmpty()) {
            maps.add(TouchMap("default", mutableListOf()))
        }
        currentMapIndex = currentMapIndex.coerceIn(0, maps.size - 1)
    }

    private fun saveMaps() {
        val names = maps.map { it.name }.toTypedArray()
        val binds = maps.map { map ->
            map.bindings.map { serializeBinding(it) }.toTypedArray()
        }.toTypedArray()
        NativeLibrary.setTouchFromButtonMaps(names, binds)

        saveToConfigIni()

        NativeLibrary.reloadTouchFromButtonMaps()
    }

    private fun saveToConfigIni() {
        try {
            val configFile = SettingsFile.getSettingsFile(SettingsFile.FILE_NAME_CONFIG)
            val context = applicationContext
            val inputStream = context.contentResolver.openInputStream(configFile.uri)
            val writer = Wini(inputStream)
            inputStream?.close()

            writer.put("Controls", "touch_from_button_map_count", maps.size)
            for (i in maps.indices) {
                writer.put("Controls", "touch_from_button_map_${i}_name", maps[i].name)
                writer.put("Controls", "touch_from_button_map_${i}_bind_count", maps[i].bindings.size)
                for (j in maps[i].bindings.indices) {
                    writer.put(
                        "Controls",
                        "touch_from_button_map_${i}_bind_${j}",
                        serializeBinding(maps[i].bindings[j])
                    )
                }
            }

            val outputStream = context.contentResolver.openOutputStream(configFile.uri, "wt")
            writer.store(outputStream)
            outputStream?.flush()
            outputStream?.close()
        } catch (e: Exception) {
            Toast.makeText(this, "Error saving touch maps: ${e.message}", Toast.LENGTH_SHORT).show()
        }
    }

    private fun parseBinding(str: String): TouchBinding? {
        // Format: "engine:gamepad,code:<TOUCH_ONLY_N>,x:<x>,y:<y>,src_key:<keyId>"
        //     or: "engine:gamepad,code:<TOUCH_ONLY_N>,x:<x>,y:<y>,src_axis:<axisId>"
        val params = mutableMapOf<String, String>()
        for (part in str.split(",")) {
            val kv = part.split(":", limit = 2)
            if (kv.size == 2) {
                params[kv[0]] = kv[1]
            }
        }
        val code = params["code"]?.toIntOrNull() ?: return null
        val x = params["x"]?.toIntOrNull() ?: 160
        val y = params["y"]?.toIntOrNull() ?: 120
        val srcKey = params["src_key"]?.toIntOrNull() ?: -1
        val srcAxis = params["src_axis"]?.toIntOrNull() ?: -1
        // Derive the slot number from the code (900 → 1, 901 → 2, etc.)
        val slot = code - NativeLibrary.ButtonType.TOUCH_ONLY_1 + 1
        return TouchBinding(slot.coerceIn(1, MAX_TOUCH_BINDINGS), x, y, srcKey, srcAxis)
    }

    private fun serializeBinding(binding: TouchBinding): String {
        val code = NativeLibrary.ButtonType.TOUCH_ONLY_1 + binding.touchOnlySlot - 1
        val sb = StringBuilder("engine:gamepad,code:$code,x:${binding.x},y:${binding.y}")
        if (binding.srcKey >= 0) sb.append(",src_key:${binding.srcKey}")
        if (binding.srcAxis >= 0) sb.append(",src_axis:${binding.srcAxis}")
        return sb.toString()
    }

    private fun setupMapSelector() {
        updateMapSelector()
    }

    private fun updateMapSelector() {
        val adapter = ArrayAdapter(this, android.R.layout.simple_dropdown_item_1line,
            maps.map { it.name })
        (binding.mapSelector as MaterialAutoCompleteTextView).apply {
            setAdapter(adapter)
            setText(maps.getOrNull(currentMapIndex)?.name ?: "", false)
            setOnItemClickListener { _, _, position, _ ->
                unregisterAllTouchBindings()
                currentMapIndex = position
                registerCurrentMapBindings()
                refreshBindings()
            }
        }
    }

    private fun setupButtons() {
        binding.buttonNewMap.setOnClickListener { showNewMapDialog() }
        binding.buttonRenameMap.setOnClickListener { showRenameMapDialog() }
        binding.buttonDeleteMap.setOnClickListener { showDeleteMapDialog() }
        binding.fabAddBinding.setOnClickListener { startAddBinding() }
    }

    private fun showNewMapDialog() {
        val input = android.widget.EditText(this)
        input.hint = getString(R.string.touch_from_button_enter_map_name)
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.touch_from_button_new_map)
            .setView(input)
            .setPositiveButton(android.R.string.ok) { _, _ ->
                val name = input.text.toString().trim()
                if (name.isNotEmpty()) {
                    maps.add(TouchMap(name, mutableListOf()))
                    currentMapIndex = maps.size - 1
                    updateMapSelector()
                    refreshBindings()
                    saveMaps()
                }
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun showRenameMapDialog() {
        if (maps.isEmpty()) return
        val input = android.widget.EditText(this)
        input.setText(maps[currentMapIndex].name)
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.touch_from_button_rename_map)
            .setView(input)
            .setPositiveButton(android.R.string.ok) { _, _ ->
                val name = input.text.toString().trim()
                if (name.isNotEmpty()) {
                    maps[currentMapIndex].name = name
                    updateMapSelector()
                    saveMaps()
                }
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun showDeleteMapDialog() {
        if (maps.size <= 1) {
            Toast.makeText(this, "Cannot delete the last map", Toast.LENGTH_SHORT).show()
            return
        }
        MaterialAlertDialogBuilder(this)
            .setTitle(R.string.touch_from_button_delete_map)
            .setMessage(R.string.touch_from_button_delete_confirm)
            .setPositiveButton(android.R.string.ok) { _, _ ->
                unregisterAllTouchBindings()
                maps.removeAt(currentMapIndex)
                currentMapIndex = currentMapIndex.coerceIn(0, maps.size - 1)
                registerCurrentMapBindings()
                updateMapSelector()
                refreshBindings()
                saveMaps()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun startAddBinding() {
        val currentBindings = maps.getOrNull(currentMapIndex)?.bindings ?: emptyList()
        if (currentBindings.size >= MAX_TOUCH_BINDINGS) {
            Toast.makeText(this, "Maximum $MAX_TOUCH_BINDINGS bindings reached", Toast.LENGTH_SHORT).show()
            return
        }
        TouchButtonPollingDialogFragment.newInstance(
            onButtonCaptured = { result ->
                promptTapPosition(result)
            },
            onCancelled = { /* nothing to do */ }
        ).show(supportFragmentManager, TouchButtonPollingDialogFragment.TAG)
    }

    private fun promptTapPosition(result: PollingResult) {
        Toast.makeText(this, R.string.touch_from_button_tap_position, Toast.LENGTH_LONG).show()
        binding.touchScreenPreview.enableTapMode()
        binding.touchScreenPreview.onPointAdded = { x, y ->
            binding.touchScreenPreview.onPointAdded = null
            addBindingToCurrentMap(result, x, y)
        }
    }

    private fun addBindingToCurrentMap(result: PollingResult, x: Int, y: Int) {
        if (maps.isEmpty()) return
        val slot = allocateTouchOnlySlot()
        if (slot == -1) {
            Toast.makeText(this, "Maximum $MAX_TOUCH_BINDINGS bindings reached", Toast.LENGTH_SHORT).show()
            return
        }
        val srcKey = if (result.keyEvent != null)
            InputBindingSetting.translateEventToKeyId(result.keyEvent) else -1
        val srcAxis = result.axisId
        val touchBinding = TouchBinding(slot, x, y, srcKey, srcAxis)
        maps[currentMapIndex].bindings.add(touchBinding)
        registerBinding(touchBinding)
        refreshBindings()
        saveMaps()
    }

    /** Allocate the next available TOUCH_ONLY slot (1-10) in the current map. */
    private fun allocateTouchOnlySlot(): Int {
        val usedSlots = maps.getOrNull(currentMapIndex)?.bindings?.map { it.touchOnlySlot }?.toSet()
            ?: emptySet()
        for (slot in 1..MAX_TOUCH_BINDINGS) {
            if (slot !in usedSlots) return slot
        }
        return -1
    }

    /**
     * Register a single touch binding in SharedPreferences so that the emulation
     * dispatch loop can fire the corresponding TOUCH_ONLY_N button event.
     */
    private fun registerBinding(binding: TouchBinding) {
        val preferences = PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)
        val editor = preferences.edit()
        val touchOnlyButtonType = NativeLibrary.ButtonType.TOUCH_ONLY_1 + binding.touchOnlySlot - 1
        if (binding.srcKey >= 0) {
            // Key-based: append TOUCH_ONLY_N to the existing StringSet for this keycode
            val prefKey = InputBindingSetting.getInputButtonKey(binding.srcKey)
            val existing = try {
                preferences.getStringSet(prefKey, mutableSetOf())?.toMutableSet() ?: mutableSetOf()
            } catch (e: ClassCastException) {
                mutableSetOf()
            }
            existing.add(touchOnlyButtonType.toString())
            editor.putStringSet(prefKey, existing)
        } else if (binding.srcAxis >= 0) {
            // Axis-based: write to a separate "_Touch" key to avoid overwriting 3DS axis mapping
            val prefKey = InputBindingSetting.getInputAxisButtonKey(binding.srcAxis) + "_Touch"
            editor.putInt(prefKey, touchOnlyButtonType)
        }
        editor.apply()
    }

    private fun unregisterBinding(binding: TouchBinding) {
        val preferences = PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)
        val editor = preferences.edit()
        val touchOnlyButtonType = NativeLibrary.ButtonType.TOUCH_ONLY_1 + binding.touchOnlySlot - 1
        if (binding.srcKey >= 0) {
            val prefKey = InputBindingSetting.getInputButtonKey(binding.srcKey)
            val existing = try {
                preferences.getStringSet(prefKey, mutableSetOf())?.toMutableSet() ?: mutableSetOf()
            } catch (e: ClassCastException) {
                mutableSetOf()
            }
            existing.remove(touchOnlyButtonType.toString())
            editor.putStringSet(prefKey, existing)
        } else if (binding.srcAxis >= 0) {
            val prefKey = InputBindingSetting.getInputAxisButtonKey(binding.srcAxis) + "_Touch"
            editor.remove(prefKey)
        }
        editor.apply()
    }

    private fun unregisterAllTouchBindings() {
        val preferences = PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)
        val editor = preferences.edit()
        // Remove TOUCH_ONLY values from all button StringSets
        for (map in maps) {
            for (b in map.bindings) {
                val touchType = NativeLibrary.ButtonType.TOUCH_ONLY_1 + b.touchOnlySlot - 1
                if (b.srcKey >= 0) {
                    val prefKey = InputBindingSetting.getInputButtonKey(b.srcKey)
                    val existing = try {
                        preferences.getStringSet(prefKey, mutableSetOf())?.toMutableSet()
                            ?: mutableSetOf()
                    } catch (e: ClassCastException) {
                        mutableSetOf()
                    }
                    existing.remove(touchType.toString())
                    editor.putStringSet(prefKey, existing)
                } else if (b.srcAxis >= 0) {
                    val prefKey = InputBindingSetting.getInputAxisButtonKey(b.srcAxis) + "_Touch"
                    editor.remove(prefKey)
                }
            }
        }
        editor.apply()
    }

    /** Register all bindings from the current map profile in SharedPreferences. */
    private fun registerCurrentMapBindings() {
        val currentBindings = maps.getOrNull(currentMapIndex)?.bindings ?: return
        for (b in currentBindings) {
            registerBinding(b)
        }
    }

    private fun setupBindingsList() {
        bindingsAdapter = BindingsAdapter()
        binding.bindingsList.layoutManager = LinearLayoutManager(this)
        binding.bindingsList.adapter = bindingsAdapter
        refreshBindings()
    }

    private fun refreshBindings() {
        val currentBindings = maps.getOrNull(currentMapIndex)?.bindings ?: emptyList()
        bindingsAdapter.submitList(currentBindings.toList())
        updatePreviewPoints()
    }

    private fun setupPreview() {
        binding.touchScreenPreview.onPointMoved = { index, x, y ->
            if (currentMapIndex in maps.indices) {
                val bindings = maps[currentMapIndex].bindings
                if (index in bindings.indices) {
                    bindings[index].x = x
                    bindings[index].y = y
                    bindingsAdapter.notifyItemChanged(index)
                    saveMaps()
                }
            }
        }
        binding.touchScreenPreview.onPointSelected = { index ->
            bindingsAdapter.setSelectedIndex(index)
        }
        updatePreviewPoints()
    }

    private fun updatePreviewPoints() {
        val currentBindings = maps.getOrNull(currentMapIndex)?.bindings ?: emptyList()
        val points = currentBindings.map {
            TouchScreenPreviewView.TouchPoint(it.x, it.y, it.touchOnlySlot)
        }
        binding.touchScreenPreview.setPoints(points)
    }

    private fun setInsets() {
        ViewCompat.setOnApplyWindowInsetsListener(binding.appbar) { view, windowInsets ->
            val barInsets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            val cutoutInsets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())
            val mlp = view.layoutParams as MarginLayoutParams
            mlp.topMargin = barInsets.top
            mlp.leftMargin = barInsets.left + cutoutInsets.left
            mlp.rightMargin = barInsets.right + cutoutInsets.right
            view.layoutParams = mlp

            val mlpShade = binding.navigationBarShade.layoutParams as MarginLayoutParams
            mlpShade.height = barInsets.bottom
            binding.navigationBarShade.layoutParams = mlpShade

            windowInsets
        }
    }

    inner class BindingsAdapter : RecyclerView.Adapter<BindingsAdapter.ViewHolder>() {
        private var items = listOf<TouchBinding>()
        private var selectedIndex = -1

        fun submitList(newItems: List<TouchBinding>) {
            items = newItems
            notifyDataSetChanged()
        }

        fun setSelectedIndex(index: Int) {
            val old = selectedIndex
            selectedIndex = index
            if (old >= 0 && old < items.size) notifyItemChanged(old)
            if (index >= 0 && index < items.size) notifyItemChanged(index)
        }

        inner class ViewHolder(val binding: ItemTouchBindingBinding) :
            RecyclerView.ViewHolder(binding.root)

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ViewHolder {
            val itemBinding = ItemTouchBindingBinding.inflate(
                LayoutInflater.from(parent.context), parent, false
            )
            return ViewHolder(itemBinding)
        }

        override fun onBindViewHolder(holder: ViewHolder, position: Int) {
            val item = items[position]
            val srcLabel = when {
                item.srcKey >= 0 -> "Key ${item.srcKey}"
                item.srcAxis >= 0 -> "Axis ${item.srcAxis}"
                else -> "?"
            }
            holder.binding.textBindingInfo.text =
                getString(R.string.touch_from_button_binding_format, srcLabel, item.x, item.y)

            holder.itemView.isSelected = position == selectedIndex
            holder.itemView.alpha = if (position == selectedIndex) 1.0f else 0.8f

            holder.itemView.setOnClickListener {
                this@TouchFromButtonMapActivity.binding.touchScreenPreview.setSelectedIndex(position)
                setSelectedIndex(position)
            }

            holder.binding.buttonDeleteBinding.setOnClickListener {
                if (currentMapIndex in maps.indices) {
                    val removed = maps[currentMapIndex].bindings.removeAt(position)
                    unregisterBinding(removed)
                    refreshBindings()
                    saveMaps()
                }
            }
        }

        override fun getItemCount() = items.size
    }
}
