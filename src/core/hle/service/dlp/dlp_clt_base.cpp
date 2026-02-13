#include "dlp_clt_base.h"

#include "core/hle/ipc_helpers.h"
#include "common/string_util.h"
#include "core/hle/service/nwm/uds_beacon.h"
#include "core/hle/service/am/am.h"
#include "common/timer.h"
#include "common/alignment.h"

#include <fstream>

namespace Service::DLP {

DLP_Clt_Base::DLP_Clt_Base(Core::System& s, std::string unique_string_id) : DLP_Base(s) {
    std::string unique_scan_event_id = std::format("DLP::{}::BeaconScanCallback", unique_string_id);
    beacon_scan_event = system.CoreTiming().RegisterEvent(
        unique_scan_event_id, [this](std::uintptr_t user_data, s64 cycles_late) {
            BeaconScanCallback(user_data, cycles_late);
        });
}

DLP_Clt_Base::~DLP_Clt_Base() {
    {
        std::scoped_lock lock(beacon_mutex);
        is_scanning = false;
        system.CoreTiming().UnscheduleEvent(beacon_scan_event, 0);
    }

    DisconnectFromServer();
}

void DLP_Clt_Base::InitializeCltBase(u32 shared_mem_size, u32 max_beacons, u32 constant_mem_size, std::shared_ptr<Kernel::SharedMemory> shared_mem, std::shared_ptr<Kernel::Event> event, DLP_Username username) {
    InitializeDlpBase(shared_mem_size, shared_mem, event, username);

    clt_state = DLP_Clt_State::Initialized;
    max_title_info = max_beacons;

    LOG_INFO(Service_DLP, "shared mem size: 0x{:x}, max beacons: {}, constant mem size: 0x{:x}, username: {}", shared_mem_size, max_beacons, constant_mem_size, Common::UTF16ToUTF8(DLPUsernameAsString16(username)));
}

void DLP_Clt_Base::FinalizeCltBase() {
    if (is_connected) {
        DisconnectFromServer();
    }

    FinalizeDlpBase();

    LOG_INFO(Service_DLP, "called");
}

void DLP_Clt_Base::GenerateChannelHandle() {
    dlp_channel_handle = 0x0421; // it seems to always be this value on hardware
}

u32 DLP_Clt_Base::GetCltState() {
    std::scoped_lock lock(clt_state_mutex);
    u16 node_bitmask = 0x0;
    if (is_connected) {
        // TODO: verify this!
        //node_bitmask = GetUDS()->GetConnectionStatusHLE().node_bitmask;
    }
    return static_cast<u32>(clt_state) << 24 | is_connected << 16 | node_bitmask;
}

void DLP_Clt_Base::GetChannels(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    GenerateChannelHandle();

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);
    rb.Push(dlp_channel_handle);
}

void DLP_Clt_Base::GetMyStatus(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    IPC::RequestBuilder rb = rp.MakeBuilder(6, 0);
    rb.Push(ResultSuccess);
    rb.Push(GetCltState());
    rb.Push(dlp_units_total);
    rb.Push(dlp_units_downloaded);
    // TODO: find out what these are
    rb.Push(0x0);
    rb.Push(0x0);
}

int DLP_Clt_Base::GetCachedTitleInfoIdx(Network::MacAddress mac_addr) {
    std::scoped_lock lock(title_info_mutex);

    for (int i = 0; auto& t : scanned_title_info) {
        if (t.first.mac_addr == mac_addr) {
            return i;
        }
        i++;
    }
    return -1;

}

bool DLP_Clt_Base::TitleInfoIsCached(Network::MacAddress mac_addr) {
    return GetCachedTitleInfoIdx(mac_addr) != -1;
}

void DLP_Clt_Base::StartScan(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    u16 scan_handle = rp.Pop<u16>();
    scan_title_id_filter = rp.Pop<u64>();
    scan_mac_address_filter = rp.PopRaw<Network::MacAddress>();
    ASSERT_MSG(scan_handle == dlp_channel_handle, "Scan handle and dlp channel handle do not match. Did you input the wrong ipc params?");
    [[maybe_unused]] u32 unk1 = rp.Pop<u32>();

    // start beacon worker
    std::lock_guard lock(beacon_mutex);
    if (!is_scanning) {
        // reset scan dependent variables
        std::scoped_lock lock(title_info_mutex);

        scanned_title_info.clear();
        ignore_servers_list.clear();
        title_info_index = 0;

        clt_state = DLP_Clt_State::Scanning;
        is_scanning = true;

        // clear out received beacons
        GetUDS()->GetReceivedBeacons(Network::BroadcastMac);

        LOG_INFO(Service_DLP, "Starting scan worker");

        constexpr int first_scan_delay_ms = 0;

        system.CoreTiming().ScheduleEvent(msToCycles(first_scan_delay_ms),
                                          beacon_scan_event, 0);
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void DLP_Clt_Base::StopScan(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    // end beacon worker
    {
        std::lock_guard lock(beacon_mutex);
        clt_state = DLP_Clt_State::Initialized;
        is_scanning = false;

        LOG_INFO(Service_DLP, "Ending scan worker");

        system.CoreTiming().UnscheduleEvent(beacon_scan_event, 0);
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

constexpr u32 res_data_available = 0x0;
constexpr u32 res_no_data_available = 0xc880afef;

void DLP_Clt_Base::GetTitleInfo(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    auto mac_addr = rp.PopRaw<Network::MacAddress>();
    u32 tid_low = rp.Pop<u32>();
    u32 tid_high = rp.Pop<u32>();

    if (!TitleInfoIsCached(mac_addr)) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(res_no_data_available);
        return;
    }

    auto c_title_idx = GetCachedTitleInfoIdx(mac_addr);
    std::vector<u8> buffer = scanned_title_info[c_title_idx].first.ToBuffer();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(res_data_available);
    rb.PushStaticBuffer(std::move(buffer), 0);
}

void DLP_Clt_Base::GetTitleInfoInOrder(Kernel::HLERequestContext& ctx) {
    constexpr u8 cmd_reset_iterator = 0x1;

    IPC::RequestParser rp(ctx);

    u8 command = rp.Pop<u8>();
    if (command == cmd_reset_iterator) {
        title_info_index = 0;
    }

    std::lock_guard lock(title_info_mutex);

    if (title_info_index >= scanned_title_info.size()) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(res_no_data_available);
        return;
    }

    std::vector<u8> buffer = scanned_title_info[title_info_index].first.ToBuffer();

    ++title_info_index;

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(res_data_available);
    rb.PushStaticBuffer(std::move(buffer), 0);
}

void DLP_Clt_Base::DeleteScanInfo(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    LOG_INFO(Service_DLP, "Called");

    auto mac_addr = rp.PopRaw<Network::MacAddress>();

    std::scoped_lock lock(title_info_mutex);

    if (!is_scanning) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(0xE0A0AC01);
        return;
    }

    if (!TitleInfoIsCached(mac_addr)) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(0xD960AC02); // info not found
        return;
    }

    scanned_title_info.erase(scanned_title_info.begin() + GetCachedTitleInfoIdx(mac_addr));

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void DLP_Clt_Base::GetServerInfo(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    auto mac_addr = rp.PopRaw<Network::MacAddress>();

    std::vector<u8> buffer;
    buffer.resize(sizeof(DLPServerInfo));
    auto server_info = reinterpret_cast<DLPServerInfo*>(buffer.data());

    if (!TitleInfoIsCached(mac_addr)) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(Result(ErrorDescription::NotFound, ErrorModule::DLP,
                       ErrorSummary::WrongArgument, ErrorLevel::Status));
        return;
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);

    *server_info = scanned_title_info[GetCachedTitleInfoIdx(mac_addr)].second;

    rb.Push(ResultSuccess);
    rb.PushStaticBuffer(std::move(buffer), 0);
}

// this is an issue for save states!
// someone please verify this
class DLP_Clt_Base::ThreadCallback : public Kernel::HLERequestContext::WakeupCallback {
public:
    explicit ThreadCallback(std::shared_ptr<DLP_Clt_Base> p) : p_obj(p) {}

    void WakeUp(std::shared_ptr<Kernel::Thread> thread, Kernel::HLERequestContext& ctx,
                Kernel::ThreadWakeupReason reason) {
        IPC::RequestBuilder rb(ctx, 1, 0);

        if (!p_obj->OnConnectCallback()) {
            // TODO: figure out what the proper error code is (timed out)
            rb.Push<u32>(-1);
            return;
        }
        rb.Push(ResultSuccess);
    }

private:
    ThreadCallback() = default;
    std::shared_ptr<DLP_Clt_Base> p_obj;

    template <class Archive>
    void serialize(Archive& ar, const unsigned int) {
        ar& boost::serialization::base_object<Kernel::HLERequestContext::WakeupCallback>(*this);
    }
    friend class boost::serialization::access;
};

bool DLP_Clt_Base::OnConnectCallback() {
    auto uds = GetUDS();
    if (uds->GetConnectionStatusHLE().status != NWM::NetworkStatus::ConnectedAsClient) {
        LOG_ERROR(Service_DLP, "Could not connect to dlp server (timed out)");
        return false;
    }

    is_connected = true;

    client_connection_worker = std::thread([this] {
        ClientConnectionManager();
    });

    return true;
}

void DLP_Clt_Base::StartSession(Kernel::HLERequestContext& ctx) {
    std::scoped_lock lock(clt_state_mutex);
    IPC::RequestParser rp(ctx);

    auto mac_addr = rp.PopRaw<Network::MacAddress>();

    LOG_INFO(Service_DLP, "called");

    // tells us which child we want to use for this session
    // only used for dlp::CLNT
    u32 dlp_child_low = rp.Pop<u32>();
    u32 dlp_child_high = rp.Pop<u32>();

    if (is_connected) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(0xe0a0ac01); // error: session already started
        return;
    }
    if (!TitleInfoIsCached(mac_addr)) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(0xc880afef); // error: cannot locate server info cache from mac address
        return;
    }

    dlp_download_child_tid = static_cast<u64>(dlp_child_high) << 32 | dlp_child_low;

    // ConnectToNetworkAsync won't work here beacuse this is
    // synchronous

    auto shared_this = std::dynamic_pointer_cast<DLP_Clt_Base>(GetServiceFrameworkSharedPtr());
    if (!shared_this) {
        LOG_CRITICAL(Service_DLP, "Could not dynamic_cast service framework shared_ptr to DLP_Clt_Base");
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(-1);
        return;
    }

    host_mac_address = mac_addr;
    clt_state = DLP_Clt_State::Joined;

    auto uds = GetUDS();
    NWM::NetworkInfo net_info;
    net_info.host_mac_address = mac_addr;
    net_info.channel = dlp_net_info_channel;
    net_info.initialized = true;
    net_info.oui_value = NWM::NintendoOUI;

    uds->ConnectToNetworkHLE(net_info, static_cast<u8>(NWM::ConnectionType::Client), dlp_password_buf);

    // 3 second timeout
    static constexpr std::chrono::nanoseconds UDSConnectionTimeout{3000000000};
    uds->connection_event = ctx.SleepClientThread("DLP_Clt_Base::StartSession", UDSConnectionTimeout,
                                                  std::make_shared<ThreadCallback>(shared_this));
}

void DLP_Clt_Base::StopSession(Kernel::HLERequestContext& ctx) {
    LOG_INFO(Service_DLP, "called");
    std::scoped_lock lock(clt_state_mutex);
    IPC::RequestParser rp(ctx);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);

    if (!is_connected) {
        // this call returns 0 no matter what
        rb.Push<u32>(0x0);
        return;
    }

    DisconnectFromServer();

    rb.Push(ResultSuccess);
}

void DLP_Clt_Base::GetConnectingNodes(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    u16 node_array_len = rp.Pop<u16>();

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 2);

    auto conn_status = GetUDS()->GetConnectionStatusHLE();

    if (!is_connected || conn_status.status != NWM::NetworkStatus::ConnectedAsClient) {
        LOG_ERROR(Service_DLP, "called when we are not connected to a server");
    }

    std::vector<u8> connected_nodes_buffer;
    connected_nodes_buffer.resize(node_array_len*sizeof(u16));
    memcpy(connected_nodes_buffer.data(), conn_status.nodes, std::min<u32>(connected_nodes_buffer.size(), conn_status.total_nodes)*sizeof(u16));

    rb.Push(ResultSuccess);
    rb.Push<u32>(conn_status.total_nodes);
    rb.PushStaticBuffer(std::move(connected_nodes_buffer), 0);
}

void DLP_Clt_Base::GetNodeInfo(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    u16 network_node_id = rp.Pop<u16>();

    auto node_info = GetUDS()->GetNodeInformationHLE(network_node_id);
    if (!node_info) {
        LOG_ERROR(Service_DLP, "Could not get node info for network node id 0x{:x}", network_node_id);
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        // this is the error code for unknown network node id
        rb.Push(0xE0A0AC01);
        return;
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(11, 0);

    rb.Push(ResultSuccess);
    rb.PushRaw(UDSToDLPNodeInfo(*node_info));
}

void DLP_Clt_Base::GetWirelessRebootPassphrase(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    LOG_INFO(Service_DLP, "called");

    std::scoped_lock lock(clt_state_mutex);
    if (clt_state != DLP_Clt_State::Complete) {
        LOG_WARNING(Service_DLP, "we have not gotten the passphrase yet");
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(4, 0);
    rb.Push(ResultSuccess);
    rb.PushRaw(wireless_reboot_passphrase);
}

void DLP_Clt_Base::BeaconScanCallback(std::uintptr_t user_data, s64 cycles_late) {
    std::scoped_lock lock{beacon_mutex, title_info_mutex};

    if (!is_scanning) {
        return;
    }

    auto uds = GetUDS();
    Common::Timer beacon_parse_timer_total;

    // sadly, we have to impl the scan code ourselves
    // because the nwm recvbeaconbroadcastdata function
    // has a timeout in it, which won't work here because
    // we don't have a uds server/client session
    auto beacons = uds->GetReceivedBeacons(Network::BroadcastMac);

    beacon_parse_timer_total.Start();

    for (auto& beacon : beacons) {
        if (auto idx = GetCachedTitleInfoIdx(beacon.transmitter_address); idx != -1) {
            // update server info from beacon
            auto b = GetDLPServerInfoFromRawBeacon(beacon);
            scanned_title_info[idx].second.clients_joined = b.clients_joined; // we only want to update clients joined
            continue;
        }
        if (scanned_title_info.size() >= max_title_info) {
            break;
        }
        if (ignore_servers_list[beacon.transmitter_address]) {
            continue;
        }

        CacheBeaconTitleInfo(beacon);
    }

    // set our next scan interval
    system.CoreTiming().ScheduleEvent(msToCycles(std::max<int>(0, beacon_scan_interval_ms - beacon_parse_timer_total.GetTimeElapsed().count())) -
                                      cycles_late, beacon_scan_event, 0);
}

void DLP_Clt_Base::CacheBeaconTitleInfo(Network::WifiPacket& beacon) {
    // connect to the network as a spectator
    // and receive dlp data

    auto uds = GetUDS();

    NWM::NetworkInfo net_info;
    net_info.host_mac_address = beacon.transmitter_address;
    net_info.channel = dlp_net_info_channel;
    net_info.initialized = true;
    net_info.oui_value = NWM::NintendoOUI;

    if (!ConnectToNetworkAsync(net_info, NWM::ConnectionType::Spectator, dlp_password_buf)) {
        LOG_ERROR(Service_DLP, "Could not connect to network.");
        return;
    }

    LOG_INFO(Service_DLP, "Connected to spec to network");

    auto [ret, data_available_event] = uds->BindHLE(dlp_bind_node_id, dlp_recv_buffer_size, dlp_broadcast_data_channel, dlp_host_network_node_id);
    if (ret != 0) {
        LOG_ERROR(Service_DLP, "Could not bind on node id 0x{:x}", dlp_bind_node_id);
        return;
    }

    auto aes = GenDLPChecksumKey(beacon.transmitter_address);

    constexpr u32 max_beacon_recv_time_out_ms = 1000;

    Common::Timer beacon_parse_timer;
    beacon_parse_timer.Start();

    std::unordered_map<int, bool> got_broadcast_packet;
    std::unordered_map<int, std::vector<u8>> broadcast_packet_idx_buf;
    DLP_Username server_username; // workaround before I decrypt the beacon data
    std::vector<u8> recv_buf;
    bool got_all_packets = false;
    while (beacon_parse_timer.GetTimeElapsed().count() < max_beacon_recv_time_out_ms) {
        if (int sz = RecvFrom(dlp_host_network_node_id, recv_buf)) {
            auto p_head = reinterpret_cast<DLPPacketHeader*>(recv_buf.data());
            if (!ValidatePacket(aes, p_head, sz) ||
                p_head->packet_index >= num_broadcast_packets) {
                ignore_servers_list[beacon.transmitter_address] = true;
                break; // corrupted info
            }
            got_broadcast_packet[p_head->packet_index] = true;
            broadcast_packet_idx_buf[p_head->packet_index] = recv_buf;
            if (got_broadcast_packet.size() == num_broadcast_packets) {
                got_all_packets = true;
                constexpr u16 nwm_host_node_network_id = 0x1;
                server_username = uds->GetNodeInformationHLE(nwm_host_node_network_id)->username;
                break; // we got all 5!
            }
        }
    }

    uds->UnbindHLE(dlp_bind_node_id);
    uds->DisconnectNetworkHLE();

    if (!got_all_packets) {
        if (!got_broadcast_packet.size()) {
            // we didn't get ANY packet info from this server
            // so we add it to the ignore list
            ignore_servers_list[beacon.transmitter_address] = true;
        }
        LOG_ERROR(Service_DLP, "Connected to beacon, but could not receive all dlp packets");
        return;
    }

    // parse packets into cached DLPServerInfo and DLPTitleInfo
    auto broad_pk1 = reinterpret_cast<DLPBroadcastPacket1*>(broadcast_packet_idx_buf[0].data());
    auto broad_pk2 = reinterpret_cast<DLPBroadcastPacket2*>(broadcast_packet_idx_buf[1].data());
    auto broad_pk3 = reinterpret_cast<DLPBroadcastPacket3*>(broadcast_packet_idx_buf[2].data());
    auto broad_pk4 = reinterpret_cast<DLPBroadcastPacket4*>(broadcast_packet_idx_buf[3].data());
    auto broad_pk5 = reinterpret_cast<DLPBroadcastPacket5*>(broadcast_packet_idx_buf[4].data());

    DLPServerInfo c_server_info = GetDLPServerInfoFromRawBeacon(beacon);
    {
        // workaround: load username in host node manually
        c_server_info.node_info[0].username = server_username;
    }

    DLPTitleInfo c_title_info{};
    c_title_info.mac_addr = beacon.transmitter_address;
    for (size_t i = 0; i < broad_pk1->title_short.size(); i++) {
        c_title_info.short_description[i] = d_ntohs(broad_pk1->title_short[i]);
    }
    for (size_t i = 0; i < broad_pk1->title_long.size(); i++) {
        c_title_info.long_description[i] = d_ntohs(broad_pk1->title_long[i]);
    }
    // unique id should be the title id without the tid high shifted 1 byte right
    c_title_info.unique_id = (d_ntohll(broad_pk1->child_title_id) & 0xFFFFFFFF) >> 8;

    c_title_info.size = d_ntohl(broad_pk1->size) + broad_title_size_diff;

    // copy over the icon data
    auto memcpy_u16_ntohs = [](void *_d, const void *_s, size_t n) {
        auto d = reinterpret_cast<char*>(_d);
        auto s = reinterpret_cast<const char*>(_s);
        for (size_t i = 0; i < n; i += 2) {
            *reinterpret_cast<u16*>(d + i) = d_ntohs(*reinterpret_cast<const u16*>(s + i));
        }
        return n;
    };

    size_t loc = 0;
    loc += memcpy_u16_ntohs(c_title_info.icon.data(), broad_pk1->icon_part.data(), broad_pk1->icon_part.size());
    loc += memcpy_u16_ntohs(c_title_info.icon.data() + loc, broad_pk2->icon_part.data(), broad_pk2->icon_part.size());
    loc += memcpy_u16_ntohs(c_title_info.icon.data() + loc, broad_pk3->icon_part.data(), broad_pk3->icon_part.size());
    loc += memcpy_u16_ntohs(c_title_info.icon.data() + loc, broad_pk4->icon_part.data(), broad_pk4->icon_part.size());

    LOG_INFO(Service_DLP, "Got title info!");

    scanned_title_info.emplace_back(c_title_info, c_server_info);

    dlp_status_event->Signal();
}

DLPServerInfo DLP_Clt_Base::GetDLPServerInfoFromRawBeacon(Network::WifiPacket& beacon) {
    // get networkinfo from beacon
    auto p_beacon = beacon.data.data();

    bool found_net_info = false;
    NWM::NetworkInfo net_info;

    // find networkinfo tag
    for (auto place = p_beacon + sizeof(NWM::BeaconFrameHeader); place < place + beacon.data.size(); place += reinterpret_cast<NWM::TagHeader*>(place)->length + sizeof(NWM::TagHeader)) {
        auto th = reinterpret_cast<NWM::TagHeader*>(place);
        if (th->tag_id == static_cast<u8>(NWM::TagId::VendorSpecific) && th->length <= sizeof(NWM::NetworkInfoTag) - sizeof(NWM::TagHeader)) {
            // cast to network info and check if correct
            auto ni_tag = reinterpret_cast<NWM::NetworkInfoTag*>(place);
            memcpy(&net_info.oui_value, ni_tag->network_info.data(), ni_tag->network_info.size());
            // make sure this is really a network info tag
            if (net_info.oui_value == NWM::NintendoOUI && net_info.oui_type == static_cast<u8>(NWM::NintendoTagId::NetworkInfo)) {
                found_net_info = true;
                break;
            }
        }
    }

    if (!found_net_info) {
        LOG_ERROR(Service_DLP, "Unable to find network info in beacon payload");
        return DLPServerInfo{};
    }

    DLPServerInfo srv_info{};
    srv_info.mac_addr = beacon.transmitter_address;
    srv_info.max_clients = net_info.max_nodes;
    srv_info.clients_joined = net_info.total_nodes;
    srv_info.signal_strength = DLPSignalStrength::Strong;
    srv_info.unk5 = 0x6;
    // TODO: decrypt node info and load it in here
    return srv_info;
}

void DLP_Clt_Base::ClientConnectionManager() {
    auto uds = GetUDS();

    auto [ret, data_available_event] = uds->BindHLE(dlp_bind_node_id, dlp_recv_buffer_size, dlp_client_data_channel, dlp_host_network_node_id);
    if (ret != 0) {
        LOG_ERROR(Service_DLP, "Could not bind on node id 0x{:x}", dlp_bind_node_id);
        return;
    }

    auto aes = GenDLPChecksumKey(host_mac_address);

    auto sleep_poll = [](size_t poll_rate) -> void {
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_rate));
    };

    constexpr u32 dlp_poll_rate_normal = 100;
    constexpr u32 dlp_poll_rate_distribute = 1;

    u32 dlp_poll_rate_ms = dlp_poll_rate_normal;
    bool got_corrupted_packets = false;

    std::set<ReceivedFragment> received_fragments;

    while (sleep_poll(dlp_poll_rate_ms), is_connected) {
        std::vector<u8> recv_buf;

        if (int sz = RecvFrom(dlp_host_network_node_id, recv_buf)) {
            auto p_head = GetPacketHead(recv_buf);
            // validate packet header
            if (!ValidatePacket(aes, p_head, sz)) {
                got_corrupted_packets = true;
                LOG_ERROR(Service_DLP, "Could not validate DLP packet header");
                break;
            }

            // now we can parse the packet
            std::scoped_lock cs_lock(clt_state_mutex);
            if (p_head->type == dl_pk_type_auth) {
                auto r_pbody = GetPacketBody<DLPSrvr_Auth>(recv_buf);
                auto s_body = PGen_SetPK<DLPClt_AuthAck>(dl_pk_head_auth_header, 0, p_head->resp_id);
                s_body->unk1 = {0x01};
                s_body->unk2 = {0x00, 0x00};
                // TODO: find out what this is. this changes each session. could be loosly based on mac address?
                s_body->resp_id = {0x01, 0x02};
                PGen_SendPK(aes, dlp_host_network_node_id, dlp_client_data_channel);
            } else if (p_head->type == dl_pk_type_start_dist) {
                if (IsFKCL() || !NeedsContentDownload(host_mac_address)) {
                    auto s_body = PGen_SetPK<DLPClt_StartDistributionAck_NoContentNeeded>(dl_pk_head_start_dist_header, 0, p_head->resp_id);
                    s_body->unk1 = {0x1};
                    s_body->unk2 = 0x0;
                    is_downloading_content = false;
                    clt_state = DLP_Clt_State::WaitingForServerReady;
                } else {
                    // send content needed ack
                    auto s_body = PGen_SetPK<DLPClt_StartDistributionAck_ContentNeeded>(dl_pk_head_start_dist_header, 0, p_head->resp_id);
                    s_body->unk1 = 0x1;
                     // TODO: figure out what these are. seems like magic values
                    s_body->unk2 = d_htons(0x20);
                    s_body->unk3 = 0x0;
                    s_body->unk4 = 0x1;
                    s_body->unk5 = 0x0;
                    s_body->unk_body = {}; // all zeros
                    is_downloading_content = true;
                    clt_state = DLP_Clt_State::Downloading;

                    if (!TitleInfoIsCached(host_mac_address)) {
                        LOG_CRITICAL(Service_DLP, "Tried to request content download, but title info was not cached");
                        break;
                    }

                    auto tinfo = scanned_title_info[GetCachedTitleInfoIdx(host_mac_address)].first;

                    dlp_units_downloaded = 0;
                    dlp_units_total = Common::AlignUp(tinfo.size - broad_title_size_diff, content_fragment_size)/content_fragment_size;
                    dlp_poll_rate_ms = dlp_poll_rate_distribute;
                    current_content_block = 0;
                    LOG_INFO(Service_DLP, "Requesting game file");
                }
                PGen_SendPK(aes, dlp_host_network_node_id, dlp_client_data_channel);
            } else if (p_head->type == dl_pk_type_distribute) {
                if (is_downloading_content) {
                    auto r_pbody = GetPacketBody<DLPSrvr_ContentDistributionFragment>(recv_buf);
                    auto& cf = r_pbody->content_fragment;
                    ReceivedFragment frag{
                        .index = static_cast<u32>(d_ntohs(r_pbody->frag_index) + dlp_content_block_length*current_content_block),
                        .content{cf.begin(), cf.begin() + d_ntohs(r_pbody->frag_size)}
                    };
                    received_fragments.insert(frag);
                    dlp_units_downloaded++;
                    if (dlp_units_downloaded == dlp_units_total) {
                        dlp_poll_rate_ms = dlp_poll_rate_normal;
                        is_downloading_content = false;
                        clt_state = DLP_Clt_State::WaitingForServerReady;
                        LOG_INFO(Service_DLP, "Finished downloading content. Installing...");

                        if (!InstallEncryptedCIAFromFragments(received_fragments)) {
                            LOG_ERROR(Service_DLP, "Could not install DLP encrypted content");
                        } else {
                            LOG_INFO(Service_DLP, "Successfully installed DLP encrypted content");
                        }
                    }
                } else {
                    LOG_ERROR(Service_DLP, "Received content fragment without requesting it");
                }
            } else if (p_head->type == dl_pk_type_finish_dist) {
                if (p_head->packet_index == 0) {
                    LOG_ERROR(Service_DLP, "Received finish dist packet, but packet index was 0");
                } else if (p_head->packet_index == 1) {
                    auto r_pbody = GetPacketBody<DLPSrvr_FinishContentUpload>(recv_buf);
                    auto s_body = PGen_SetPK<DLPClt_FinishContentUploadAck>(dl_pk_head_finish_dist_header, 0, p_head->resp_id);
                    if (is_downloading_content) {
                        current_content_block++;
                    }
                    s_body->unk1 = 0x1;
                    s_body->unk2 = 0x1;
                    s_body->unk3 = is_downloading_content;
                    s_body->seq_ack = d_htonl(d_ntohl(r_pbody->seq_num) + 1);
                    s_body->unk4 = 0x0;
                    PGen_SendPK(aes, dlp_host_network_node_id, dlp_client_data_channel);
                }
            } else if (p_head->type == dl_pk_type_start_game) {
                if (p_head->packet_index == 0) {
                    auto s_body = PGen_SetPK<DLPClt_BeginGameAck>(dl_pk_head_start_game_header, 0, p_head->resp_id);
                    s_body->unk1 = 0x1;
                    s_body->unk2 = 0x9;
                    PGen_SendPK(aes, dlp_host_network_node_id, dlp_client_data_channel);
                } else if (p_head->packet_index == 1) {
                    clt_state = DLP_Clt_State::Complete;
                    auto r_pbody = GetPacketBody<DLPSrvr_BeginGameFinal>(recv_buf);
                    wireless_reboot_passphrase = r_pbody->wireless_reboot_passphrase;
                } else {
                    LOG_ERROR(Service_DLP, "Unknown packet index {}", p_head->packet_index);
                }
            } else {
                LOG_ERROR(Service_DLP, "Unknown DLP Magic 0x{:x} 0x{:x} 0x{:x} 0x{:x}", p_head->magic[0], p_head->magic[1], p_head->magic[2], p_head->magic[3]);
            }
        }
    }

    uds->UnbindHLE(dlp_host_network_node_id);
    uds->DisconnectNetworkHLE();
}

bool DLP_Clt_Base::NeedsContentDownload(Network::MacAddress mac_addr) {
    std::scoped_lock lock(title_info_mutex);
    if (!TitleInfoIsCached(mac_addr)) {
        LOG_ERROR(Service_DLP, "title info was not cached");
        return false;
    }
    auto tinfo = scanned_title_info[GetCachedTitleInfoIdx(mac_addr)].first;
    u64 title_id = DLP_CHILD_TID_HIGH | (tinfo.unique_id << 8);
    return !FileUtil::Exists(AM::GetTitleContentPath(FS::MediaType::NAND, title_id));
}

bool DLP_Clt_Base::InstallEncryptedCIAFromFragments(std::set<ReceivedFragment>& frags) {
    auto cia_file = std::make_unique<AM::CIAFile>(system, FS::MediaType::NAND);
    cia_file->decryption_authorized = true;
    bool install_errored = false;
    for (u64 nb = 0; auto& frag : frags) {
        constexpr bool flush_data = true;
        constexpr bool update_timestamp = false;
        auto res = cia_file->Write(nb, frag.content.size(), flush_data, update_timestamp, frag.content.data());

        if (res.Failed()) {
            LOG_ERROR(Service_DLP, "Could not install CIA. Error code {:08x}", res.Code().raw);
            install_errored = true;
            break;
        }

        nb += frag.content.size();
    }
    cia_file->Close();
    return !install_errored;
}

void DLP_Clt_Base::DisconnectFromServer() {
    is_connected = false;
    if (client_connection_worker.joinable()) {
        client_connection_worker.join();
    }
}

} // namespace Service::DLP
