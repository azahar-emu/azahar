// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <unistd.h>

namespace AppleUtils {

bool IsRunningFromTerminal() {
    return (getppid() != 1);
}

} // namespace AppleUtils
