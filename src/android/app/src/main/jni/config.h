// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <string>
#include "common/settings.h"

class INIReader;

class Config {
private:
    std::unique_ptr<INIReader> android_config;
    std::string android_config_loc;
    std::unique_ptr<INIReader> per_game_config;
    std::string per_game_config_loc;

    bool LoadINI(const std::string& default_contents = "", bool retry = true);
    void ReadValues();

public:
    Config();
    ~Config();

    void Reload();
    // Load a per-game config overlay by title id or fallback name. Does not create files.
    void LoadPerGameConfig(u64 title_id, const std::string& fallback_name = "");

private:
    /**
     * Applies a value read from the android_config to a Setting.
     *
     * @param group The name of the INI group
     * @param setting The setting to modify
     */
    template <typename Type, bool ranged>
    void ReadSetting(const std::string& group, Settings::Setting<Type, ranged>& setting);

    /**
    * Reads a value honoring per_game config, and returns it.
    * Does not modify the setting.
    *
    * @param group The name of the INI group
    * @param setting The setting to modify
    */
    template <typename Type, bool ranged>
    Type GetSetting(const std::string& group, Settings::Setting<Type, ranged>& setting);

};
