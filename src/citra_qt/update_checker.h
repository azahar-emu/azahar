// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <optional>
#include <string>

namespace UpdateChecker {
std::optional<std::string> GetLatestRelease(bool);
}
