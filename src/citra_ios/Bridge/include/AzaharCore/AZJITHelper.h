// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// The helper app extension that enables JIT from outside the app.
@interface AZJITHelper : NSObject

/// Starts the helper and describes what happened (found, started, reply), on the main queue.
+ (void)testWithCompletion:(void (^)(NSString* report))completion;

@end

NS_ASSUME_NONNULL_END
