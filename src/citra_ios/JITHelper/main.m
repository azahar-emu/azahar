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

#if AZ_HAVE_IDEVICE
// A symbol of the library. Weak, so a library without it shows up in the report instead of
// failing the link. (dlsym can't be used: symbols of an app extension's own binary are not
// exported.)
extern int rp_pairing_file_read(void) __attribute__((weak_import));
#endif

@interface AZJITHelperHandler : NSObject <NSExtensionRequestHandling>
@end

@implementation AZJITHelperHandler

- (void)beginRequestWithExtensionContext:(NSExtensionContext*)context {
#if AZ_HAVE_IDEVICE
    const BOOL compiledWith = YES;
    const BOOL linked = (void*)&rp_pairing_file_read != NULL;
#else
    const BOOL compiledWith = NO;
    const BOOL linked = NO;
#endif

    NSExtensionItem* request = (NSExtensionItem*)context.inputItems.firstObject;
    id hostPid = request.userInfo[@"hostPid"];

    NSExtensionItem* reply = [[NSExtensionItem alloc] init];
    reply.userInfo = @{
        @"status" : @"started",
        @"pid" : @(getpid()),
        @"hostPid" : hostPid ?: @(0),
        @"ideviceFoundAtBuild" : @(compiledWith),
        @"ideviceLinked" : @(linked),
    };
    [context completeRequestReturningItems:@[ reply ] completionHandler:nil];
}

@end
