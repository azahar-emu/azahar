// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/hle/kernel/event.h"
#include "core/hle/kernel/shared_memory.h"
#include "dlp_base.h"

namespace Service::DLP {

enum class DLP_Clt_State : u32 {
    NotInitialized = 0, // TODO: check on hardware. it probably just errors
    Initialized = 1,
    Scanning = 2,
    Joined = 5,
    Downloading = 6,
    WaitingForServerReady = 7,
    Complete = 9,
};

// number of bars
enum class DLPSignalStrength : u8 {
    VeryWeak = 0,
    Weak = 1,
    Medium = 2,
    Strong = 3,
};

// info from a server that
// can be obtained from its beacon only
struct DLPServerInfo {
    Network::MacAddress mac_addr;
    u8 unk1;
    DLPSignalStrength signal_strength;
    u8 max_clients;
    u8 clients_joined;
    u16 unk3;    // node bitmask?
    u32 padding; // all zeros
    std::array<DLPNodeInfo, 16> node_info;
    u32 unk4;
    u32 unk5;
    std::vector<u8> ToBuffer() {
        std::vector<u8> out;
        out.resize(sizeof(DLPServerInfo));
        memcpy(out.data(), this, sizeof(DLPServerInfo));
        return out;
    }
};

static_assert(sizeof(DLPServerInfo) == 0x298);

class DLP_Clt_Base : public DLP_Base {
protected:
    DLP_Clt_Base(Core::System& s, std::string unique_string_id);
    virtual ~DLP_Clt_Base();

    virtual bool IsHost() {
        return false;
    }

    class ThreadCallback;
    bool OnConnectCallback();
    void ClientConnectionManager();

    virtual bool IsFKCL() = 0;
    bool IsCLNT() {
        return !IsFKCL();
    }

    DLP_Clt_State clt_state = DLP_Clt_State::NotInitialized;
    u16 dlp_channel_handle{};
    std::atomic_bool is_connected = false;
    u32 dlp_units_downloaded = 0x0, dlp_units_total = 0x0;
    u64 dlp_download_child_tid = 0x0;
    u32 title_info_index = 0;
    u32 max_title_info = 0; ///< once we receive x beacons, we will no longer parse any other
                            ///< beacons until at least one tinfo buf element is cleared
    bool is_scanning = false;
    constexpr static inline int beacon_scan_interval_ms = 1000;
    std::vector<std::pair<DLPTitleInfo, DLPServerInfo>> scanned_title_info;
    std::map<Network::MacAddress, bool>
        ignore_servers_list; // ignore servers which give us bad broadcast data
    u64 scan_title_id_filter;
    Network::MacAddress scan_mac_address_filter;
    Network::MacAddress host_mac_address;
    constexpr static inline u16 dlp_net_info_channel = 0x1;
    constexpr static inline u16 dlp_bind_node_id = 0x1;
    constexpr static inline u32 dlp_recv_buffer_size = 0x3c00;
    constexpr static inline u8 dlp_broadcast_data_channel = 0x1;
    constexpr static inline u8 dlp_client_data_channel = 0x2;
    constexpr static inline u8 dlp_host_network_node_id = 0x1;

    Core::TimingEventType* beacon_scan_event;

    std::mutex beacon_mutex;
    std::recursive_mutex title_info_mutex;
    std::recursive_mutex clt_state_mutex;

    std::thread client_connection_worker;

    bool is_downloading_content;
    struct ReceivedFragment {
        u32 index;
        std::vector<u8> content;
        bool operator<(const ReceivedFragment& o) const {
            return index < o.index;
        }
    };
    u16 current_content_block;

    void InitializeCltBase(u32 shared_mem_size, u32 max_beacons, u32 constant_mem_size,
                           std::shared_ptr<Kernel::SharedMemory> shared_mem,
                           std::shared_ptr<Kernel::Event> event, DLP_Username username);
    void FinalizeCltBase();
    void GenerateChannelHandle();
    u32 GetCltState();
    void BeaconScanCallback(std::uintptr_t user_data, s64 cycles_late);
    void CacheBeaconTitleInfo(Network::WifiPacket& beacon);
    int GetCachedTitleInfoIdx(Network::MacAddress mac_addr);
    bool TitleInfoIsCached(Network::MacAddress mac_addr);
    DLPServerInfo GetDLPServerInfoFromRawBeacon(Network::WifiPacket& beacon);
    bool NeedsContentDownload(Network::MacAddress mac_addr);
    bool InstallEncryptedCIAFromFragments(std::set<ReceivedFragment>& frags);
    void DisconnectFromServer();
    bool IsIdling();

    void GetMyStatus(Kernel::HLERequestContext& ctx);
    void GetChannels(Kernel::HLERequestContext& ctx);
    void GetTitleInfo(Kernel::HLERequestContext& ctx);
    void GetTitleInfoInOrder(Kernel::HLERequestContext& ctx);
    void StartScan(Kernel::HLERequestContext& ctx);
    void StopScan(Kernel::HLERequestContext& ctx);
    void DeleteScanInfo(Kernel::HLERequestContext& ctx);
    void GetServerInfo(Kernel::HLERequestContext& ctx);
    void StartSession(Kernel::HLERequestContext& ctx);
    void StopSession(Kernel::HLERequestContext& ctx);
    void GetConnectingNodes(Kernel::HLERequestContext& ctx);
    void GetNodeInfo(Kernel::HLERequestContext& ctx);
    void GetWirelessRebootPassphrase(Kernel::HLERequestContext& ctx);
};

} // namespace Service::DLP
