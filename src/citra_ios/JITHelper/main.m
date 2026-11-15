// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

// The JIT helper: an app extension that runs in a process of its own. Enabling JIT needs a
// debugger attached to the app, and an app cannot attach to itself, so the app starts this
// extension and the extension does the debugging from outside.
//
// This first version only answers: it reports that it started, its process id and whether the
// idevice library (the part that talks to the debug service) is linked in.

#import <Foundation/Foundation.h>
#include <dlfcn.h>
#include <unistd.h>

@interface AZJITHelperHandler : NSObject <NSExtensionRequestHandling>
@end

@implementation AZJITHelperHandler

- (void)beginRequestWithExtensionContext:(NSExtensionContext*)context {
    // A symbol of the library: it is linked in with -force_load, or not at all.
    const BOOL linked = dlsym(RTLD_DEFAULT, "rp_pairing_file_read") != NULL;

    NSExtensionItem* request = (NSExtensionItem*)context.inputItems.firstObject;
    id hostPid = request.userInfo[@"hostPid"];

    NSExtensionItem* reply = [[NSExtensionItem alloc] init];
    reply.userInfo = @{
        @"status" : @"started",
        @"pid" : @(getpid()),
        @"hostPid" : hostPid ?: @(0),
        @"ideviceLinked" : @(linked),
    };
    [context completeRequestReturningItems:@[ reply ] completionHandler:nil];
}

@end
