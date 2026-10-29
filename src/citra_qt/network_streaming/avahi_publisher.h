// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>
#include "common/common_types.h"

struct AvahiClient;
struct AvahiEntryGroup;
struct AvahiThreadedPoll;

namespace NetworkStreaming {

/// Advertises a TCP service over mDNS/DNS-SD (Zeroconf) via Avahi, so the Android viewer app's
/// NSD-based discovery can find this machine on the LAN with no manual IP entry - the desktop
/// equivalent of Android's NsdManager.registerService() call in NetworkStreamer.kt.
///
/// Uses Avahi's "threaded poll" helper, which owns and runs its own background thread for the
/// mDNS event loop - no manual thread/poll-loop plumbing needed here.
class AvahiPublisher {
public:
    AvahiPublisher();
    ~AvahiPublisher();

    void Start(const std::string& service_type, u16 port);
    void Stop();

    /// Called by the Avahi client callback once the connection to the daemon is ready.
    void RegisterService();

    // Set directly by the (free-function) Avahi client callback, which can fire synchronously
    // from within avahi_client_new() - before Start() gets a chance to store its return value.
    AvahiClient* client = nullptr;

private:
    std::string service_type;
    u16 port = 0;

    AvahiThreadedPoll* threaded_poll = nullptr;
    AvahiEntryGroup* group = nullptr;
};

} // namespace NetworkStreaming
