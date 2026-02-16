// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "dlp_base.h"

#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include "common/alignment.h"
#include "common/swap.h"
#include "common/timer.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/service/nwm/uds_data.h"
#include "core/hle/service/service.h"
#include "core/hw/aes/key.h"
#include "core/hw/unique_data.h"

namespace Service::DLP {

DLP_Base::DLP_Base(Core::System& s) : system(s) {}

std::shared_ptr<CFG::Module> DLP_Base::GetCFG() {
    return Service::CFG::GetModule(system);
}

std::shared_ptr<NWM::NWM_UDS> DLP_Base::GetUDS() {
    return system.ServiceManager().GetService<Service::NWM::NWM_UDS>("nwm::UDS");
}

std::u16string DLP_Base::DLPUsernameAsString16(DLP_Username uname) {
    std::u16string strUsername;
    for (auto c : uname) {
        strUsername.push_back(c);
    }
    return strUsername;
}

DLP_Username DLP_Base::String16AsDLPUsername(std::u16string str) {
    DLP_Username out{};
    u32 num_chars_copy = std::min<u32>(out.size(), str.size());
    memcpy(out.data(), str.data(), num_chars_copy * sizeof(u16_le));
    return out;
}

std::string DLP_Base::MacAddrToString(Network::MacAddress mac_addr) {
    std::string out;
    for (auto c : mac_addr) {
        out += std::format("0x{:x} ", c);
    }
    out.pop_back();
    return out;
}

DLPNodeInfo DLP_Base::UDSToDLPNodeInfo(NWM::NodeInfo node_info) {
    DLPNodeInfo out;
    out.username = node_info.username;
    out.network_node_id = node_info.network_node_id;
    out.friend_code_seed = node_info.friend_code_seed;
    return out;
}

u16 DLP_Base::d_htons(u16 n) {
    if constexpr (std::endian::native == std::endian::little) {
        return Common::swap16(n);
    } else {
        return n;
    }
}

u16 DLP_Base::d_ntohs(u16 n) {
    if constexpr (std::endian::native == std::endian::little) {
        return Common::swap16(n);
    } else {
        return n;
    }
}

u32 DLP_Base::d_htonl(u32 n) {
    if constexpr (std::endian::native == std::endian::little) {
        return Common::swap32(n);
    } else {
        return n;
    }
}

u32 DLP_Base::d_ntohl(u32 n) {
    if constexpr (std::endian::native == std::endian::little) {
        return Common::swap32(n);
    } else {
        return n;
    }
}

u64 DLP_Base::d_ntohll(u64 n) {
    if constexpr (std::endian::native == std::endian::little) {
        return Common::swap64(n);
    } else {
        return n;
    }
}

u64 DLP_Base::d_htonll(u64 n) {
    if constexpr (std::endian::native == std::endian::little) {
        return Common::swap64(n);
    } else {
        return n;
    }
}

void DLP_Base::GetEventDescription(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    LOG_WARNING(Service_DLP, "(STUBBED) called");

    DLPEventDescription desc{};

    IPC::RequestBuilder rb = rp.MakeBuilder(8, 0);

    rb.Push(ResultSuccess);
    rb.PushRaw(desc);
}

void DLP_Base::InitializeDlpBase(u32 shared_mem_size,
                                 std::shared_ptr<Kernel::SharedMemory> shared_mem,
                                 std::shared_ptr<Kernel::Event> event, DLP_Username uname) {
    dlp_sharedmem_size = shared_mem_size;
    dlp_sharedmem = shared_mem;
    dlp_status_event = event;
    username = uname;

    uds_sharedmem =
        system.Kernel()
            .CreateSharedMemory(nullptr, uds_sharedmem_size, Kernel::MemoryPermission::ReadWrite,
                                Kernel::MemoryPermission::ReadWrite, 0, Kernel::MemoryRegion::BASE,
                                "NWM::UDS:SharedMemory")
            .Unwrap();

    auto dlp_pass = "0km@tsa$uhmy1a0sa";
    dlp_password_buf.resize(sizeof(dlp_pass));
    memcpy(dlp_password_buf.data(), dlp_pass, sizeof(dlp_pass));

    NWM::NodeInfo cnode_info{
        .friend_code_seed = HW::UniqueData::GetLocalFriendCodeSeedB().body.friend_code_seed,
        .username = uname,
    };
    GetUDS()->Initialize(uds_sharedmem_size, cnode_info, uds_version, uds_sharedmem);
}

void DLP_Base::FinalizeDlpBase() {
    GetUDS()->ShutdownHLE();
    dlp_sharedmem.reset();
    uds_sharedmem.reset();
    dlp_status_event.reset();
    username = DLP_Username{};
}

bool DLP_Base::ConnectToNetworkAsync(NWM::NetworkInfo net_info, NWM::ConnectionType conn_type,
                                     std::vector<u8> passphrase) {
    auto uds = GetUDS();

    // we need to make this event manually
    uds->connection_event =
        system.Kernel().CreateEvent(Kernel::ResetType::OneShot, "dlp_connect_to_beacon");

    uds->ConnectToNetworkHLE(net_info, static_cast<u8>(conn_type), passphrase);

    // wait for connection
    Common::Timer t_time_out;
    t_time_out.Start();
    bool timed_out = false;
    while (true) { // busy wait, TODO: change to not busy wait?
        if (uds->GetConnectionStatusHLE().status == NWM::NetworkStatus::ConnectedAsSpectator) {
            // connected
            break;
        }
        constexpr u32 connect_network_timeout_ms = 3000;
        if (t_time_out.GetTimeElapsed().count() > connect_network_timeout_ms) {
            timed_out = true;
            break;
        }
    }

    if (timed_out) {
        // TODO: fix unlikely race cond, timeout happens, we disconnect, then server registers our
        // connection
        uds->DisconnectNetworkHLE();
        LOG_ERROR(Service_DLP, "Timed out when trying to connect to beacon");
        return false;
    }

    if (uds->GetConnectionStatusHLE().status != NWM::NetworkStatus::ConnectedAsSpectator &&
        uds->GetConnectionStatusHLE().status != NWM::NetworkStatus::ConnectedAsClient) {
        // error!
        LOG_ERROR(Service_DLP, "Could not connect spec to network, connected as 0x{:x}",
                  static_cast<u32>(uds->GetConnectionStatusHLE().status));
        return false;
    }

    return true;
}

int DLP_Base::RecvFrom(u16 node_id, std::vector<u8>& buffer) {
    constexpr u32 max_pullpacket_size = 0x3c00;
    std::vector<u8> buffer_out;

    NWM::SecureDataHeader secure_data;
    auto uds = GetUDS();
    if (!uds) {
        LOG_ERROR(Service_DLP, "Could not get get pointer to UDS service!");
        return 0;
    }
    auto ret =
        uds->PullPacketHLE(node_id, max_pullpacket_size, static_cast<u32>(max_pullpacket_size) >> 2,
                           buffer_out, &secure_data);

    if (!ret) {
        return 0;
    }

    buffer = buffer_out;
    return *ret; // size
}

bool DLP_Base::SendTo(u16 node_id, u8 data_channel, std::vector<u8>& buffer, u8 flags) {
    constexpr u32 max_sendto_size = 0x3c00;

    if (buffer.size() > max_sendto_size) {
        LOG_WARNING(Service_DLP, "Packet size is larger than 0x{:x}", max_sendto_size);
    }

    return GetUDS()->SendToHLE(node_id, data_channel, buffer.size(), flags, buffer) ==
           NWM::ResultStatus::ResultSuccess;
}

u32 DLP_Base::GeneratePKChecksum(u32 aes_value, void* _input_buffer, u32 packet_size) {
    auto input_buffer = reinterpret_cast<u8*>(_input_buffer);

    u32 working_hash = 0;
    // add all word aligned bytes
    for (u32 i = 0; i < packet_size / sizeof(u32); i++) {
        u32 inp_buf_word = reinterpret_cast<u32*>(input_buffer)[i];
        working_hash += Common::swap32(inp_buf_word);
    }
    // add any remaining non word-aligned bytes
    if (u32 num_bytes_non_aligned = packet_size & 3; num_bytes_non_aligned != 0) {
        u32 non_aligned = 0;
        memcpy(&non_aligned, input_buffer + packet_size - num_bytes_non_aligned,
               num_bytes_non_aligned);
        working_hash += Common::swap32(non_aligned);
    }
    // hash by the aes value
    u8 num_extra_hash = (reinterpret_cast<u8*>(&aes_value)[3] & 0b0111) + 2;
    u8 num_shift_extra_hash = (reinterpret_cast<u8*>(&aes_value)[2] & 0b1111) + 4;
    u32 aes_swap = Common::swap32(aes_value);
    for (u8 i = 0; i < num_extra_hash; i++) {
        working_hash =
            (working_hash >> num_shift_extra_hash | working_hash << num_shift_extra_hash) ^
            aes_swap;
    }
    return Common::swap32(working_hash);
}

u32 DLP_Base::GenDLPChecksumKey(Network::MacAddress mac_addr) {
    auto dlp_iv_ctr_buf = HW::AES::GetDlpChecksumModIv();

    std::array<u8, 0x10> ctr_encrypt_buf{};
    for (u32 i = 0; i < 0x10; i++) {
        ctr_encrypt_buf[i] = mac_addr[i % 6] ^ dlp_iv_ctr_buf[i];
    }

    u32 val_out = 0;
    DLPEncryptCTR(&val_out, sizeof(val_out), ctr_encrypt_buf.data());
    return val_out;
}

bool DLP_Base::ValidatePacket(u32 aes, void* pk, size_t sz, bool checksum) {
    if (sz < sizeof(DLPPacketHeader)) {
        LOG_ERROR(Service_DLP, "Packet size is too small");
        return false;
    }

    auto ph = reinterpret_cast<DLPPacketHeader*>(pk);

    if (d_ntohs(ph->size) != sz) {
        LOG_ERROR(Service_DLP, "Packet size in header does not match size received");
        return false;
    }

    std::vector<u8> pk_copy;
    pk_copy.resize(sz);
    memcpy(pk_copy.data(), pk, sz);

    if (checksum) {
        auto ph_cpy = reinterpret_cast<DLPPacketHeader*>(pk_copy.data());
        ph_cpy->checksum = 0;
        u32 new_checksum = GeneratePKChecksum(aes, pk_copy.data(), pk_copy.size());
        if (new_checksum != ph->checksum) {
            LOG_ERROR(Service_DLP, "Could not verify packet checksum 0x{:x} != 0x{:x}",
                      new_checksum, ph->checksum);
            return false;
        }
    }
    return true;
}

u32 DLP_Base::GetNumFragmentsFromTitleSize(u32 tsize) {
    return Common::AlignUp(tsize - broad_title_size_diff, content_fragment_size) /
           content_fragment_size;
}

} // namespace Service::DLP
