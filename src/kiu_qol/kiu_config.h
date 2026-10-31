// Licensed under GPLv2 or any later version.
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "kiu_qol/kiu_types.h"

namespace KiuQol {

/// Minimal INI reader: [section], key = value, ';' or '#' comments, case-insensitive keys.
struct IniFile {
    struct Section {
        std::string name;
        std::map<std::string, std::string> values;
    };
    std::vector<Section> sections; ///< in file order

    static IniFile Parse(std::string_view text);
    const Section* Find(std::string_view name) const;
};

Config ParseConfig(std::string_view text);

/// The config shipped with the build; written to disk on first run.
extern const char* const kDefaultConfigText;

} // namespace KiuQol
