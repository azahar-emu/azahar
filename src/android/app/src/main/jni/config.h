// Copyright 2014-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include "common/settings.h"

class INIReader;

class Config {
private:
    std::unique_ptr<INIReader> android_config;
    std::string android_config_loc;
    std::unique_ptr<INIReader> per_game_config;
    std::string per_game_config_loc;
    inline static std::map<std::string, std::string> default_android_values;

    bool LoadINI(const std::string& default_contents = "", bool retry = true);
    void ReadValues();

public:
    Config(u64 titleId = 0L);
    ~Config();
    static void AddDefaultSettingValue(const std::string& key, const std::string& value);
    void Reload();
    // Load a per-game config overlay by title id. Does not create files.
    void LoadPerGameConfig(u64 title_id);

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

    template <typename Type>
    Type GetSettingFromKey(const std::string& group, const std::string& key,
                           std::optional<Type> default_value = std::nullopt);
};
