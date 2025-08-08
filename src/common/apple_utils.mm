// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>

namespace AppleUtils {

float GetRefreshRate() { // TODO: How does this handle multi-monitor? -OS
    NSScreen* screen = [NSScreen mainScreen];
    if (screen) {
        NSDictionary* screenInfo = [screen deviceDescription];
        CGDirectDisplayID displayID =
            (CGDirectDisplayID)[screenInfo[@"NSScreenNumber"] unsignedIntValue];
        CGDisplayModeRef displayMode = CGDisplayCopyDisplayMode(displayID);
        if (displayMode) {
            CGFloat refreshRate = CGDisplayModeGetRefreshRate(displayMode);
            CFRelease(displayMode);
            return refreshRate;
        }
    }

    return 60; // Something went wrong, so just return a generic value
}

int IsLowPowerModeEnabled() {
    return (int)[NSProcessInfo processInfo].lowPowerModeEnabled;
}

} // namespace AppleUtils
