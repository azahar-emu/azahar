// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/core.h"
#include "core/hle/service/cfg/cfg.h"
#include "core/hle/service/nwm/nwm_uds.h"
#include "core/hle/service/service.h"
#include "core/loader/smdh.h"

#include <semaphore>

// DLP save states are not supported

namespace Service::DLP {

using DLP_Username = std::array<u16_le, 10>;
constexpr inline u64 DLP_CHILD_TID_HIGH = 0x0004000100000000;
constexpr inline u32 content_fragment_size = 1440; // const frag packet size

struct DLPTitleInfo {
    u32 unique_id; // games look at this to make sure it's their title info
    u32 variation;
    Network::MacAddress mac_addr;
    u16 version; // XX: probably?
    std::array<u8, 16> age_ratings;
    std::array<u16, 64> title_short; // UTF-16
    std::array<u16, 128> title_long; // UTF-16
    std::array<u16, 0x900> icon;     // 48x48, RGB565
    u32 size;
    u8 unk2;
    u8 unk3;
    u16 padding;
    std::vector<u8> ToBuffer() {
        std::vector<u8> out;
        out.resize(sizeof(DLPTitleInfo));
        memcpy(out.data(), this, sizeof(DLPTitleInfo));
        return out;
    }
};

static_assert(sizeof(DLPTitleInfo) == 5032, "DLPTitleInfo is the wrong size");

struct DLPNodeInfo {
    u64 friend_code_seed;
    std::array<u16, 2> pad;
    DLP_Username username;
    u32 unk1;
    u32 network_node_id;
};

static_assert(sizeof(DLPNodeInfo) == 0x28);

struct DLPEventDescription {
    std::array<u8, 0x18> unk;
};

static_assert(sizeof(DLPEventDescription) == 0x18);

enum class DLP_Clt_State : u16 {
    NotInitialized = 0, // TODO: check on hardware. it probably just errors
    Initialized = 1,
    Scanning = 2,
    Joined = 5,
    Downloading = 6,
    WaitingForServerReady = 7,
    Complete = 9,
    WaitingForAccept =
        64, // when a client joins, but is not accepted yet (used on servers w/ manual accept)
};

// START BIG ENDIAN

constexpr inline u8 dl_pk_type_broadcast = 0x01;
constexpr inline u8 dl_pk_type_auth = 0x02;
constexpr inline u8 dl_pk_type_start_dist = 0x03;
constexpr inline u8 dl_pk_type_distribute = 0x04;
constexpr inline u8 dl_pk_type_finish_dist = 0x05;
constexpr inline u8 dl_pk_type_start_game = 0x06;

constexpr inline std::array<u8, 4> dl_pk_head_broadcast_header = {dl_pk_type_broadcast, 0x02};
constexpr inline std::array<u8, 4> dl_pk_head_auth_header = {dl_pk_type_auth, 0x02};
constexpr inline std::array<u8, 4> dl_pk_head_start_dist_header = {dl_pk_type_start_dist, 0x02};
constexpr inline std::array<u8, 4> dl_pk_head_distribute_header = {dl_pk_type_distribute, 0x02};
constexpr inline std::array<u8, 4> dl_pk_head_finish_dist_header = {dl_pk_type_finish_dist, 0x02};
constexpr inline std::array<u8, 4> dl_pk_head_start_game_header = {dl_pk_type_start_game, 0x02};

struct DLPPacketHeader {
    union {
        std::array<u8, 4> magic;
        struct {
            u8 type;
            u8 mag0x02; // usually 0x02, but appears to depend on the internal state of a random
                        // buffer
            u16 padding;
        };
    };
    u16_be size;      // size of the whole packet, including the header
    u16_le must_be_2; // always set to 2
    u32 checksum;     // always calculate
    u8 packet_index;  // starts at 0
    union {
        std::array<u8, 3> resp_id; // client: copies this from host packet when responding to it
        struct {                   // for server use
            u8 pk_seq_num;
            std::array<u8, 2> resp_id_high;
        };
    };
};

static_assert(sizeof(DLPPacketHeader) == 0x10);

// bool with 3ds padding included
struct DLPPacketBool {
    union {
        u32 raw;
        struct {
            u8 active_value : 1;
            u8 padding : 7;
            std::array<u8, 3> padding2;
        };
    };
    operator bool() {
        return active_value;
    }
    DLPPacketBool& operator=(const bool& o) {
        raw = 0x0;
        active_value = o;
        return *this;
    }
};

static_assert(sizeof(DLPPacketBool) == sizeof(u32_be));

#pragma pack(push, 2)
struct DLPBroadcastPacket1 {
    DLPPacketHeader head;
    u64_be child_title_id; // title id of the child being broadcasted
    std::array<u8, 2> unk1;
    std::array<u8, 2> unk6;    // need 0x1 0x1
    u32_be content_block_size; // full byte size of content block
    u8 max_nodes;
    std::array<u8, 7> unk2;
    u64 unk3;
    u64 unk4; // all 0s
    u32_be transfer_size;
    u32_be required_size;
    std::array<u16_be, 64> title_short;
    std::array<u16_be, 128> title_long;
    std::array<u16_be, 0x9c> icon_part;
    u64 unk;
};
#pragma pack(pop)

static_assert(sizeof(DLPBroadcastPacket1) == 768);

struct DLPBroadcastPacket2 {
    DLPPacketHeader head;
    std::array<u16_be, 0x2cc> icon_part;
};

static_assert(sizeof(DLPBroadcastPacket2) == 1448);

struct DLPBroadcastPacket3 {
    DLPPacketHeader head;
    std::array<u16_be, 0x2cc> icon_part;
};

static_assert(sizeof(DLPBroadcastPacket3) == 1448);

struct DLPBroadcastPacket4 {
    DLPPacketHeader head;
    std::array<u16_be, 0x2cc> icon_part;
};

static_assert(sizeof(DLPBroadcastPacket4) == 1448);

struct DLPBroadcastPacket5 {
    DLPPacketHeader head;
    std::array<u8, 0x8> unk1;
    std::array<u8, 0x8> unk2;
    std::array<u8, 0x598> unk3;
};

static_assert(sizeof(DLPBroadcastPacket5) == 1464);

// auth session
struct DLPSrvr_Auth {
    DLPPacketHeader head;
    u32 unk1; // 0x0
};

static_assert(sizeof(DLPSrvr_Auth) == 0x14);

struct DLPClt_AuthAck {
    DLPPacketHeader head;
    DLPPacketBool initialized; // true
    std::array<u8, 2> padding;
    std::array<u8, 2> resp_id; // very important! game specific?
};

static_assert(sizeof(DLPClt_AuthAck) == 0x18);

// start distribution
struct DLPSrvr_StartDistribution {
    DLPPacketHeader head;
    DLPPacketBool initialized; // 0x1
};

static_assert(sizeof(DLPSrvr_StartDistribution) == 0x14);

struct DLPClt_StartDistributionAck_NoContentNeeded {
    DLPPacketHeader head;
    DLPPacketBool initialized; // 0x1
    u16_be unk2;               // 0x0. if 0x20, move to ContentNeeded
    u16_be unk3;               // 0x0
};

static_assert(sizeof(DLPClt_StartDistributionAck_NoContentNeeded) == 0x18);

struct DLPClt_StartDistributionAck_ContentNeeded {
    DLPPacketHeader head;
    DLPPacketBool initialized; // 0x1
    u16_be unk2;               // BE 0x20 unk important!
    u16_be unk3;               // 0x0
    DLPPacketBool unk4;        // 0x1
    u32_be unk5;               // 0x0
    std::array<u8, 0x18> unk_body;
};

static_assert(sizeof(DLPClt_StartDistributionAck_ContentNeeded) == 0x38);

// perform distribution of content
// packet_index is 1
#pragma pack(push, 1)
struct DLPSrvr_ContentDistributionFragment {
    DLPPacketHeader head;
    u8 unk2;
    std::array<u8, 3> padding;
    u32_be content_block; // content_block index starting at 0
    u16_be frag_index;    // BE % dlp_content_block_length
    u16_be frag_size;     // BE
    u8 content_fragment[];
};
#pragma pack(pop)

static_assert(sizeof(DLPSrvr_ContentDistributionFragment) == 28);

// finish receiving content
struct DLPSrvr_FinishContentUpload {
    DLPPacketHeader head;
    DLPPacketBool initialized; // 0x1
    u32_be seq_num;            // BE starts at 0x0 and copies whatever number the ack gives it
};

static_assert(sizeof(DLPSrvr_FinishContentUpload) == 0x18);

// it sends this to clients during distribution
#pragma pack(push, 2)
struct DLPClt_FinishContentUploadAck {
    DLPPacketHeader head;
    DLPPacketBool initialized; // 0x1
    u8 finished_cur_block;     // 0x1 if finished downloading current block
    u8 needs_content;          // 0x1 if downloading conetnt
    u32_be seq_ack;            // BE client increments this every ack
    u16 unk4;                  // 0x0
};
#pragma pack(pop)

static_assert(sizeof(DLPClt_FinishContentUploadAck) == 0x1C);

// start game
// these will keep sending until
// the final command is given
struct DLPSrvr_BeginGame {
    DLPPacketHeader head;
    u32_le unk1; // 0x1
    u32_le unk2; // 0x9 could be DLP_Srvr_State
};

static_assert(sizeof(DLPSrvr_BeginGame) == 0x18);

struct DLPClt_BeginGameAck {
    DLPPacketHeader head;
    u32_le unk1; // 0x1
    u32_le unk2; // 0x9 could be DLP_Clt_State
};

static_assert(sizeof(DLPClt_BeginGameAck) == 0x18);

// packet_index is 1. this is not acked
struct DLPSrvr_BeginGameFinal {
    DLPPacketHeader head;
    u32_le unk1; // 0x1
    std::array<u8, 9> wireless_reboot_passphrase;
    u8 unk2;     // 0x09 could be server state
    u16 padding; // 0x00 0x00
};

static_assert(sizeof(DLPSrvr_BeginGameFinal) == 0x20);

// END BIG ENDIAN

class DLP_Base {
public:
    static Loader::SMDH::TitleLanguage SystemLanguageToSMDHLanguage(CFG::SystemLanguage);

protected:
    DLP_Base(Core::System& s);
    virtual ~DLP_Base() = default;

    virtual std::shared_ptr<Kernel::SessionRequestHandler> GetServiceFrameworkSharedPtr() = 0;
    virtual bool IsHost() = 0;

    Core::System& system;

    std::shared_ptr<Kernel::SharedMemory> dlp_sharedmem;
    std::shared_ptr<Kernel::SharedMemory> uds_sharedmem;

    std::shared_ptr<Kernel::Event> dlp_status_event; // out
    std::shared_ptr<Kernel::Event> uds_status_event; // in

    bool should_verify_checksum = false;

    const u32 uds_sharedmem_size = 0x4000;
    const u32 uds_version = 0x400;
    const u8 num_broadcast_packets = 5;
    u32 dlp_sharedmem_size{};

    constexpr static inline u32 dlp_poll_rate_normal = 100;

    constexpr static inline u16 dlp_wlan_comm_id = 0x2810;
    constexpr static inline u16 dlp_net_info_channel = 0x1;
    constexpr static inline u16 dlp_bind_node_id = 0x1;
    constexpr static inline u32 dlp_recv_buffer_size = 0x3c00;
    constexpr static inline u8 dlp_broadcast_data_channel = 0x1;
    constexpr static inline u8 dlp_client_data_channel = 0x2;
    constexpr static inline u8 dlp_host_network_node_id = 0x1;

    const u8 first_client_node_id = dlp_host_network_node_id + 1;

    DLP_Username username;
    // stubbed as HLE NWM_UDS does not check this. Should be: 0km@tsa$uhmy1a0sa + nul
    std::vector<u8> dlp_password_buf{};
    std::array<u8, 9> wireless_reboot_passphrase;

    constexpr static inline u32 dlp_content_block_length = 182;

    std::shared_ptr<CFG::Module> GetCFG();
    std::shared_ptr<NWM::NWM_UDS> GetUDS();

    void GetEventDescription(Kernel::HLERequestContext& ctx);

    void InitializeDlpBase(u32 shared_mem_size, std::shared_ptr<Kernel::SharedMemory> shared_mem,
                           std::shared_ptr<Kernel::Event> event, DLP_Username username);
    void FinalizeDlpBase();

    bool ConnectToNetworkAsync(NWM::NetworkInfo net_info, NWM::ConnectionType conn_type,
                               std::vector<u8> passphrase);
    // out_node: if node_id is broadcast, check which node we received from
    int RecvFrom(u16 node_id, std::vector<u8>& buffer, u16* out_node = nullptr);
    bool SendTo(u16 node_id, u8 data_channel, std::vector<u8>& buffer, u8 flags = 0);

    static std::u16string DLPUsernameAsString16(DLP_Username uname);
    static DLP_Username String16AsDLPUsername(std::u16string str);
    static DLPNodeInfo UDSToDLPNodeInfo(NWM::NodeInfo node_info);
    template <typename T>
    static T* GetPacketBody(std::vector<u8>& b) {
        if (b.size() < sizeof(T)) {
            LOG_CRITICAL(Service_DLP, "Packet size is too small to fit content {} < {}", b.size(),
                         sizeof(T));
            return nullptr;
        }
        return reinterpret_cast<T*>(b.data());
    }
    static DLPPacketHeader* GetPacketHead(std::vector<u8>& b) {
        if (b.size() < sizeof(DLPPacketHeader)) {
            LOG_CRITICAL(Service_DLP, "Packet is too small to fit a DLP header");
            return nullptr;
        }
        return reinterpret_cast<DLPPacketHeader*>(b.data());
    }

    static u32 GeneratePKChecksum(u32 aes_value, void* input_buffer, u32 packet_size);

    template <typename T>
    T* PGen_SetPK(std::array<u8, 4> magic, u8 packet_index, std::array<u8, 3> resp_id) {
        if (!sm_packet_sender_session.try_acquire()) {
            LOG_DEBUG(Service_DLP,
                      "Tried to send 2 packets concurrently, causing blocking on this thread");
            sm_packet_sender_session.acquire();
        }
        send_packet_ctx.resize(sizeof(T));
        auto ph = GetPacketHead(send_packet_ctx);
        ph->magic = magic;
        ph->size = sizeof(T);
        ph->must_be_2 = 2;
        ph->resp_id = resp_id;
        ph->packet_index = packet_index;
        return GetPacketBody<T>(send_packet_ctx);
    }
    void PGen_SendPK(u32 aes, u16 node_id, u8 data_channel, u8 flags = 0) {
        ASSERT(send_packet_ctx.size() >= sizeof(DLPPacketHeader));
        auto ph = GetPacketHead(send_packet_ctx);
        ASSERT(ph->size == send_packet_ctx.size());
        ph->checksum = 0;
        ph->checksum = GeneratePKChecksum(aes, ph, ph->size);
        SendTo(node_id, data_channel, send_packet_ctx, flags);
        send_packet_ctx.clear();
        sm_packet_sender_session.release();
    }
    // adds x bytes to the packet and refreshes the
    // packet body pointer
    template <typename T>
    void PGen_AddPacketData(T*& p, size_t num_bytes) {
        send_packet_ctx.resize(send_packet_ctx.size() + num_bytes);
        auto head = GetPacketHead(send_packet_ctx);
        head->size = send_packet_ctx.size();
        p = GetPacketBody<T>(send_packet_ctx);
    }
    // input the host mac address
    u32 GenDLPChecksumKey(Network::MacAddress mac_addr);
    static void DLPEncryptCTR(void* out, size_t size, const u8* iv_ctr);
    static bool ValidatePacket(u32 aes, void* pk, size_t sz, bool checksum = true);

    static u32 GetNumFragmentsFromTitleSize(u32 tsize);

private:
    std::binary_semaphore sm_packet_sender_session{1};
    std::vector<u8> send_packet_ctx;
};

} // namespace Service::DLP
