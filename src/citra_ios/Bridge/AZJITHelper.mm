// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

// Starts the JIT helper app extension (see JITHelper/main.m) and looks after the pairing file it
// needs. The extension API for starting an extension from its own app is private, so the
// selectors are looked up at run time.
//
// The way of starting the extension follows the launcher of ARMSX3 and DolphiniOS (GPL-2.0-or-
// later, https://github.com/OatmealDome/dolphin-ios pull request 277).

#import <AzaharCore/AZEmulator.h>
#import <AzaharCore/AZJITHelper.h>
#include "common/apple_jit.h"
#include <unistd.h>
#import <objc/message.h>
#import <objc/runtime.h>

static const NSUInteger kMaxPairingBytes = 65536;

@implementation AZJITHelper

// MARK: Pairing file

+ (NSURL*)pairingFileURL {
    NSURL* support = [NSFileManager.defaultManager URLsForDirectory:NSApplicationSupportDirectory
                                                          inDomains:NSUserDomainMask].firstObject;
    return [[support URLByAppendingPathComponent:@"JIT" isDirectory:YES]
        URLByAppendingPathComponent:@"pairingFile.plist"];
}

/// The files of SideStore that hold a pairing file of the kind the helper takes, when this app
/// runs inside LiveContainer (which shares its folder with the guests).
+ (NSArray<NSURL*>*)sideStoreCandidates {
    const char* home = getenv("LC_HOME_PATH");
    if (home == NULL) {
        return @[];
    }
    NSString* base = [[NSString stringWithUTF8String:home]
        stringByAppendingPathComponent:@"Documents/SideStore/Documents"];
    NSMutableArray* result = [NSMutableArray array];
    for (NSString* name in @[ @"PairingFile_RemoteRP.plist", @"ALTPairingFile.mobiledevicepairing" ]) {
        NSURL* url = [NSURL fileURLWithPath:[base stringByAppendingPathComponent:name]];
        if ([NSFileManager.defaultManager fileExistsAtPath:url.path]) {
            [result addObject:url];
        }
    }
    return result;
}

/// nil when the bytes are a pairing file of the kind the helper takes (a RemotePairing file:
/// two 32 byte keys and an identifier), otherwise why not.
+ (NSString*)problemWithPairingData:(NSData*)data {
    if (data.length == 0 || data.length > kMaxPairingBytes) {
        return @"The file is empty or too big for a pairing file.";
    }
    id plist = [NSPropertyListSerialization propertyListWithData:data
                                                         options:NSPropertyListImmutable
                                                          format:nil
                                                           error:nil];
    if (![plist isKindOfClass:NSDictionary.class]) {
        return @"The file is not a property list.";
    }
    NSDictionary* dictionary = plist;
    // The RemotePairing keys decide. A file can hold both kinds (the ones SideStore writes on
    // recent iOS versions have the old lockdown keys as well), which is fine.
    NSData* publicKey = dictionary[@"public_key"];
    NSData* privateKey = dictionary[@"private_key"];
    if (![publicKey isKindOfClass:NSData.class] || publicKey.length != 32 ||
        ![privateKey isKindOfClass:NSData.class] || privateKey.length != 32 ||
        ![dictionary[@"identifier"] isKindOfClass:NSString.class]) {
        if (dictionary[@"HostCertificate"] || dictionary[@"DeviceCertificate"] ||
            dictionary[@"HostPrivateKey"] || dictionary[@"SystemBUID"]) {
            return @"This is an older kind of pairing file (lockdown) without the RemotePairing "
                   @"keys. The helper needs one with public_key, private_key and identifier.";
        }
        return @"This is not a RemotePairing file (public_key, private_key and identifier).";
    }
    return nil;
}

+ (NSString*)importPairingFileAtURL:(NSURL*)url {
    const BOOL scoped = [url startAccessingSecurityScopedResource];
    NSData* data = [NSData dataWithContentsOfURL:url];
    if (scoped) {
        [url stopAccessingSecurityScopedResource];
    }
    if (data == nil) {
        return @"The file could not be read.";
    }
    NSString* problem = [self problemWithPairingData:data];
    if (problem != nil) {
        return problem;
    }
    NSURL* destination = [self pairingFileURL];
    [NSFileManager.defaultManager createDirectoryAtURL:destination.URLByDeletingLastPathComponent
                           withIntermediateDirectories:YES
                                            attributes:nil
                                                 error:nil];
    NSError* error = nil;
    if (![data writeToURL:destination options:NSDataWritingAtomic error:&error]) {
        return error.localizedDescription;
    }
    [destination setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:nil];
    return nil;
}

+ (void)removePairingFile {
    [NSFileManager.defaultManager removeItemAtURL:[self pairingFileURL] error:nil];
}

+ (NSString*)pairingStatus {
    NSData* own = [NSData dataWithContentsOfURL:[self pairingFileURL]];
    if (own != nil && [self problemWithPairingData:own] == nil) {
        return @"Imported";
    }
    if ([self sideStoreCandidates].count > 0) {
        return @"Found in SideStore (LiveContainer)";
    }
    return @"None";
}

/// The pairing files to try, in order: the imported one, then SideStore's.
+ (NSArray<NSData*>*)pairingFiles {
    NSMutableArray<NSData*>* result = [NSMutableArray array];
    NSMutableArray<NSURL*>* urls = [NSMutableArray arrayWithObject:[self pairingFileURL]];
    [urls addObjectsFromArray:[self sideStoreCandidates]];
    for (NSURL* url in urls) {
        NSData* data = [NSData dataWithContentsOfURL:url];
        if (data != nil && [self problemWithPairingData:data] == nil) {
            [result addObject:data];
        }
    }
    return result;
}

// MARK: Starting the extension

+ (NSString*)helperIdentifier {
    NSURL* plugins = NSBundle.mainBundle.builtInPlugInsURL;
    NSArray<NSURL*>* items = [NSFileManager.defaultManager contentsOfDirectoryAtURL:plugins
                                                        includingPropertiesForKeys:nil
                                                                           options:0
                                                                             error:nil];
    for (NSURL* url in items) {
        if ([url.pathExtension isEqualToString:@"appex"]) {
            return [NSBundle bundleWithURL:url].bundleIdentifier;
        }
    }
    return nil;
}

/// Starts the helper with a request and reports the reply (nil when there was none) and a log of
/// what was found and done, on the main queue.
+ (void)runWithInfo:(NSDictionary*)info
         completion:(void (^)(NSDictionary* _Nullable reply, NSString* log))completion {
    NSMutableString* log = [NSMutableString string];
    void (^finish)(NSDictionary*) = ^(NSDictionary* reply) {
      dispatch_async(dispatch_get_main_queue(), ^{
        completion(reply, [log copy]);
      });
    };

    NSString* identifier = [self helperIdentifier];
    if (identifier == nil) {
        [log appendString:@"The app has no helper extension (PlugIns folder is empty).\n"];
        finish(nil);
        return;
    }
    [log appendFormat:@"Helper extension: %@\n", identifier];

    Class extensionClass = NSClassFromString(@"NSExtension");
    SEL lookup = NSSelectorFromString(@"extensionWithIdentifier:excludingDisabledExtensions:error:");
    if (extensionClass == Nil || ![extensionClass respondsToSelector:lookup]) {
        [log appendString:@"The system has no way to look the extension up (selector gone).\n"];
        finish(nil);
        return;
    }
    NSError* error = nil;
    id extension = ((id(*)(Class, SEL, NSString*, BOOL, NSError**))objc_msgSend)(
        extensionClass, lookup, identifier, NO, &error);
    if (extension == nil) {
        [log appendFormat:@"The system does not know the extension: %@\n",
                          error.localizedDescription ?: @"no error given"];
        finish(nil);
        return;
    }

    NSMutableDictionary* userInfo = [NSMutableDictionary dictionaryWithDictionary:info];
    userInfo[@"hostPid"] = @(getpid());
    const NSTimeInterval timeout = [userInfo[@"timeoutSeconds"] doubleValue] ?: 20;
    [userInfo removeObjectForKey:@"timeoutSeconds"];
    NSExtensionItem* item = [[NSExtensionItem alloc] init];
    item.userInfo = userInfo;

    SEL setCompletion = NSSelectorFromString(@"setRequestCompletionBlock:");
    SEL beginCompletion = NSSelectorFromString(@"beginExtensionRequestWithInputItems:completion:");
    SEL beginError = NSSelectorFromString(@"beginExtensionRequestWithInputItems:error:");
    SEL pidFor = NSSelectorFromString(@"pidForRequestIdentifier:");
    SEL kill = NSSelectorFromString(@"_kill:");

    __block BOOL answered = NO;
    void (^onReply)(NSUUID*, NSArray*) = ^(NSUUID* request, NSArray* items) {
      if (answered) {
          return;
      }
      answered = YES;
      NSExtensionItem* first = items.firstObject;
      if ([extension respondsToSelector:kill] && request != nil) {
          ((void (*)(id, SEL, NSUUID*))objc_msgSend)(extension, kill, request);
      }
      finish(first.userInfo);
    };
    if ([extension respondsToSelector:setCompletion]) {
        ((void (*)(id, SEL, id))objc_msgSend)(extension, setCompletion, onReply);
    }

    if ([extension respondsToSelector:beginCompletion]) {
        ((void (*)(id, SEL, NSArray*, id))objc_msgSend)(
            extension, beginCompletion, @[ item ], ^(NSUUID* request) {
              [log appendFormat:@"Started, request %@\n", request];
              if ([extension respondsToSelector:pidFor] && request != nil) {
                  const int pid = ((int (*)(id, SEL, NSUUID*))objc_msgSend)(extension, pidFor,
                                                                            request);
                  [log appendFormat:@"Helper process id: %d\n", pid];
              }
            });
    } else if ([extension respondsToSelector:beginError]) {
        NSError* beginFailure = nil;
        NSUUID* request = ((id(*)(id, SEL, NSArray*, NSError**))objc_msgSend)(
            extension, beginError, @[ item ], &beginFailure);
        [log appendFormat:@"Started, request %@ %@\n", request,
                          beginFailure.localizedDescription ?: @""];
    } else {
        [log appendString:@"No way to start the extension was found.\n"];
        finish(nil);
        return;
    }

    // Nothing came back: say so instead of leaving the screen waiting.
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(timeout * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
                     if (!answered) {
                         answered = YES;
                         [log appendFormat:@"No reply from the helper within %.0f seconds.\n", timeout];
                         finish(nil);
                     }
                   });
}

+ (void)enableJITWithCompletion:(void (^)(BOOL, NSString*))completion {
    if ([AZEmulator isJITAvailable]) {
        completion(YES, @"JIT is already enabled.");
        return;
    }
    NSArray<NSData*>* files = [self pairingFiles];
    if (files.count == 0) {
        completion(NO, @"No pairing file. Import one first.");
        return;
    }
    NSArray<NSNumber*>* ranges = @[];
    const BOOL needsMemory = [AZEmulator deviceNeedsPreparedMemory];
    if (needsMemory) {
        std::size_t size = 0;
        void* base = Common::AppleJIT::ReserveHelperArena(&size);
        if (base == nullptr) {
            completion(NO, @"Executable memory could not be reserved.");
            return;
        }
        ranges = @[ @((unsigned long long)(uintptr_t)base), @((unsigned long long)size) ];
    }
    [self prepareWithFiles:files index:0 ranges:ranges needsMemory:needsMemory
                       log:[NSMutableString string] completion:completion];
}

+ (void)prepareWithFiles:(NSArray<NSData*>*)files
                   index:(NSUInteger)index
                  ranges:(NSArray<NSNumber*>*)ranges
             needsMemory:(BOOL)needsMemory
                     log:(NSMutableString*)total
              completion:(void (^)(BOOL, NSString*))completion {
    NSDictionary* request = @{
        @"op" : @"prepare",
        @"pairing" : files[index],
        @"targetPid" : @(getpid()),
        @"ranges" : ranges,
        @"timeoutSeconds" : @120,
    };
    [self runWithInfo:request
           completion:^(NSDictionary* reply, NSString* log) {
             NSString* status = reply[@"status"];
             [total appendFormat:@"%@File %lu of %lu: %@ %@\n", log, (unsigned long)index + 1,
                                 (unsigned long)files.count, status ?: @"no reply",
                                 reply[@"detail"] ?: @""];
             if (reply != nil) {
                 [total appendFormat:@"%@\n", reply];
             }
             if ([status isEqualToString:@"prepared"]) {
                 if (needsMemory) {
                     Common::AppleJIT::MarkHelperArenaPrepared();
                 }
                 const BOOL enabled = [AZEmulator isJITAvailable];
                 [total appendString:enabled ? @"JIT is enabled.\n"
                                             : @"The helper finished, but the core did not "
                                               @"find JIT available.\n"];
                 completion(enabled, total);
                 return;
             }
             const BOOL refused = [status isEqualToString:@"tunnel-failed"] ||
                                  [status isEqualToString:@"bad-pairing"];
             if (refused && index + 1 < files.count) {
                 [self prepareWithFiles:files index:index + 1 ranges:ranges
                            needsMemory:needsMemory log:total completion:completion];
                 return;
             }
             completion(NO, total);
           }];
}

+ (void)testWithCompletion:(void (^)(NSString* report))completion {
    [self runWithInfo:@{@"op" : @"ping"}
           completion:^(NSDictionary* reply, NSString* log) {
             completion([NSString stringWithFormat:@"%@Reply: %@\n", log, reply ?: @"(none)"]);
           }];
}

+ (void)checkConnectionWithCompletion:(void (^)(NSString* report))completion {
    NSArray<NSData*>* files = [self pairingFiles];
    if (files.count == 0) {
        completion(@"No pairing file. Import one first.");
        return;
    }
    [self checkPairingFiles:files index:0 log:[NSMutableString string] completion:completion];
}

/// Tries the pairing files one after the other while the device refuses the one before.
+ (void)checkPairingFiles:(NSArray<NSData*>*)files
                    index:(NSUInteger)index
                      log:(NSMutableString*)total
               completion:(void (^)(NSString*))completion {
    [self runWithInfo:@{@"op" : @"check", @"pairing" : files[index]}
           completion:^(NSDictionary* reply, NSString* log) {
             NSString* status = reply[@"status"];
             [total appendFormat:@"%@File %lu of %lu: %@ %@\n%@\n", log,
                                 (unsigned long)index + 1, (unsigned long)files.count,
                                 status ?: @"no reply", reply[@"detail"] ?: @"",
                                 reply ? [NSString stringWithFormat:@"%@", reply] : @""];
             const BOOL refused = [status isEqualToString:@"tunnel-failed"] ||
                                  [status isEqualToString:@"bad-pairing"];
             if (refused && index + 1 < files.count) {
                 [self checkPairingFiles:files index:index + 1 log:total completion:completion];
                 return;
             }
             NSString* summary = @"";
             if ([status isEqualToString:@"ready"]) {
                 summary = @"Ready: the debug service is reachable.\n";
             } else if ([status isEqualToString:@"connect-failed"]) {
                 summary = @"The device's loopback network does not answer (10.7.0.1). Turn on "
                           @"the VPN of the debugger app (StikDebug) and try again.\n";
             } else if ([status isEqualToString:@"ddi-not-mounted"]) {
                 summary = @"Connected, but the developer disk image is not mounted.\n";
             } else if ([status isEqualToString:@"tunnel-failed"]) {
                 summary = @"The device refused the pairing file.\n";
             }
             completion([summary stringByAppendingString:total]);
           }];
}

@end
