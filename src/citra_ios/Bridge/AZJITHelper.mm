// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

// Starts the JIT helper app extension (see JITHelper/main.m). The extension API for starting an
// extension from its own app is private, so the selectors are looked up at run time and every
// step reports what was found, which is what this first version is for.
//
// The way of starting the extension follows the launcher of ARMSX3 and DolphiniOS (GPL-2.0-or-
// later, https://github.com/OatmealDome/dolphin-ios pull request 277).

#import <AzaharCore/AZJITHelper.h>
#include <unistd.h>
#import <objc/message.h>
#import <objc/runtime.h>

@implementation AZJITHelper

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

+ (void)testWithCompletion:(void (^)(NSString* report))completion {
    NSMutableString* report = [NSMutableString string];
    void (^finish)(void) = ^{
      dispatch_async(dispatch_get_main_queue(), ^{
        completion([report copy]);
      });
    };

    NSString* identifier = [self helperIdentifier];
    if (identifier == nil) {
        [report appendString:@"The app has no helper extension (PlugIns folder is empty)."];
        finish();
        return;
    }
    [report appendFormat:@"Helper extension: %@\n", identifier];

    Class extensionClass = NSClassFromString(@"NSExtension");
    SEL lookup = NSSelectorFromString(@"extensionWithIdentifier:excludingDisabledExtensions:error:");
    if (extensionClass == Nil || ![extensionClass respondsToSelector:lookup]) {
        [report appendString:@"The system has no way to look the extension up (selector gone).\n"];
        finish();
        return;
    }
    NSError* error = nil;
    id extension = ((id(*)(Class, SEL, NSString*, BOOL, NSError**))objc_msgSend)(
        extensionClass, lookup, identifier, NO, &error);
    if (extension == nil) {
        [report appendFormat:@"The system does not know the extension: %@\n",
                             error.localizedDescription ?: @"no error given"];
        finish();
        return;
    }

    NSExtensionItem* item = [[NSExtensionItem alloc] init];
    item.userInfo = @{@"hostPid" : @(getpid())};

    SEL setCompletion = NSSelectorFromString(@"setRequestCompletionBlock:");
    SEL beginCompletion = NSSelectorFromString(@"beginExtensionRequestWithInputItems:completion:");
    SEL beginError = NSSelectorFromString(@"beginExtensionRequestWithInputItems:error:");
    SEL pidFor = NSSelectorFromString(@"pidForRequestIdentifier:");
    SEL kill = NSSelectorFromString(@"_kill:");
    [report appendFormat:@"Selectors: setRequestCompletionBlock %d, begin..completion %d, "
                         @"begin..error %d, pidForRequestIdentifier %d\n",
                         [extension respondsToSelector:setCompletion],
                         [extension respondsToSelector:beginCompletion],
                         [extension respondsToSelector:beginError],
                         [extension respondsToSelector:pidFor]];

    __block BOOL answered = NO;
    void (^onReply)(NSUUID*, NSArray*) = ^(NSUUID* request, NSArray* items) {
      if (answered) {
          return;
      }
      answered = YES;
      NSExtensionItem* first = items.firstObject;
      [report appendFormat:@"Reply: %@\n", first.userInfo ?: @"(none)"];
      if ([extension respondsToSelector:kill] && request != nil) {
          ((void (*)(id, SEL, NSUUID*))objc_msgSend)(extension, kill, request);
      }
      finish();
    };
    if ([extension respondsToSelector:setCompletion]) {
        ((void (*)(id, SEL, id))objc_msgSend)(extension, setCompletion, onReply);
    }

    if ([extension respondsToSelector:beginCompletion]) {
        ((void (*)(id, SEL, NSArray*, id))objc_msgSend)(
            extension, beginCompletion, @[ item ], ^(NSUUID* request) {
              [report appendFormat:@"Started, request %@\n", request];
              if ([extension respondsToSelector:pidFor] && request != nil) {
                  const int pid = ((int (*)(id, SEL, NSUUID*))objc_msgSend)(extension, pidFor,
                                                                            request);
                  [report appendFormat:@"Helper process id: %d\n", pid];
              }
            });
    } else if ([extension respondsToSelector:beginError]) {
        NSError* beginFailure = nil;
        NSUUID* request = ((id(*)(id, SEL, NSArray*, NSError**))objc_msgSend)(
            extension, beginError, @[ item ], &beginFailure);
        [report appendFormat:@"Started, request %@ %@\n", request,
                             beginFailure.localizedDescription ?: @""];
        if ([extension respondsToSelector:pidFor] && request != nil) {
            const int pid =
                ((int (*)(id, SEL, NSUUID*))objc_msgSend)(extension, pidFor, request);
            [report appendFormat:@"Helper process id: %d\n", pid];
        }
    } else {
        [report appendString:@"No way to start the extension was found.\n"];
        finish();
        return;
    }

    // Nothing came back: say so instead of leaving the screen waiting.
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 8 * NSEC_PER_SEC),
                   dispatch_get_main_queue(), ^{
                     if (!answered) {
                         answered = YES;
                         [report appendString:@"No reply from the helper within 8 seconds.\n"];
                         finish();
                     }
                   });
}

@end
