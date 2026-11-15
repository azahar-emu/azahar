// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// What this device's MetalFX offers for frame interpolation, read from the Objective-C runtime.
@interface AZMetalFXProbe : NSObject
+ (NSString*)report;
@end

NS_ASSUME_NONNULL_END
