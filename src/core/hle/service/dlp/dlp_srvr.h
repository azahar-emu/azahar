// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/timer.h"
#include "core/hle/service/service.h"
#include "dlp_base.h"

namespace Service::FS {
class FS_USER;
}; // namespace Service::FS

namespace Service::DLP {

enum class DLP_Srvr_State : u8 {
    NotInitialized = 0,
    Initialized = 1,
    Accepting = 2,
    WaitingForDistribution = 6, // server is processing clients' needs
    Distributing = 7,
    NeedToSendPassphrase = 8, // finished distribution
    Complete = 9,
};

class DLP_SRVR final : public ServiceFramework<DLP_SRVR>, public DLP_Base {
public:
    DLP_SRVR();
    ~DLP_SRVR();

    virtual std::shared_ptr<Kernel::SessionRequestHandler> GetServiceFrameworkSharedPtr();
    virtual bool IsHost() {
        return true;
    }

private:
    std::shared_ptr<FS::FS_USER> GetFS();

    std::recursive_mutex srvr_state_mutex;
    DLP_Srvr_State srvr_state = DLP_Srvr_State::NotInitialized;
    u8 max_clients;
    std::vector<u8> distribution_content;
    bool manual_accept;
    constexpr static inline int title_broadcast_interval_ms = 500;
    constexpr static inline u32 dlp_poll_rate_distribute = 1;
    const u16 broadcast_node_id = 0xFFFF;
    bool is_broadcasting;
    std::mutex broadcast_mutex;
    const std::array<u8, 3> default_resp_id{};
    Network::MacAddress host_mac_address;

    std::atomic_int dlp_srvr_poll_rate_ms;

    struct TitleBroadcastInfo {
        u64 title_id;
        u64 title_size;
        u64 transfer_size;
        u64 required_size;
        std::array<u16, 0x900> icon;
        std::array<u16, 0x40> title_short;
        std::array<u16, 0x80> title_long;
    } title_broadcast_info;

    std::atomic_bool is_distributing;
    std::atomic_bool is_waiting_for_passphrase;

    struct ClientState {
        ClientState() {
            rate_timer.Start();
        }
        bool needs_content_download;
        bool can_download_next_block;
        bool sent_next_block_req;
        bool is_accepted;
        u8 pk_seq_num;
        void IncrSeqNum() {
            pk_seq_num++;
        }
        Common::Timer rate_timer;
        bool ShouldRateLimit(int ms) {
            if (rate_timer.GetTimeDifference().count() < ms) {
                return true;
            }
            rate_timer.Update();
            return false;
        }
        u32 dlp_units_total;
        u32 GetDlpUnitsDownloaded() {
            return current_content_block * dlp_content_block_length;
        }
        u32 current_content_block;
        u32 GetTotalFragIndex(u8 block_frag_index) {
            return current_content_block * dlp_content_block_length + block_frag_index;
        }
        u8 network_node_id;
        std::array<u8, 2> resp_id;
        u16 next_req_ack;

        std::array<u8, 3> GetPkRespId() {
            return {pk_seq_num, resp_id[0], resp_id[1]};
        }
        u32 GetMyState() {
            DLP_Clt_State state_lle;
            switch (state) {
            case NotJoined:
                LOG_WARNING(Service_DLP, "Trying to get LLE state of client that is not joined");
                state_lle = DLP_Clt_State::Initialized;
                break;
            case NeedsAuth:
            case NeedsAuthAck:
                state_lle = DLP_Clt_State::WaitingForAccept;
                break;
            case Accepted:
                state_lle = DLP_Clt_State::Joined;
                break;
            case NeedsDistributeAck:
            case NeedsContent:
            case DoesNotNeedContent:
                state_lle = DLP_Clt_State::Downloading;
                break;
            case DistributeDone:
                state_lle = DLP_Clt_State::WaitingForServerReady;
                break;
            case SentPassphrase:
                state_lle = DLP_Clt_State::Complete;
                break;
            default:
                LOG_ERROR(Service_DLP, "Unknown client state");
                break;
            }

            bool is_joined = state != NotJoined && state != NeedsAuth && state != NeedsAuthAck;

            return static_cast<u32>(state_lle) << 24 | is_joined << 16 | network_node_id;
        }

        // could be used in getclientstate
        enum : u8 {
            NotJoined = 0,
            NeedsAuth,
            NeedsAuthAck, // protects us from clients authing themselves
            Accepted,
            NeedsDistributeAck,
            NeedsContent,
            DoesNotNeedContent,
            DistributeDone,
            SentPassphrase,
        } state;
    };

    std::vector<ClientState> client_states;
    std::recursive_mutex client_states_mutex;

    ClientState* GetClState(u8 node_id, bool should_error = true);

    Core::TimingEventType* title_broadcast_event;

    std::atomic_bool is_hosting;
    std::thread server_connection_worker;

    void ServerConnectionManager();
    void EndConnectionManager();
    void SendAuthPacket(ClientState& cl);
    bool SendNextCIAFragment(ClientState& cl, u8 block_frag_index);

    u8 GetSrvrState();
    void InitializeSrvrCommon(u32 shared_mem_size, u8 max_clnts, u32 process_id, DLP_Username uname,
                              std::shared_ptr<Kernel::SharedMemory> shared_mem,
                              std::shared_ptr<Kernel::Event> event);
    bool CacheContentFileInMemory(u32 process_id);
    void TitleBroadcastCallback(std::uintptr_t user_data, s64 cycles_late);
    void EnsureEndBroadcaster();

    void IsChild(Kernel::HLERequestContext& ctx);
    void GetDupNoticeNeed(Kernel::HLERequestContext& ctx);
    void GetServerState(Kernel::HLERequestContext& ctx);
    void Initialize(Kernel::HLERequestContext& ctx);
    void InitializeWithName(Kernel::HLERequestContext& ctx);
    void Finalize(Kernel::HLERequestContext& ctx);
    void StartHosting(Kernel::HLERequestContext& ctx);
    void EndHosting(Kernel::HLERequestContext& ctx);
    void GetConnectingClients(Kernel::HLERequestContext& ctx);
    void GetClientInfo(Kernel::HLERequestContext& ctx);
    void GetClientState(Kernel::HLERequestContext& ctx);
    void StartDistribution(Kernel::HLERequestContext& ctx);
    void BeginGame(Kernel::HLERequestContext& ctx);
    void AcceptClient(Kernel::HLERequestContext& ctx);
    void DisconnectClient(Kernel::HLERequestContext& ctx);

    SERVICE_SERIALIZATION_SIMPLE
    friend class ClientState;
};

} // namespace Service::DLP

BOOST_CLASS_EXPORT_KEY(Service::DLP::DLP_SRVR)
