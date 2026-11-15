// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

// The JIT helper: an app extension that runs in a process of its own. Enabling JIT needs a
// debugger attached to the app, and an app cannot attach to itself, so the app starts this
// extension and the extension does the debugging from outside, through the device's own debug
// service (reached over the loopback VPN of the device with a pairing file).
//
// The way this is done follows ARMSX3 and DolphiniOS (GPL-2.0-or-later,
// https://github.com/OatmealDome/dolphin-ios pull request 277); the library for the device
// protocols is idevice (MIT, https://github.com/jkcoxson/idevice).
//
// A request carries "op": "ping" (answers), or "check" with the bytes of a pairing file
// ("pairing"): opens the tunnel and asks whether the debug service is there. The reply holds a
// "status" word and, when something failed, a "detail" text.

#import <Foundation/Foundation.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#if AZ_HAVE_IDEVICE
#include "idevice.h"
// Weak, so a library without this function shows up in the report instead of failing the link.
// (dlsym can't be used: symbols of an app extension's own binary are not exported.)
#pragma weak rp_pairing_file_from_bytes
#endif

// The address and port of the device's remote service discovery on the loopback VPN.
static const char* const kRSDAddress = "10.7.0.1";
static const uint16_t kRSDPort = 49152;

#if AZ_HAVE_IDEVICE

static NSString* ErrorText(IdeviceFfiError* error) {
    if (error == NULL) {
        return @"";
    }
    NSString* text = [NSString stringWithFormat:@"%d/%d %s", (int)error->code,
                                                (int)error->sub_code,
                                                error->message ? error->message : ""];
    idevice_error_free(error);
    return text;
}

/// Whether something answers on the address within the time (milliseconds).
static BOOL ProbeTCP(const char* address, uint16_t port, int timeoutMs) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return NO;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, address, &addr.sin_addr);
    BOOL ok = NO;
    const int result = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (result == 0) {
        ok = YES;
    } else if (errno == EINPROGRESS) {
        struct pollfd pfd = {.fd = fd, .events = POLLOUT};
        if (poll(&pfd, 1, timeoutMs) > 0) {
            int error = 0;
            socklen_t length = sizeof(error);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length);
            ok = error == 0;
        }
    }
    close(fd);
    return ok;
}

/// Opens the tunnel with the pairing file and asks whether the debug proxy service is there.
static NSDictionary* CheckConnection(NSData* pairing) {
    if (pairing.length == 0 || pairing.length > 65536) {
        return @{@"status" : @"no-pairing"};
    }
    if (rp_pairing_file_from_bytes == NULL) {
        return @{@"status" : @"library-missing"};
    }
    RpPairingFileHandle* file = NULL;
    IdeviceFfiError* error = rp_pairing_file_from_bytes(pairing.bytes, pairing.length, &file);
    if (error != NULL) {
        return @{@"status" : @"bad-pairing", @"detail" : ErrorText(error)};
    }

    NSDictionary* result = nil;
    if (!ProbeTCP(kRSDAddress, kRSDPort, 1500)) {
        // Nothing answers: the VPN of the debugger app (or the loopback network) is not up.
        rp_pairing_file_free(file);
        return @{@"status" : @"connect-failed"};
    }

    idevice_set_global_timeout(5);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kRSDPort);
    inet_pton(AF_INET, kRSDAddress, &addr.sin_addr);

    AdapterHandle* adapter = NULL;
    RsdHandshakeHandle* handshake = NULL;
    error = tunnel_create_rppairing((const idevice_sockaddr*)&addr, sizeof(addr), "Azahar", file,
                                    NULL, NULL, &adapter, &handshake);
    if (error != NULL) {
        result = @{@"status" : @"tunnel-failed", @"detail" : ErrorText(error)};
    } else {
        bool available = false;
        error = rsd_service_available(handshake, "com.apple.internal.dt.remote.debugproxy",
                                      &available);
        if (error != NULL) {
            result = @{@"status" : @"service-check-failed", @"detail" : ErrorText(error)};
        } else if (!available) {
            // The developer disk image is not mounted: the debug service is not offered.
            result = @{@"status" : @"ddi-not-mounted"};
        } else {
            result = @{@"status" : @"ready"};
        }
    }
    if (handshake != NULL) {
        rsd_handshake_free(handshake);
    }
    if (adapter != NULL) {
        adapter_free(adapter);
    }
    rp_pairing_file_free(file);
    return result;
}

#endif // AZ_HAVE_IDEVICE

@interface AZJITHelperHandler : NSObject <NSExtensionRequestHandling>
@end

@implementation AZJITHelperHandler

- (void)beginRequestWithExtensionContext:(NSExtensionContext*)context {
    NSExtensionItem* request = (NSExtensionItem*)context.inputItems.firstObject;
    NSDictionary* info = request.userInfo;
    NSString* operation = info[@"op"] ?: @"ping";

#if AZ_HAVE_IDEVICE
    const BOOL compiledWith = YES;
    const BOOL linked = (void*)&rp_pairing_file_from_bytes != NULL;
#else
    const BOOL compiledWith = NO;
    const BOOL linked = NO;
#endif

    NSMutableDictionary* answer = [NSMutableDictionary dictionaryWithDictionary:@{
        @"status" : @"started",
        @"pid" : @(getpid()),
        @"hostPid" : info[@"hostPid"] ?: @(0),
        @"ideviceFoundAtBuild" : @(compiledWith),
        @"ideviceLinked" : @(linked),
    }];
#if AZ_HAVE_IDEVICE
    if ([operation isEqualToString:@"check"]) {
        [answer addEntriesFromDictionary:CheckConnection(info[@"pairing"])];
    }
#endif

    NSExtensionItem* reply = [[NSExtensionItem alloc] init];
    reply.userInfo = answer;
    [context completeRequestReturningItems:@[ reply ] completionHandler:nil];
}

@end
