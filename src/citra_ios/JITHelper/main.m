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
// A request carries "op": "ping" (answers), "check" with the bytes of a pairing file
// ("pairing"): opens the tunnel and asks whether the debug service is there, or "prepare": the
// same, then attaches to the app ("pid") as a debugger, writes one byte to every 16 KiB page of
// the given ranges ("ranges": base, size, base, size, ...; none on a device without TXM, where
// attaching alone is enough) and detaches. TXM lets a page run code only after a debugger wrote
// it. The reply holds a "status" word and, when something failed, a "detail" text.

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

/// Opens the tunnel with the pairing file, checks that the debug proxy service is offered and
/// calls the block with the tunnel (nil block: only the check).
static NSDictionary* ConnectAndRun(NSData* pairing,
                                   NSDictionary* (^run)(AdapterHandle*, RsdHandshakeHandle*)) {
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
        } else if (run != nil) {
            result = run(adapter, handshake);
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

static NSDictionary* CheckConnection(NSData* pairing) {
    return ConnectAndRun(pairing, nil);
}

// MARK: Debug protocol

/// A debugserver packet: $body#checksum.
static NSData* Packet(NSString* body) {
    NSData* bytes = [body dataUsingEncoding:NSASCIIStringEncoding];
    uint8_t sum = 0;
    const uint8_t* p = bytes.bytes;
    for (NSUInteger i = 0; i < bytes.length; i++) {
        sum += p[i];
    }
    return [[NSString stringWithFormat:@"$%@#%02x", body, sum] dataUsingEncoding:NSASCIIStringEncoding];
}

/// Sends a packet and reads the answer (an empty string when the proxy gave none). NO when the
/// sending or reading failed.
static BOOL Exchange(DebugProxyHandle* proxy, NSString* body, NSString** answer,
                     NSString** detail) {
    NSData* packet = Packet(body);
    IdeviceFfiError* error = debug_proxy_send_raw(proxy, packet.bytes, packet.length);
    if (error != NULL) {
        *detail = ErrorText(error);
        return NO;
    }
    char* response = NULL;
    error = debug_proxy_read_response(proxy, &response);
    if (error != NULL) {
        *detail = ErrorText(error);
        return NO;
    }
    *answer = response ? [NSString stringWithUTF8String:response] : @"";
    if (response != NULL) {
        idevice_string_free(response);
    }
    return YES;
}

/// Attaches to the process, writes one byte to every page of the ranges, detaches.
static NSDictionary* AttachAndPrepare(DebugProxyHandle* proxy, int pid, NSArray<NSNumber*>* ranges) {
    NSString* answer = nil;
    NSString* detail = nil;

    // Acks are switched off first, the way debuggers do it.
    debug_proxy_send_ack(proxy);
    if (!Exchange(proxy, @"QStartNoAckMode", &answer, &detail)) {
        return @{@"status" : @"attach-failed", @"detail" : [@"QStartNoAckMode: " stringByAppendingString:detail]};
    }
    debug_proxy_set_ack_mode(proxy, 0);

    if (!Exchange(proxy, [NSString stringWithFormat:@"vAttach;%x", pid], &answer, &detail)) {
        return @{@"status" : @"attach-failed", @"detail" : detail};
    }
    if (answer.length == 0 || [answer hasPrefix:@"E"]) {
        return @{@"status" : @"attach-failed", @"detail" : [@"vAttach answered: " stringByAppendingString:answer]};
    }

    // One byte (0x69) per 16 KiB page, through the debugger (writing it from the app itself
    // would kill the process). 512 packets are sent, then their answers are read (fewer round trips than one by one).
    const uint64_t page = 16384;
    const NSUInteger batchSize = 512;
    uint64_t pages = 0, written = 0;
    for (NSUInteger r = 0; r + 1 < ranges.count; r += 2) {
        const uint64_t base = ranges[r].unsignedLongLongValue;
        const uint64_t size = ranges[r + 1].unsignedLongLongValue;
        if (base == 0 || base % page != 0 || size == 0 || size % page != 0) {
            Exchange(proxy, @"D", &answer, &detail);
            return @{@"status" : @"invalid-range"};
        }
        uint64_t offset = 0;
        while (offset < size) {
            NSUInteger inBatch = 0;
            for (; inBatch < batchSize && offset < size; inBatch++, offset += page) {
                NSData* packet = Packet([NSString stringWithFormat:@"M%llx,1:69", base + offset]);
                IdeviceFfiError* error = debug_proxy_send_raw(proxy, packet.bytes, packet.length);
                if (error != NULL) {
                    detail = ErrorText(error);
                    Exchange(proxy, @"D", &answer, &detail);
                    return @{@"status" : @"prepare-failed", @"detail" : detail, @"pages" : @(pages), @"written" : @(written)};
                }
                pages++;
            }
            for (NSUInteger i = 0; i < inBatch; i++) {
                char* response = NULL;
                IdeviceFfiError* error = debug_proxy_read_response(proxy, &response);
                if (error != NULL) {
                    detail = ErrorText(error);
                    return @{@"status" : @"prepare-failed", @"detail" : detail, @"pages" : @(pages), @"written" : @(written)};
                }
                if (response != NULL) {
                    if (strstr(response, "OK") != NULL) {
                        written++;
                    }
                    idevice_string_free(response);
                }
            }
        }
    }

    BOOL detached = Exchange(proxy, @"D", &answer, &detail);
    if (written != pages) {
        return @{@"status" : @"prepare-incomplete", @"pages" : @(pages), @"written" : @(written)};
    }
    if (!detached) {
        return @{@"status" : @"detach-failed", @"detail" : detail ?: @"", @"pages" : @(pages)};
    }
    return @{@"status" : @"prepared", @"pages" : @(pages), @"written" : @(written)};
}

/// The whole job of a "prepare" request: the tunnel, then the debugger side.
static NSDictionary* Prepare(NSData* pairing, int pid, NSArray<NSNumber*>* ranges) {
    if (pid <= 0) {
        return @{@"status" : @"invalid-request"};
    }
    // CheckConnection with the debug side attached to its tunnel.
    return ConnectAndRun(pairing, ^NSDictionary*(AdapterHandle* adapter, RsdHandshakeHandle* handshake) {
      // The remote server service is opened first, like the other helpers do; when it
      // refuses, the debug proxy may still work.
      RemoteServerHandle* server = NULL;
      NSString* serverDetail = @"";
      IdeviceFfiError* error = remote_server_connect_rsd(adapter, handshake, &server);
      if (error != NULL) {
          serverDetail = ErrorText(error);
      }
      DebugProxyHandle* proxy = NULL;
      error = debug_proxy_connect_rsd(adapter, handshake, &proxy);
      NSDictionary* result;
      if (error != NULL) {
          result = @{@"status" : @"debug-proxy-failed", @"detail" : ErrorText(error), @"serverDetail" : serverDetail};
      } else {
          result = AttachAndPrepare(proxy, pid, ranges);
          debug_proxy_free(proxy);
      }
      if (server != NULL) {
          remote_server_free(server);
      }
      return result;
    });
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
    } else if ([operation isEqualToString:@"prepare"]) {
        [answer addEntriesFromDictionary:Prepare(info[@"pairing"], [info[@"targetPid"] intValue],
                                                 info[@"ranges"] ?: @[])];
    }
#endif

    NSExtensionItem* reply = [[NSExtensionItem alloc] init];
    reply.userInfo = answer;
    [context completeRequestReturningItems:@[ reply ] completionHandler:nil];
}

@end
