#pragma once

#include "core/hle/service/service.h"
#include "core/hle/service/cfg/cfg.h"
#include "core/core.h"
#include "core/hle/service/nwm/nwm_uds.h"

#include <semaphore>

namespace Service::DLP {

using DLP_Username = std::array<u16_le, 10>;
constexpr inline u64 DLP_CHILD_TID_HIGH = 0x0004000100000000;
constexpr inline u32 content_fragment_size = 1440;

struct DLPTitleInfo {
    u32 unique_id; // games look at this to make sure it's their title info
    u32 variation;
    Network::MacAddress mac_addr;
    u16 version; // XX: probably?
    std::array<u8, 16> age_ratings;
    std::array<u16, 64> short_description; // UTF-16
    std::array<u16, 128> long_description; // UTF-16
    std::array<u8, 0x1200> icon;  // 48x48, RGB565
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

// START BIG ENDIAN

constexpr inline u8 dl_pk_type_broadcast   = 0x01;
constexpr inline u8 dl_pk_type_auth        = 0x02;
constexpr inline u8 dl_pk_type_start_dist  = 0x03;
constexpr inline u8 dl_pk_type_distribute  = 0x04;
constexpr inline u8 dl_pk_type_finish_dist = 0x05;
constexpr inline u8 dl_pk_type_start_game  = 0x06;

constexpr inline std::array<u8, 4> dl_pk_head_broadcast_header   = {dl_pk_type_broadcast,   0x02};
constexpr inline std::array<u8, 4> dl_pk_head_auth_header        = {dl_pk_type_auth,        0x02};
constexpr inline std::array<u8, 4> dl_pk_head_start_dist_header  = {dl_pk_type_start_dist,  0x02};
constexpr inline std::array<u8, 4> dl_pk_head_distribute_header  = {dl_pk_type_distribute,  0x02};
constexpr inline std::array<u8, 4> dl_pk_head_finish_dist_header = {dl_pk_type_finish_dist, 0x02};
constexpr inline std::array<u8, 4> dl_pk_head_start_game_header  = {dl_pk_type_start_game,  0x02};

struct DLPPacketHeader {
    union {
        std::array<u8, 4> magic;
        struct {
            u8 type;
            u8 mag0x02;
            u16 unk; // usually 0x00 0x00
        };
    };
    u16 size; // size of the whole packet, including the header
	std::array<u8, 2> unk1; // always 0x02 0x00
	u32 checksum; // always calculate
	u8 packet_index; // starts at 0
	std::array<u8, 3> resp_id; // copies this from host packet when responding to it
};

static_assert(sizeof(DLPPacketHeader) == 0x10);

constexpr u32 broad_title_size_diff = 111360;

#pragma pack(push, 2)
struct DLPBroadcastPacket1 {
    DLPPacketHeader head;
    u64 child_title_id; // title id of the child being broadcasted
    u64 unk1;
    u64 unk2;
    u64 unk3;
    u64 unk4; // all 0s
    u64 size; // size minus broad_title_size_diff
	std::array<u16, 64> title_short;
	std::array<u16, 128> title_long;
	std::array<u8, 0x138> icon_part;
	u64 unk;
};
#pragma pack(pop)

static_assert(sizeof(DLPBroadcastPacket1) == 768);

struct DLPBroadcastPacket2 {
	DLPPacketHeader head;
	std::array<u8, 0x598> icon_part;
};

static_assert(sizeof(DLPBroadcastPacket2) == 1448);

struct DLPBroadcastPacket3 {
	DLPPacketHeader head;
	std::array<u8, 0x598> icon_part;
};

static_assert(sizeof(DLPBroadcastPacket3) == 1448);

struct DLPBroadcastPacket4 {
	DLPPacketHeader head;
	std::array<u8, 0x598> icon_part;
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
    std::array<u8, 4> unk1; // 0x1
    std::array<u8, 2> unk2; // 0x0 could be padding
    std::array<u8, 2> resp_id; // very important! game specific?
};

static_assert(sizeof(DLPClt_AuthAck) == 0x18);

// start distribution
struct DLPSrvr_StartDistribution {
    DLPPacketHeader head;
    u32 unk1; // 0x1
};

static_assert(sizeof(DLPSrvr_StartDistribution) == 0x14);

struct DLPClt_StartDistributionAck_NoContentNeeded {
    DLPPacketHeader head;
    std::array<u8, 4> unk1; // 0x1
    u32 unk2; // 0x0
};

static_assert(sizeof(DLPClt_StartDistributionAck_NoContentNeeded) == 0x18);

struct DLPClt_StartDistributionAck_ContentNeeded {
    DLPPacketHeader head;
    u32 unk1; // 0x1
    // TODO: combine unk2 & unk3 into a single dword
    u16 unk2; // BE 0x20 unk important!
    u16 unk3; // 0x0
    u32 unk4; // 0x1
    u32 unk5; // 0x0
    std::array<u8, 0x18> unk_body;
};

static_assert(sizeof(DLPClt_StartDistributionAck_ContentNeeded) == 0x38);

// perform distribution of content
// packet_index is 1
struct DLPSrvr_ContentDistributionFragment {
    DLPPacketHeader head;
    u32 content_magic; // extra magic value
    u32 unk1; // 0x1 BE
    u16 frag_index; // BE % dlp_content_block_length
    u16 frag_size; // 0x5 0xa2
    std::array<u8, content_fragment_size> content_fragment;
};

static_assert(sizeof(DLPSrvr_ContentDistributionFragment) == 1468);

// finish receiving content
struct DLPSrvr_FinishContentUpload {
    DLPPacketHeader head;
    u32 unk1; // 0x1
    u32 seq_num; // BE starts at 0x0 and copies whatever number the ack gives it
};

static_assert(sizeof(DLPSrvr_FinishContentUpload) == 0x18);

// it sends this to clients during distribution
#pragma pack(push, 2)
struct DLPClt_FinishContentUploadAck {
    DLPPacketHeader head;
    u32 unk1; // 0x1
    u8 unk2; // 0x1
    u8 unk3; // 0x1 if downloading conetnt
    u32 seq_ack; // BE client increments this every ack
    u16 unk4; // 0x0
};
#pragma pack(pop)

static_assert(sizeof(DLPClt_FinishContentUploadAck) == 0x1C);

// start game
// these will keep sending until
// the final command is given
struct DLPSrvr_BeginGame {
    DLPPacketHeader head;
    u32 unk1; // 0x1
    u32 unk2; // 0x9 could be DLP_Srvr_State
};

static_assert(sizeof(DLPSrvr_BeginGame) == 0x18);

struct DLPClt_BeginGameAck {
    DLPPacketHeader head;
    u32 unk1; // 0x1
    u32 unk2; // 0x9 could be DLP_Clt_State
};

static_assert(sizeof(DLPClt_BeginGameAck) == 0x18);

// packet_index is 1. this is not acked
struct DLPSrvr_BeginGameFinal {
    DLPPacketHeader head;
    u32 unk1; // 0x1
    std::array<u8, 9> wireless_reboot_passphrase;
    u8 unk2; // 0x09 could be server state
    u16 padding; // 0x00 0x00
};

static_assert(sizeof(DLPSrvr_BeginGameFinal) == 0x20);

// END BIG ENDIAN

class DLP_Base {
public:
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
    
    const u32 uds_sharedmem_size = 0x4000;
    const u32 uds_version = 0x400;
    const u32 recv_buffer_size = 0x3c00;
    const u32 dlp_channel = 0x10;
    const u8 num_broadcast_packets = 5;
    u32 dlp_sharedmem_size{};
    
    DLP_Username username;
    std::vector<u8> dlp_password_buf;
    std::array<u8, 9> wireless_reboot_passphrase;
    
    const u32 dlp_content_block_length = 182;
    
    std::shared_ptr<CFG::Module> GetCFG();
    std::shared_ptr<NWM::NWM_UDS> GetUDS();
    
    void GetEventDescription(Kernel::HLERequestContext& ctx);
    
    void InitializeDlpBase(u32 shared_mem_size, std::shared_ptr<Kernel::SharedMemory> shared_mem, std::shared_ptr<Kernel::Event> event, DLP_Username username);
    void FinalizeDlpBase();
    
    bool ConnectToNetworkAsync(NWM::NetworkInfo net_info, NWM::ConnectionType conn_type, std::vector<u8> passphrase);
    int RecvFrom(u16 node_id, std::vector<u8>& buffer);
    bool SendTo(u16 node_id, u8 data_channel, std::vector<u8>& buffer, u8 flags = 0);
    
    static std::u16string DLPUsernameAsString16(DLP_Username uname);
    static DLP_Username String16AsDLPUsername(std::u16string str);
    static std::string MacAddrToString(Network::MacAddress);
    static DLPNodeInfo UDSToDLPNodeInfo(NWM::NodeInfo node_info);
    static u16 d_htons(u16);
    static u16 d_ntohs(u16);
    static u32 d_htonl(u32 n);
    static u32 d_ntohl(u32 n);
    static u64 d_ntohll(u64);
    static u64 d_htonll(u64);
    template <typename T>
    static T *GetPacketBody(std::vector<u8>& b) {
        if (b.size() < sizeof(T)) {
            LOG_CRITICAL(Service_DLP, "Packet size is too small to fit content {} < {}", b.size(), sizeof(T));
        }
        return reinterpret_cast<T*>(b.data());
    }
    static DLPPacketHeader *GetPacketHead(std::vector<u8>& b) {
        if (b.size() < sizeof(DLPPacketHeader)) {
            LOG_CRITICAL(Service_DLP, "Packet is too small to fit a DLP header");
        }
        return reinterpret_cast<DLPPacketHeader*>(b.data());
    }
    void GeneratePKChecksum(void *output_word, void *input_buffer, u32 packet_size);
    
    template <typename T>
    T *PGen_SetPK(std::array<u8, 4> magic, u8 packet_index, std::array<u8, 3> resp_id) {
        sm_packet_sender_session.acquire();
        send_packet_ctx.resize(sizeof(T));
        auto ph = GetPacketHead(send_packet_ctx);
        ph->magic = magic;
        ph->size = d_htons(sizeof(T));
        ph->unk1 = {0x02, 0x00};
        ph->resp_id = resp_id;
        ph->packet_index = packet_index;
        return GetPacketBody<T>(send_packet_ctx);
    }
    void PGen_SendPK(u16 node_id, u8 data_channel, u8 flags = 0) {
        auto ph = GetPacketHead(send_packet_ctx);
        ph->checksum = 0;
        GeneratePKChecksum(&ph->checksum, ph, d_ntohs(ph->size));
        SendTo(node_id, data_channel, send_packet_ctx, flags);
        send_packet_ctx.clear();
        sm_packet_sender_session.release();
    }
    // input the host mac address
    void GenDLPChecksumKey(Network::MacAddress mac_addr);
    static void DLPEncryptCTR(void *out, size_t size, const u8 *iv_ctr);
    bool ValidatePacket(void *pk, size_t sz, bool checksum = true);
private:
    std::binary_semaphore sm_packet_sender_session{1};
    std::vector<u8> send_packet_ctx;
    u32 aes_value = 0;
};

} // namespace Service::DLP


template <typename T>
inline std::string FmtArray(T a, size_t max_print = -1) {
    std::string fmt;
    for (int i = 0; auto c : a) {
        if (i == max_print) {
            break;
        }
        if (i % 16 == 0) {
            fmt += "\n";
        }
        fmt += std::format("{:2x} ", c);
        i++;
    }
    if (fmt.size())
        fmt.pop_back();
    return fmt;
}