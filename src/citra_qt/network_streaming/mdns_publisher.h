// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <string>
#include <thread>
#include "common/common_types.h"

namespace NetworkStreaming {

/// Advertises a TCP service over mDNS/DNS-SD (Zeroconf) so the Android viewer app's NSD-based
/// discovery can find this machine on the LAN with no manual IP entry - the Windows counterpart
/// to AvahiPublisher (Linux) and NsdManager.registerService() (Android).
///
/// Unlike AvahiPublisher, this does not depend on any system daemon or external library (no
/// Bonjour/mDNSResponder, no WinRT DNS-SD) - it is a small, hand-rolled mDNS responder built
/// directly on WinSock2, since Windows has no built-in equivalent to Avahi that this project can
/// rely on being present. It only ever answers questions about its own single service; it is not
/// a general-purpose mDNS responder.
class MdnsPublisher {
public:
    MdnsPublisher();
    ~MdnsPublisher();

    void Start(const std::string& service_type, u16 port);
    void Stop();

private:
    void ResponderLoop();

    std::atomic_bool running{false};
    std::thread responder_thread;
    unsigned long long sock; // SOCKET; stored widened, see net_compat.h's SocketHandle rationale.

    std::string service_fqdn;  ///< e.g. "_azahar._tcp.local" - PTR question target.
    std::string instance_fqdn; ///< e.g. "Azahar-DESKTOP._azahar._tcp.local" - PTR answer, SRV/TXT owner.
    std::string host_fqdn;     ///< e.g. "Azahar-DESKTOP.local" - SRV target and A record owner.
    u16 port = 0;
    u32 local_ipv4_be = 0; ///< Network-byte-order IPv4, ready to drop into the A record's rdata.
};

} // namespace NetworkStreaming
