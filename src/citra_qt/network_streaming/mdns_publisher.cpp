// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// winsock2.h must be the very first include in this translation unit - see net_compat.h's
// comment on why (same precedent as core/gdbstub/gdbstub.cpp).
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <string_view>
#include <vector>

#include "citra_qt/network_streaming/mdns_publisher.h"
#include "common/logging/log.h"
#include "network/socket_manager.h"

namespace NetworkStreaming {

namespace {

// --- mDNS/DNS wire-format constants -----------------------------------------------------

constexpr u16 DnsTypeA = 1;
constexpr u16 DnsTypePtr = 12;
constexpr u16 DnsTypeTxt = 16;
constexpr u16 DnsTypeSrv = 33;
constexpr u16 DnsTypeAny = 255;
constexpr u16 DnsClassIn = 1;

constexpr u16 FlagQr = 0x8000; ///< Query/Response bit in the header's flags field.
// RFC 6762 5.4: top bit of a question's QCLASS is the "unicast response preferred" bit, not
// part of the class value itself - must be masked off before comparing to DnsClassIn.
constexpr u16 QuBitMask = 0x8000;
// RFC 6762 10.2: set on our SRV/TXT/A answers (unique records) so caches can flush stale data;
// left unset on PTR answers, since a PTR name can legitimately have more than one instance.
constexpr u16 CacheFlushBit = 0x8000;

constexpr u32 PtrTtlSeconds = 4500; // RFC 6762 recommendation for "shared" records.
constexpr u32 UniqueTtlSeconds = 120; // RFC 6762 recommendation for "unique" records (SRV/TXT/A).

constexpr int kMdnsPort = 5353;
constexpr const char* kMdnsGroup = "224.0.0.251";

enum AnswerPlan : u32 {
    AnswerNone = 0,
    AnswerPtr = 1u << 0,
    AnswerSrv = 1u << 1,
    AnswerTxt = 1u << 2,
    AnswerA = 1u << 3,
};

// --- Small big-endian wire helpers -------------------------------------------------------
// Always explicit byte-by-byte shifts - never pointer-cast a multi-byte field over the raw
// buffer (alignment/UB risk on both read and write sides).

void AppendU16BE(std::vector<u8>& out, u16 value) {
    out.push_back(static_cast<u8>((value >> 8) & 0xFF));
    out.push_back(static_cast<u8>(value & 0xFF));
}

void AppendU32BE(std::vector<u8>& out, u32 value) {
    out.push_back(static_cast<u8>((value >> 24) & 0xFF));
    out.push_back(static_cast<u8>((value >> 16) & 0xFF));
    out.push_back(static_cast<u8>((value >> 8) & 0xFF));
    out.push_back(static_cast<u8>(value & 0xFF));
}

void AppendRaw(std::vector<u8>& out, const void* data, std::size_t size) {
    const u8* p = static_cast<const u8*>(data);
    out.insert(out.end(), p, p + size);
}

u16 ReadU16BE(const u8* buf, std::size_t offset) {
    return static_cast<u16>((static_cast<u16>(buf[offset]) << 8) | buf[offset + 1]);
}

// Encodes a dotted name ("azahar-pc._azahar._tcp.local") as length-prefixed labels terminated
// by a zero byte. We never emit compressed names - only an optimization, not required for
// correctness - so this is unconditionally safe/simple.
void EncodeName(std::vector<u8>& out, std::string_view dotted_name) {
    std::size_t start = 0;
    while (start <= dotted_name.size()) {
        std::size_t dot = dotted_name.find('.', start);
        const std::size_t end = (dot == std::string_view::npos) ? dotted_name.size() : dot;
        const std::size_t len = end - start;
        if (len > 0) {
            out.push_back(static_cast<u8>(len)); // len <= 63 guaranteed by our own fixed names.
            AppendRaw(out, dotted_name.data() + start, len);
        }
        if (dot == std::string_view::npos) {
            break;
        }
        start = dot + 1;
    }
    out.push_back(0);
}

std::string Lowercase(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Bounds-checked DNS name decoder with capped compression-pointer following (RFC 1035 4.1.4).
// `offset` is advanced past only the bytes the name occupies *at its original location* in the
// packet - even if a compression pointer redirects the read elsewhere, so the caller can
// correctly continue parsing QTYPE/QCLASS (or the next record) right after this name.
bool DecodeName(const u8* buf, std::size_t size, std::size_t& offset, std::string& out) {
    out.clear();
    std::size_t cursor = offset;
    std::size_t consumed_end = static_cast<std::size_t>(-1);
    int jumps = 0;
    int labels = 0;

    for (;;) {
        if (labels++ > 128) {
            return false; // Defense in depth against pathological/hostile input.
        }
        if (cursor >= size) {
            return false;
        }
        const u8 len = buf[cursor];
        if (len == 0) {
            cursor += 1;
            if (consumed_end == static_cast<std::size_t>(-1)) {
                consumed_end = cursor;
            }
            break;
        }
        if ((len & 0xC0) == 0xC0) {
            if (cursor + 1 >= size) {
                return false;
            }
            const std::size_t pointer =
                (static_cast<std::size_t>(len & 0x3F) << 8) | buf[cursor + 1];
            if (consumed_end == static_cast<std::size_t>(-1)) {
                consumed_end = cursor + 2;
            }
            if (pointer >= cursor) {
                return false; // Must point strictly backward - rejects forward/self pointers.
            }
            if (++jumps > 1) {
                return false; // One jump is all a well-formed packet of ours ever needs.
            }
            cursor = pointer;
            continue;
        }
        if (len > 63) {
            return false; // Invalid label length (top two bits already handled above).
        }
        if (cursor + 1 + len > size) {
            return false;
        }
        if (!out.empty()) {
            out += '.';
        }
        out.append(reinterpret_cast<const char*>(buf + cursor + 1), len);
        cursor += 1 + len;
    }

    offset = consumed_end;
    return true;
}

void AppendAnswerRecord(std::vector<u8>& out, std::string_view owner_name, u16 type, u16 dns_class,
                         u32 ttl, const std::vector<u8>& rdata) {
    EncodeName(out, owner_name);
    AppendU16BE(out, type);
    AppendU16BE(out, dns_class);
    AppendU32BE(out, ttl);
    AppendU16BE(out, static_cast<u16>(rdata.size()));
    AppendRaw(out, rdata.data(), rdata.size());
}

std::vector<u8> BuildPtrRdata(std::string_view instance_fqdn) {
    std::vector<u8> rdata;
    EncodeName(rdata, instance_fqdn);
    return rdata;
}

std::vector<u8> BuildSrvRdata(u16 port, std::string_view host_fqdn) {
    std::vector<u8> rdata;
    AppendU16BE(rdata, 0); // priority
    AppendU16BE(rdata, 0); // weight
    AppendU16BE(rdata, port);
    EncodeName(rdata, host_fqdn);
    return rdata;
}

std::vector<u8> BuildTxtRdata() {
    // RFC 6763 6.1: an "empty" TXT record must still contain one zero-length character-string,
    // i.e. RDLENGTH=1, RDATA={0x00} - not RDLENGTH=0 - some clients reject a truly empty RDATA.
    return {0x00};
}

std::vector<u8> BuildARdata(u32 ipv4_network_order) {
    std::vector<u8> rdata(4);
    std::memcpy(rdata.data(), &ipv4_network_order, 4);
    return rdata;
}

std::vector<u8> BuildReply(u32 plan, const std::string& service_fqdn,
                            const std::string& instance_fqdn, const std::string& host_fqdn,
                            u16 port, u32 local_ipv4_be) {
    u16 ancount = 0;
    if (plan & AnswerPtr) {
        ancount++;
    }
    if (plan & AnswerSrv) {
        ancount++;
    }
    if (plan & AnswerTxt) {
        ancount++;
    }
    if (plan & AnswerA) {
        ancount++;
    }

    std::vector<u8> out;
    AppendU16BE(out, 0);          // ID
    AppendU16BE(out, 0x8400);     // Flags: QR=1 (response), AA=1 (authoritative)
    AppendU16BE(out, 0);          // QDCOUNT
    AppendU16BE(out, ancount);    // ANCOUNT - fold everything in here; section placement beyond
                                  // "an answer" doesn't matter to the queriers we target.
    AppendU16BE(out, 0);          // NSCOUNT
    AppendU16BE(out, 0);          // ARCOUNT

    if (plan & AnswerPtr) {
        AppendAnswerRecord(out, service_fqdn, DnsTypePtr, DnsClassIn, PtrTtlSeconds,
                           BuildPtrRdata(instance_fqdn));
    }
    if (plan & AnswerSrv) {
        AppendAnswerRecord(out, instance_fqdn, DnsTypeSrv, DnsClassIn | CacheFlushBit,
                           UniqueTtlSeconds, BuildSrvRdata(port, host_fqdn));
    }
    if (plan & AnswerTxt) {
        AppendAnswerRecord(out, instance_fqdn, DnsTypeTxt, DnsClassIn | CacheFlushBit,
                           UniqueTtlSeconds, BuildTxtRdata());
    }
    if (plan & AnswerA) {
        AppendAnswerRecord(out, host_fqdn, DnsTypeA, DnsClassIn | CacheFlushBit, UniqueTtlSeconds,
                           BuildARdata(local_ipv4_be));
    }
    return out;
}

std::string MakeServiceName() {
    char host[256] = "azahar";
    gethostname(host, sizeof(host) - 1);
    host[sizeof(host) - 1] = '\0';
    return std::string("Azahar-") + host;
}

// Determines this machine's outbound LAN IPv4 by "connecting" a UDP socket to a well-known
// external address and reading back the local address the OS picked - no packet is actually
// sent (UDP connect() only records a default peer). Deliberately simpler than enumerating
// adapters via GetAdaptersAddresses/iphlpapi (which would need real logic to skip
// down/loopback/APIPA/VPN-only adapters) - mirrors how AvahiPublisher itself never hand-picks
// an interface/address either, just lets the resolver figure out "the" address.
u32 DiscoverLocalIPv4() {
    const SOCKET probe = socket(AF_INET, SOCK_DGRAM, 0);
    if (probe == INVALID_SOCKET) {
        return 0;
    }
    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &remote.sin_addr);
    if (connect(probe, reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) != 0) {
        closesocket(probe);
        return 0;
    }
    sockaddr_in local{};
    int len = sizeof(local);
    if (getsockname(probe, reinterpret_cast<sockaddr*>(&local), &len) != 0) {
        closesocket(probe);
        return 0;
    }
    closesocket(probe);
    return local.sin_addr.s_addr; // Already network-byte-order.
}

} // namespace

MdnsPublisher::MdnsPublisher() : sock(static_cast<unsigned long long>(INVALID_SOCKET)) {}

MdnsPublisher::~MdnsPublisher() {
    Stop();
}

void MdnsPublisher::Start(const std::string& service_type, u16 port_) {
    if (running) {
        return;
    }
    port = port_;
    const std::string instance_name = MakeServiceName();
    service_fqdn = service_type + ".local";
    instance_fqdn = instance_name + "." + service_fqdn;
    host_fqdn = instance_name + ".local";

    Network::SocketManager::EnableSockets();

    local_ipv4_be = DiscoverLocalIPv4();
    if (local_ipv4_be == 0) {
        LOG_ERROR(Frontend, "MdnsPublisher: could not determine local IPv4 address");
        Network::SocketManager::DisableSockets();
        return;
    }

    const SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET) {
        LOG_ERROR(Frontend, "MdnsPublisher: could not create socket");
        Network::SocketManager::DisableSockets();
        return;
    }

    const int reuse = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<u16>(kMdnsPort));
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        LOG_ERROR(Frontend, "MdnsPublisher: could not bind UDP port {}", kMdnsPort);
        closesocket(s);
        Network::SocketManager::DisableSockets();
        return;
    }

    ip_mreq mreq{};
    inet_pton(AF_INET, kMdnsGroup, &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = INADDR_ANY;
    if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&mreq),
                   sizeof(mreq)) != 0) {
        LOG_ERROR(Frontend, "MdnsPublisher: could not join mDNS multicast group");
        closesocket(s);
        Network::SocketManager::DisableSockets();
        return;
    }

    const u8 ttl = 255; // RFC 6762 section 11: guards against off-subnet spoofed packets.
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&ttl), sizeof(ttl));

    // SO_RCVTIMEO on Windows takes a plain millisecond count sized like a DWORD (32-bit
    // unsigned); `unsigned long` has the same width on Windows (LLP64) without depending on
    // <windows.h> having been pulled in for the DWORD typedef itself.
    const unsigned long recv_timeout_ms = 200; // Bounds how long Stop() waits to notice.
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&recv_timeout_ms),
               sizeof(recv_timeout_ms));

    sock = static_cast<unsigned long long>(s);
    running = true;
    responder_thread = std::thread(&MdnsPublisher::ResponderLoop, this);
    LOG_INFO(Frontend, "MdnsPublisher: advertising {} on port {}", instance_fqdn, port);
}

void MdnsPublisher::Stop() {
    if (!running) {
        return;
    }
    running = false;
    if (responder_thread.joinable()) {
        responder_thread.join();
    }
    if (sock != static_cast<unsigned long long>(INVALID_SOCKET)) {
        closesocket(static_cast<SOCKET>(sock));
        sock = static_cast<unsigned long long>(INVALID_SOCKET);
    }
    Network::SocketManager::DisableSockets();
    LOG_INFO(Frontend, "MdnsPublisher: stopped");
}

void MdnsPublisher::ResponderLoop() {
    std::array<u8, 4096> buf;
    sockaddr_in mdns_group{};
    mdns_group.sin_family = AF_INET;
    mdns_group.sin_port = htons(static_cast<u16>(kMdnsPort));
    inet_pton(AF_INET, kMdnsGroup, &mdns_group.sin_addr);

    const std::string lower_service_fqdn = Lowercase(service_fqdn);
    const std::string lower_instance_fqdn = Lowercase(instance_fqdn);
    const std::string lower_host_fqdn = Lowercase(host_fqdn);

    while (running) {
        sockaddr_in from{};
        int from_len = sizeof(from);
        const int n = recvfrom(static_cast<SOCKET>(sock), reinterpret_cast<char*>(buf.data()),
                                static_cast<int>(buf.size()), 0, reinterpret_cast<sockaddr*>(&from),
                                &from_len);
        if (n <= 0) {
            continue; // Timeout (expected, lets us re-check `running`) or a transient error.
        }
        const auto size = static_cast<std::size_t>(n);
        if (size < 12) {
            continue; // Shorter than a DNS header - malformed, ignore.
        }

        const u16 flags = ReadU16BE(buf.data(), 2);
        if (flags & FlagQr) {
            continue; // It's a response (e.g. our own multicast loopback) - never answer answers.
        }
        const u16 qdcount = ReadU16BE(buf.data(), 4);

        u32 plan = AnswerNone;
        std::size_t offset = 12;
        constexpr u16 kMaxQuestions = 32; // Hard cap - never trust qdcount from the wire alone.
        const u16 questions_to_read = std::min(qdcount, kMaxQuestions);

        for (u16 i = 0; i < questions_to_read && offset < size; ++i) {
            std::string name;
            if (!DecodeName(buf.data(), size, offset, name)) {
                break; // Malformed - abort parsing the rest of this packet.
            }
            if (offset + 4 > size) {
                break; // Need QTYPE+QCLASS, truncated.
            }
            const u16 qtype = ReadU16BE(buf.data(), offset);
            const u16 qclass = ReadU16BE(buf.data(), offset + 2) & ~QuBitMask;
            offset += 4;

            if (qclass != DnsClassIn) {
                continue;
            }
            const std::string lname = Lowercase(name);
            if (lname == lower_service_fqdn && (qtype == DnsTypePtr || qtype == DnsTypeAny)) {
                plan |= AnswerPtr;
            } else if (lname == lower_instance_fqdn) {
                if (qtype == DnsTypeSrv || qtype == DnsTypeAny) {
                    plan |= AnswerSrv;
                }
                if (qtype == DnsTypeTxt || qtype == DnsTypeAny) {
                    plan |= AnswerTxt;
                }
            } else if (lname == lower_host_fqdn && (qtype == DnsTypeA || qtype == DnsTypeAny)) {
                plan |= AnswerA;
            }
            // Anything else is not one of our three names - silently ignored. We only ever
            // answer for our own single service, never act as a general mDNS responder.
        }

        if (plan == AnswerNone) {
            continue;
        }
        // Bundling: whenever PTR is answered, include SRV+TXT+A too, pre-answering the
        // querier's inevitable follow-up resolve() - matches what Avahi gives us for free.
        if (plan & AnswerPtr) {
            plan |= AnswerSrv | AnswerTxt | AnswerA;
        }

        const std::vector<u8> reply =
            BuildReply(plan, service_fqdn, instance_fqdn, host_fqdn, port, local_ipv4_be);
        sendto(static_cast<SOCKET>(sock), reinterpret_cast<const char*>(reply.data()),
               static_cast<int>(reply.size()), 0, reinterpret_cast<sockaddr*>(&mdns_group),
               sizeof(mdns_group));
    }
}

} // namespace NetworkStreaming
