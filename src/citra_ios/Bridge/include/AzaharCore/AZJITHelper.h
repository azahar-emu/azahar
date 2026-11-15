// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// The helper app extension that enables JIT from outside the app.
@interface AZJITHelper : NSObject

/// Starts the helper and describes what happened (found, started, reply), on the main queue.
+ (void)testWithCompletion:(void (^)(NSString* report))completion;

/// Opens the tunnel to the device's debug service with the pairing file and says whether it is
/// reachable (a report, on the main queue).
+ (void)checkConnectionWithCompletion:(void (^)(NSString* report))completion;

/// Where the imported pairing file is kept, and what is known about pairing files: "Imported",
/// "Found in SideStore (LiveContainer)" or "None".
+ (NSString*)pairingStatus;

/// Copies a pairing file (a RemotePairing file) into the app. nil when it was accepted, otherwise
/// why not.
+ (nullable NSString*)importPairingFileAtURL:(NSURL*)url;
+ (void)removePairingFile;

@end

NS_ASSUME_NONNULL_END
