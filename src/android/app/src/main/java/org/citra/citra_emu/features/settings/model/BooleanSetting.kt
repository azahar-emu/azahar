// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model

enum class BooleanSetting(
    override val key: String,
    override val section: String,
    override val defaultValue: Boolean
) : AbstractBooleanSetting {
    EXPAND_TO_CUTOUT_AREA("expand_to_cutout_area", Settings.SECTION_LAYOUT, false),
    SPIRV_SHADER_GEN("spirv_shader_gen", Settings.SECTION_RENDERER, true),
    ASYNC_SHADERS("async_shader_compilation", Settings.SECTION_RENDERER, false),
    PLUGIN_LOADER("plugin_loader", Settings.SECTION_SYSTEM, false),
    ALLOW_PLUGIN_LOADER("allow_plugin_loader", Settings.SECTION_SYSTEM, true),
    SWAP_SCREEN("swap_screen", Settings.SECTION_LAYOUT, false),
    INSTANT_DEBUG_LOG("instant_debug_log", Settings.SECTION_DEBUG, false),
    ENABLE_RPC_SERVER("enable_rpc_server", Settings.SECTION_DEBUG, false),
    CUSTOM_LAYOUT("custom_layout",Settings.SECTION_LAYOUT,false),
    OVERLAY_SHOW_FPS("overlay_show_fps", Settings.SECTION_LAYOUT, true),
    OVERLAY_SHOW_FRAMETIME("overlay_show_frame_time", Settings.SECTION_LAYOUT, false),
    OVERLAY_SHOW_SPEED("overlay_show_speed", Settings.SECTION_LAYOUT, false),
    OVERLAY_SHOW_APP_RAM_USAGE("overlay_show_app_ram_usage", Settings.SECTION_LAYOUT, false),
    OVERLAY_SHOW_AVAILABLE_RAM("overlay_show_available_ram", Settings.SECTION_LAYOUT, false),
    OVERLAY_SHOW_BATTERY_TEMP("overlay_show_battery_temp", Settings.SECTION_LAYOUT, false),
    OVERLAY_BACKGROUND("overlay_background", Settings.SECTION_LAYOUT, false),
    DELAY_START_LLE_MODULES("delay_start_for_lle_modules", Settings.SECTION_DEBUG, true),
    DETERMINISTIC_ASYNC_OPERATIONS("deterministic_async_operations", Settings.SECTION_DEBUG, false),
    REQUIRED_ONLINE_LLE_MODULES("enable_required_online_lle_modules", Settings.SECTION_SYSTEM, false);

    override var boolean: Boolean = defaultValue

    override val valueAsString: String
        get() = boolean.toString()

    override val isRuntimeEditable: Boolean
        get() {
            for (setting in NOT_RUNTIME_EDITABLE) {
                if (setting == this) {
                    return false
                }
            }
            return true
        }

    companion object {
        private val NOT_RUNTIME_EDITABLE = listOf(
            PLUGIN_LOADER,
            ALLOW_PLUGIN_LOADER, 
            ASYNC_SHADERS,
            DELAY_START_LLE_MODULES,
            DETERMINISTIC_ASYNC_OPERATIONS,
            REQUIRED_ONLINE_LLE_MODULES,
        )

        fun from(key: String): BooleanSetting? =
            BooleanSetting.values().firstOrNull { it.key == key }

        fun clear() = BooleanSetting.values().forEach { it.boolean = it.defaultValue }
    }
}
