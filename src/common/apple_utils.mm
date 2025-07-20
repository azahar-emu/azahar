// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#import <Foundation/Foundation.h>

namespace AppleUtils {

int IsLowPowerModeEnabled() {
    return (int)[NSProcessInfo processInfo].lowPowerModeEnabled;
}

} // namespace AppleUtils
