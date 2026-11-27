// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

namespace CitraCLI {

constexpr char compression_ops_optstring[] = "c:x:o:";
constexpr char cli_capture_optstring[] = "c:x:o:";

bool CheckForOptions(const char* optstring, int argc, char* argv[]);
int ParseCommand(int argc, char* argv[]);

} // namespace CitraCLI
