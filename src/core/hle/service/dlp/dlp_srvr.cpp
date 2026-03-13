// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/archives.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/file_sys/archive_ncch.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/event.h"
#include "core/hle/kernel/shared_memory.h"
#include "core/hle/result.h"
#include "core/hle/romfs.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/dlp/dlp_srvr.h"
#include "core/hle/service/fs/archive.h"
#include "core/hle/service/fs/fs_user.h"

SERIALIZE_EXPORT_IMPL(Service::DLP::DLP_SRVR)

namespace Service::DLP {

std::shared_ptr<Kernel::SessionRequestHandler> DLP_SRVR::GetServiceFrameworkSharedPtr() {
    return shared_from_this();
}

std::shared_ptr<FS::FS_USER> DLP_SRVR::GetFS() {
    return system.ServiceManager().GetService<FS::FS_USER>("fs:USER");
}

u8 DLP_SRVR::GetSrvrState() {
    std::scoped_lock lock(srvr_state_mutex);
    return static_cast<u32>(srvr_state);
}

void DLP_SRVR::InitializeSrvrCommon(u32 shared_mem_size, u8 max_clnts, u32 process_id,
                                    DLP_Username uname,
                                    std::shared_ptr<Kernel::SharedMemory> shared_mem,
                                    std::shared_ptr<Kernel::Event> event) {
    InitializeDlpBase(shared_mem_size, shared_mem, event, uname);

    // this gets our mac address, and we will be the host
    host_mac_address = GetUDS()->GetMacAddress();
    max_clients = max_clnts;
    client_states.resize(max_clients);
    // set up client array
    for (u8 i = first_client_node_id; auto& cl : client_states) {
        cl.network_node_id = i;
        cl.state = ClientState::NotJoined;
        cl.pk_seq_num = 0;
        i++;
    }

    if (!CacheContentFileInMemory(process_id)) {
        LOG_WARNING(
            Service_DLP,
            "Unable to load cia file. You will not be able to perform content distribution!");
    }

    std::scoped_lock lock(srvr_state_mutex);

    srvr_state = DLP_Srvr_State::Initialized;
}

bool DLP_SRVR::CacheContentFileInMemory(u32 process_id) {
    auto fs = GetFS();

    if (!fs) {
        LOG_ERROR(Service_DLP, "Could not get pointer to fs");
        return false;
    }

    auto title_info = fs->GetProgramLaunchInfo(process_id);

    // get special content index. could have made a new
    // HLE func in FS, but this is so small that it
    // doesn't matter
    ResultVal<u16> index;
    if (title_info->media_type == FS::MediaType::GameCard) {
        index = fs->GetSpecialContentIndexFromGameCard(title_info->program_id,
                                                       FS::SpecialContentType::DLPChild);
    } else {
        index = fs->GetSpecialContentIndexFromTMD(title_info->media_type, title_info->program_id,
                                                  FS::SpecialContentType::DLPChild);
    }

    if (!index) {
        LOG_ERROR(Service_DLP, "Could not get special content index from program id 0x{:x}",
                  title_info->program_id);
        return false;
    }

    // read as ncch to find the content
    FileSys::NCCHArchive container(title_info->program_id, title_info->media_type);

    std::array<char, 8> exefs_filepath{};
    FileSys::Path file_path =
        FileSys::MakeNCCHFilePath(FileSys::NCCHFileOpenType::NCCHData, *index,
                                  FileSys::NCCHFilePathType::RomFS, exefs_filepath);
    FileSys::Mode open_mode = {};
    open_mode.read_flag.Assign(1);
    const u32 open_attributes_none = 0;
    auto file_result = container.OpenFile(file_path, open_mode, open_attributes_none);

    if (file_result.Failed()) {
        LOG_ERROR(Service_DLP, "Could not open DLP child archive");
        return false;
    }

    auto romfs = std::move(file_result).Unwrap();

    std::vector<u8> romfs_buffer(romfs->GetSize());
    romfs->Read(0, romfs_buffer.size(), romfs_buffer.data());
    romfs->Close();

    u64 dlp_child_tid = (title_info->program_id & 0xFFFFFFFF) | DLP_CHILD_TID_HIGH;
    auto filename = fmt::format("{:016x}.cia", dlp_child_tid);

    LOG_INFO(Service_DLP, "Loading romfs file: {}", filename.c_str());

    const RomFS::RomFSFile child_file =
        RomFS::GetFile(romfs_buffer.data(), {Common::UTF8ToUTF16(filename)});

    if (!child_file.Length()) {
        LOG_ERROR(Service_DLP, "DLP child is missing from archive");
        return false;
    }

    distribution_content.resize(child_file.Length());
    memcpy(distribution_content.data(), child_file.Data(), child_file.Length());

    FileSys::CIAContainer cia_container;
    if (cia_container.Load(distribution_content) != Loader::ResultStatus::Success) {
        LOG_ERROR(Service_DLP, "Could not load DLP child header");
        return false;
    }

    const auto& smdh = cia_container.GetSMDH();

    if (!smdh) {
        LOG_ERROR(Service_DLP, "Failed to load DLP child SMDH");
        return false;
    }

    // load up title_broadcast_info
    memset(&title_broadcast_info, 0, sizeof(title_broadcast_info));
    title_broadcast_info.title_id = dlp_child_tid;
    title_broadcast_info.title_size = child_file.Length();
    title_broadcast_info.transfer_size = cia_container.GetMetadataOffset();
    title_broadcast_info.required_size = child_file.Length(); // TODO: verify on HW

    memcpy(title_broadcast_info.icon.data(), smdh->large_icon.data(), smdh->large_icon.size());

    auto request_lang = SystemLanguageToSMDHLanguage(GetCFG()->GetSystemLanguage());
    LOG_DEBUG(Service_DLP, "Requesting lang: {}", static_cast<u32>(request_lang));

    auto t_long = smdh->GetLongTitle(request_lang);
    auto t_short = smdh->GetShortTitle(request_lang);
    memcpy(title_broadcast_info.title_short.data(), t_short.data(), t_short.size());
    memcpy(title_broadcast_info.title_long.data(), t_long.data(), t_long.size());

    LOG_INFO(Service_DLP, "Successfully cached DLP child content");

    return true;
}

void DLP_SRVR::IsChild(Kernel::HLERequestContext& ctx) {
    auto fs = GetFS();

    IPC::RequestParser rp(ctx);
    u32 process_id = rp.Pop<u32>();

    bool child;
    if (!fs) {
        LOG_CRITICAL(Service_DLP, "Could not get direct pointer fs:USER (sm returned null)");
    }
    auto title_info = fs->GetProgramLaunchInfo(process_id);

    if (title_info) {
        // check if tid corresponds to dlp filter
        u32 tid[2];
        memcpy(tid, &title_info->program_id, sizeof(tid));
        child = (tid[1] & 0xFFFFC000) == 0x40000 && (tid[1] & 0xFFFF) == 0x1;
        LOG_INFO(Service_DLP, "Checked on tid high: {:x} (low {:x}). Is child: {}", tid[1], tid[0],
                 child);
    } else { // child not found
        child = false;
        LOG_ERROR(Service_DLP,
                  "Could not determine program id from process id. (process id not found: {:x})",
                  process_id);
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);
    rb.Push(child);
}

void DLP_SRVR::GetDupNoticeNeed(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    // this is not a 3ds. we don't have
    // to update anything.
    bool need = false;

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);
    rb.Push(need);
}

void DLP_SRVR::GetServerState(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);
    rb.Push(GetSrvrState());
}

void DLP_SRVR::Initialize(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    auto shared_mem_size = rp.Pop<u32>();
    auto max_clients = rp.Pop<u8>();
    auto process_id = rp.Pop<u32>();
    rp.Skip(3, false); // unk

    auto [sharedmem, net_event] = rp.PopObjects<Kernel::SharedMemory, Kernel::Event>();

    InitializeSrvrCommon(shared_mem_size, max_clients, process_id,
                         String16AsDLPUsername(GetCFG()->GetUsername()), sharedmem, net_event);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void DLP_SRVR::InitializeWithName(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    auto shared_mem_size = rp.Pop<u32>();
    auto max_clients = rp.Pop<u8>();
    auto process_id = rp.Pop<u32>();
    rp.Skip(3, false); // unk
    auto username = rp.PopRaw<DLP_Username>();
    rp.Skip(1, false);

    auto [sharedmem, net_event] = rp.PopObjects<Kernel::SharedMemory, Kernel::Event>();

    InitializeSrvrCommon(shared_mem_size, max_clients, process_id, username, sharedmem, net_event);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void DLP_SRVR::Finalize(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    std::scoped_lock lock(broadcast_mutex);
    is_broadcasting = false;

    EndConnectionManager();

    distribution_content.clear();

    FinalizeDlpBase();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void DLP_SRVR::StartHosting(Kernel::HLERequestContext& ctx) {
    LOG_INFO(Service_DLP, "called");
    IPC::RequestParser rp(ctx);

    std::scoped_lock lock{srvr_state_mutex, broadcast_mutex};

    manual_accept = rp.Pop<bool>();
    u8 channel = rp.Pop<u8>();

    if (channel != 0 && channel != 1 && channel != 6 && channel != 11) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(Result(ErrorDescription::OutOfRange, ErrorModule::DLP,
                       ErrorSummary::InvalidArgument, ErrorLevel::Permanent));
        return;
    }

    NWM::NetworkInfo net_info;
    net_info.max_nodes = max_clients + 1;
    net_info.channel = channel; // if 0x0, gets set to default channel: 11
    net_info.wlan_comm_id = dlp_wlan_comm_id;
    net_info.application_data_size = 0;

    GetUDS()->BeginHostingNetwork({reinterpret_cast<u8*>(&net_info), sizeof(NWM::NetworkInfo)},
                                  dlp_password_buf);

    is_broadcasting = true;
    constexpr u32 title_broadcast_delay_ms = 1;

    system.CoreTiming().ScheduleEvent(title_broadcast_delay_ms, title_broadcast_event, 0);

    srvr_state = DLP_Srvr_State::Accepting;

    is_hosting = true;
    dlp_srvr_poll_rate_ms = dlp_poll_rate_normal;
    server_connection_worker = std::thread([this] { ServerConnectionManager(); });

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void DLP_SRVR::EndHosting(Kernel::HLERequestContext& ctx) {
    LOG_INFO(Service_DLP, "called");
    IPC::RequestParser rp(ctx);

    std::scoped_lock lock{srvr_state_mutex, broadcast_mutex};
    is_broadcasting = false;
    srvr_state = DLP_Srvr_State::Initialized;

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void DLP_SRVR::GetConnectingClients(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    u32 len = rp.Pop<u32>();
    auto mapped_buf = rp.PopMappedBuffer();

    std::vector<u16> buf_connecting;

    std::scoped_lock lock(client_states_mutex);

    for (auto& cl : client_states) {
        if (cl.state == ClientState::NotJoined) {
            continue;
        }
        buf_connecting.push_back(cl.network_node_id);
    }

    // ignore the host node
    mapped_buf.Write(buf_connecting.data(), 0,
                     std::min((u32)buf_connecting.size(), len) * sizeof(u16));

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 2);
    rb.Push(ResultSuccess);
    rb.Push<u32>(buf_connecting.size());
    rb.PushMappedBuffer(mapped_buf);
}

void DLP_SRVR::GetClientInfo(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    auto node_id = rp.Pop<u16>();

    auto node_info = GetUDS()->GetNodeInformationHLE(node_id);
    if (!node_info) {
        LOG_ERROR(Service_DLP, "Could not get node info for network node id 0x{:x}", node_id);
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(Result(ErrorDescription::NoData, ErrorModule::DLP, ErrorSummary::NotFound,
                       ErrorLevel::Status));
        return;
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(11, 0);
    if (node_id == dlp_host_network_node_id) {
        // even though it's not supposed to use
        // this info, we will still give it the
        // info
        rb.Push(Result(ErrorDescription::NoData, ErrorModule::DLP, ErrorSummary::NotFound,
                       ErrorLevel::Status));
    } else {
        rb.Push(ResultSuccess);
    }
    rb.PushRaw(UDSToDLPNodeInfo(*node_info));
}

void DLP_SRVR::GetClientState(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    u8 node_id = rp.Pop<u16>();

    // TODO: check if nodata is returned if the
    // node requested isn't joined yet

    std::scoped_lock lock(client_states_mutex);

    bool state_should_error = false;
    auto cl = GetClState(node_id, state_should_error);

    if (!cl) {
        LOG_WARNING(Service_DLP, "Node id out of range: 0x{:x}", node_id);
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(Result(ErrorDescription::NoData, ErrorModule::DLP, ErrorSummary::NotFound,
                       ErrorLevel::Status));
        return;
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(6, 0);
    rb.Push(ResultSuccess);
    rb.Push(cl->GetMyState());
    rb.Push<u32>(cl->dlp_units_total);
    rb.Push<u32>(cl->GetDlpUnitsDownloaded());
    // unk, see dlp_client_base state
    rb.Push<u32>(0x0);
    rb.Push<u32>(0x0);
}

void DLP_SRVR::StartDistribution(Kernel::HLERequestContext& ctx) {
    LOG_INFO(Service_DLP, "called");

    // when StartDistribution is called, the real
    // DLP_SRVR doesn't stop its broadcast thread
    // we do because we're better. we're stronger.
    std::scoped_lock lock{srvr_state_mutex, broadcast_mutex};

    is_broadcasting = false;
    is_distributing = true;
    is_waiting_for_passphrase = false;
    dlp_srvr_poll_rate_ms = dlp_poll_rate_distribute;

    srvr_state = DLP_Srvr_State::WaitingForDistribution;

    auto aes = GenDLPChecksumKey(host_mac_address);

    // now send distribute info requests
    for (auto& cl : client_states) {
        if (cl.state == ClientState::NotJoined)
            continue;
        if (cl.state != ClientState::Accepted) {
            LOG_ERROR(Service_DLP, "Client was not ready start distribution");
        }
        cl.state = ClientState::NeedsDistributeAck;
        auto s_body = PGen_SetPK<DLPSrvr_StartDistribution>(dl_pk_head_start_dist_header, 1,
                                                            cl.GetPkRespId());
        s_body->initialized = true;
        PGen_SendPK(aes, cl.network_node_id, dlp_client_data_channel);
    }

    IPC::RequestParser rp(ctx);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void DLP_SRVR::BeginGame(Kernel::HLERequestContext& ctx) {
    LOG_INFO(Service_DLP, "called");

    IPC::RequestParser rp(ctx);

    auto passphrase = rp.PopRaw<std::array<u8, 9>>();

    std::scoped_lock lock{client_states_mutex};

    auto aes = GenDLPChecksumKey(host_mac_address);

    for (auto& cl : client_states) {
        if (cl.state == ClientState::NotJoined) {
            continue;
        }
        if (cl.state == ClientState::SentPassphrase) {
            LOG_WARNING(Service_DLP, "Already sent passphrase");
            IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
            rb.Push(-1); // TODO: find real error
            return;
        }
        if (cl.state != ClientState::DistributeDone) {
            LOG_ERROR(Service_DLP, "Client is not ready to begin the game (state: {})",
                      static_cast<u8>(cl.state));
            IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
            rb.Push(-1); // TODO: find real error
            return;
        }
        auto s_body =
            PGen_SetPK<DLPSrvr_BeginGameFinal>(dl_pk_head_start_game_header, 1, cl.GetPkRespId());
        s_body->unk1 = 0x1;
        s_body->wireless_reboot_passphrase = passphrase;
        s_body->unk2 = 0x9; // could be our state
        PGen_SendPK(aes, cl.network_node_id, dlp_client_data_channel);
        cl.state = ClientState::SentPassphrase;
    }

    is_waiting_for_passphrase = true;

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void DLP_SRVR::AcceptClient(Kernel::HLERequestContext& ctx) {
    LOG_INFO(Service_DLP, "called");

    IPC::RequestParser rp(ctx);

    auto node_id = rp.Pop<u16>();

    std::scoped_lock lock(client_states_mutex);

    auto cl = GetClState(node_id);

    if (!cl) {
        LOG_ERROR(Service_DLP, "Could not find client node id: {}", node_id);
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push<u32>(-1); // TODO: find real error code
        return;
    }

    SendAuthPacket(*cl);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void DLP_SRVR::DisconnectClient(Kernel::HLERequestContext& ctx) {
    LOG_INFO(Service_DLP, "called");

    IPC::RequestParser rp(ctx);

    auto node_id = rp.Pop<u16>();

    std::scoped_lock lock(client_states_mutex);

    auto cl = GetClState(node_id);

    if (!cl) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push<u32>(-1); // TODO: find real error code
        return;
    }

    GetUDS()->EjectClientHLE(node_id);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void DLP_SRVR::TitleBroadcastCallback(std::uintptr_t user_data, s64 cycles_late) {
    std::scoped_lock lock(broadcast_mutex);

    if (!is_broadcasting) {
        return;
    }

    auto aes = GenDLPChecksumKey(host_mac_address);

    std::array<u8, 3> broadcast_resp_id = {0x05};

    auto p1 = PGen_SetPK<DLPBroadcastPacket1>(dl_pk_head_broadcast_header, 0, broadcast_resp_id);
    p1->child_title_id = title_broadcast_info.title_id;
    p1->transfer_size = title_broadcast_info.transfer_size;
    p1->required_size = title_broadcast_info.required_size;
    p1->content_block_size = content_fragment_size * dlp_content_block_length;
    p1->max_nodes = max_clients + 1;
    std::copy(title_broadcast_info.title_short.begin(), title_broadcast_info.title_short.end(),
              p1->title_short.begin());
    std::copy(title_broadcast_info.title_long.begin(), title_broadcast_info.title_long.end(),
              p1->title_long.begin());

    // copy in the icon data
    size_t cur_icon_start = 0, cur_icon_end = 0;

    auto copy_pk_icon = [&](std::span<u16_be>&& icon_part) {
        cur_icon_start = cur_icon_end;
        cur_icon_end += icon_part.size();
        std::copy(title_broadcast_info.icon.begin() + cur_icon_start,
                  title_broadcast_info.icon.begin() + cur_icon_end, icon_part.begin());
    };

    copy_pk_icon({p1->icon_part.begin(), p1->icon_part.end()});

    // i'm not sure what this is. is this regional?
    p1->unk1 = {0x0, 0x10};
    p1->unk6 = {0x1, 0x1};
    PGen_SendPK(aes, broadcast_node_id, dlp_broadcast_data_channel);
    auto p2 = PGen_SetPK<DLPBroadcastPacket2>(dl_pk_head_broadcast_header, 1, broadcast_resp_id);
    copy_pk_icon({p2->icon_part.begin(), p2->icon_part.end()});
    PGen_SendPK(aes, broadcast_node_id, dlp_broadcast_data_channel);
    auto p3 = PGen_SetPK<DLPBroadcastPacket3>(dl_pk_head_broadcast_header, 2, broadcast_resp_id);
    copy_pk_icon({p3->icon_part.begin(), p3->icon_part.end()});
    PGen_SendPK(aes, broadcast_node_id, dlp_broadcast_data_channel);
    auto p4 = PGen_SetPK<DLPBroadcastPacket4>(dl_pk_head_broadcast_header, 3, broadcast_resp_id);
    copy_pk_icon({p4->icon_part.begin(), p4->icon_part.end()});
    PGen_SendPK(aes, broadcast_node_id, dlp_broadcast_data_channel);
    [[maybe_unused]] auto p5 =
        PGen_SetPK<DLPBroadcastPacket5>(dl_pk_head_broadcast_header, 4, broadcast_resp_id);
    PGen_SendPK(aes, broadcast_node_id, dlp_broadcast_data_channel);

    system.CoreTiming().ScheduleEvent(msToCycles(title_broadcast_interval_ms) - cycles_late,
                                      title_broadcast_event, 0);
}

void DLP_SRVR::ServerConnectionManager() {
    auto uds = GetUDS();
    auto aes = GenDLPChecksumKey(host_mac_address);

    auto [ret, data_available_event] = uds->BindHLE(dlp_bind_node_id, dlp_recv_buffer_size,
                                                    dlp_client_data_channel, broadcast_node_id);
    if (ret != NWM::ResultStatus::ResultSuccess) {
        LOG_ERROR(Service_DLP, "Could not bind on node id 0x{:x}", dlp_bind_node_id);
        return;
    }

    auto sleep_poll = [](size_t poll_rate) -> void {
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_rate));
    };

    LOG_INFO(Service_DLP, "Started");

    while (sleep_poll(dlp_srvr_poll_rate_ms), is_hosting) {
        // these mutexes are preventing the poll_rate from being
        // 0 ms (since they are unfair mutexes)
        std::scoped_lock lock{client_states_mutex, srvr_state_mutex};

        auto conn_status = uds->GetConnectionStatusHLE();

        auto any_needs_distribute = [this]() -> bool {
            for (auto& cl : client_states) {
                if (cl.state == ClientState::NotJoined) {
                    continue;
                }
                if (cl.state == ClientState::NeedsContent ||
                    cl.state == ClientState::DoesNotNeedContent ||
                    cl.state == ClientState::NeedsDistributeAck) {
                    return true;
                }
                if (cl.state != ClientState::DistributeDone) {
                    LOG_WARNING(Service_DLP,
                                "Invalid client state, should be in content distribution phase");
                }
            }
            return false;
        };
        auto all_got_passphrase = [this]() -> bool {
            for (auto& cl : client_states) {
                if (cl.state == ClientState::NotJoined) {
                    continue;
                }
                if (cl.state != ClientState::SentPassphrase) {
                    return false;
                }
            }
            return true;
        };

        // send outgoing messages
        for (auto& cl : client_states) {
            // client disconnected
            if (cl.state != ClientState::NotJoined && !conn_status.nodes[cl.network_node_id - 1]) {
                cl.state = ClientState::NotJoined;
                LOG_INFO(Service_DLP, "Client disconnected");
            }
            switch (cl.state) {
            case ClientState::NotJoined: {
                // check if client should be joined
                if (!conn_status.nodes[cl.network_node_id - 1]) {
                    break;
                }
                cl.state = ClientState::NeedsAuth;
                cl.pk_seq_num = 0;
                LOG_INFO(Service_DLP, "Client connected");
            }
            // fall through
            case ClientState::NeedsAuth: {
                if (manual_accept) {
                    break;
                }
            }
            // fall through
            case ClientState::NeedsAuthAck: {
                // coded it this way so that it
                // matches the hw behavior of constantly
                // sending accept packets
                SendAuthPacket(cl);
                break;
            }
            case ClientState::Accepted:
                break; // can idle
            case ClientState::NeedsDistributeAck:
                break;
            case ClientState::NeedsContent: {
                if (!cl.can_download_next_block) {
                    if (cl.sent_next_block_req) {
                        continue;
                    }
                    // send content block ack request (finish dist)
                    auto s_body = PGen_SetPK<DLPSrvr_FinishContentUpload>(
                        dl_pk_head_finish_dist_header, 1, cl.GetPkRespId());
                    s_body->initialized = true;
                    s_body->seq_num = cl.next_req_ack;
                    PGen_SendPK(aes, cl.network_node_id, dlp_client_data_channel);
                    cl.sent_next_block_req = true;
                    continue;
                }
                // TODO: change to broadcast content instead of
                // sending it to individual clients. I'm doing it
                // like this for simplicity's sake, but a real 3ds
                // needs to broadcast the packets for efficiency.
                for (size_t i = 0; i < dlp_content_block_length; i++) {
                    if (cl.GetTotalFragIndex(i) == cl.dlp_units_total) {
                        LOG_DEBUG(Service_DLP, "Cut block short");
                        break;
                    }
                    SendNextCIAFragment(cl, i);
                }
                cl.can_download_next_block = false;
                cl.sent_next_block_req = false;
                cl.current_content_block++;
                break;
            }
            case ClientState::DoesNotNeedContent: {
                if (cl.can_download_next_block) {
                    LOG_ERROR(Service_DLP, "Got unexpected content block request when client did "
                                           "not request content previously");
                    continue;
                }
                // since we have a global poll rate,
                // we need to rate limit packet sending
                // to individual clients
                if (cl.ShouldRateLimit(dlp_poll_rate_normal)) {
                    continue;
                }
                // send finish dist req
                auto s_body = PGen_SetPK<DLPSrvr_FinishContentUpload>(dl_pk_head_finish_dist_header,
                                                                      1, cl.GetPkRespId());
                s_body->initialized = true;
                s_body->seq_num = 0;
                PGen_SendPK(aes, cl.network_node_id, dlp_client_data_channel);
                break;
            }
            case ClientState::DistributeDone:
            case ClientState::SentPassphrase: {
                if (cl.ShouldRateLimit(dlp_poll_rate_normal)) {
                    continue;
                }
                auto s_body = PGen_SetPK<DLPSrvr_BeginGame>(dl_pk_head_start_game_header, 0,
                                                            cl.GetPkRespId());
                s_body->unk1 = 0x1;
                s_body->unk2 = 0x9;
                PGen_SendPK(aes, cl.network_node_id, dlp_client_data_channel);
                break;
            }
            default:
                LOG_ERROR(Service_DLP, "Unknown client state: {}", static_cast<u8>(cl.state));
            }
        }

        // receive incoming response messages
        std::vector<u8> recv_buf;
        u16 recv_node = 0;
        while (int sz = RecvFrom(dlp_bind_node_id, recv_buf, &recv_node)) {
            auto p_head = GetPacketHead(recv_buf);
            auto cl = GetClState(recv_node);

            if (!cl) {
                LOG_ERROR(Service_DLP, "Could not get client state from received packet's node id");
                continue;
            }

            // validate packet header
            if (!ValidatePacket(aes, p_head, sz, should_verify_checksum)) {
                LOG_ERROR(Service_DLP, "Could not validate DLP packet header");
                continue;
            }

            if (p_head->type == dl_pk_type_auth) {
                LOG_DEBUG(Service_DLP, "Recv auth");
                if (cl->state != ClientState::NeedsAuthAck)
                    continue;
                auto r_pbody = GetPacketBody<DLPClt_AuthAck>(recv_buf);
                cl->state = ClientState::Accepted;
                cl->resp_id = r_pbody->resp_id;
                cl->IncrSeqNum();
            } else if (p_head->type == dl_pk_type_start_dist) {
                LOG_DEBUG(Service_DLP, "Recv start dist");
                if (cl->state != ClientState::NeedsDistributeAck)
                    continue;
                // packet needs to be at least the size of
                // no_content_needed
                auto r_pbody_nc =
                    GetPacketBody<DLPClt_StartDistributionAck_NoContentNeeded>(recv_buf);
                if (r_pbody_nc->unk2 == 0x20) { // check if we should upgrade to content_needed
                    [[maybe_unused]] auto r_pbody_c =
                        GetPacketBody<DLPClt_StartDistributionAck_ContentNeeded>(recv_buf);
                    cl->state = ClientState::NeedsContent;
                    srvr_state = DLP_Srvr_State::Distributing;
                    cl->current_content_block = 0;
                    cl->sent_next_block_req = false;
                    cl->can_download_next_block = true;
                    cl->dlp_units_total =
                        GetNumFragmentsFromTitleSize(title_broadcast_info.title_size);
                    cl->next_req_ack = 0;
                } else { // keep no_content_needed
                    if (!r_pbody_nc->initialized) {
                        LOG_WARNING(Service_DLP, "Corrupted packet info");
                    }
                    cl->sent_next_block_req = false;
                    cl->can_download_next_block = false;
                    // the reason we have this enum value is because
                    // we still need it to confirm that it finished
                    // "downloading" content
                    cl->state = ClientState::DoesNotNeedContent;
                }
                cl->IncrSeqNum();
            } else if (p_head->type == dl_pk_type_distribute) {
                LOG_ERROR(Service_DLP, "Unexpected content distribution fragment");
            } else if (p_head->type == dl_pk_type_finish_dist) {
                LOG_DEBUG(Service_DLP, "Recv finish dist");
                if (cl->state != ClientState::NeedsContent &&
                    cl->state != ClientState::DoesNotNeedContent) {
                    continue;
                }
                if (cl->can_download_next_block) {
                    // got finish dist when we didn't need it
                    LOG_WARNING(
                        Service_DLP,
                        "Got finish dist when client was already queued to download next block");
                    continue;
                }
                auto r_pbody = GetPacketBody<DLPClt_FinishContentUploadAck>(recv_buf);
                if (!r_pbody->needs_content) {
                    LOG_INFO(Service_DLP, "Client has finished downloading content");
                    cl->state = ClientState::DistributeDone;
                    cl->sent_next_block_req = false;
                } else {
                    if (cl->current_content_block != r_pbody->seq_ack) {
                        if (r_pbody->finished_cur_block) {
                            LOG_WARNING(Service_DLP,
                                        "Received out of order block request. Ignoring. ({} != {})",
                                        static_cast<u32>(r_pbody->seq_ack),
                                        cl->current_content_block);
                        }
                        cl->sent_next_block_req = false;
                        cl->IncrSeqNum(); // client expects us to increment here
                        continue;
                    }
                }
                cl->can_download_next_block = r_pbody->needs_content;
                cl->next_req_ack++;
                cl->IncrSeqNum();
            } else if (p_head->type == dl_pk_type_start_game) {
                LOG_DEBUG(Service_DLP, "Recv start game");
                if (cl->state != ClientState::DistributeDone &&
                    cl->state != ClientState::SentPassphrase) {
                    continue;
                }
                auto r_pbody = GetPacketBody<DLPClt_BeginGameAck>(recv_buf);
                if (r_pbody->unk2 != 0x9) {
                    LOG_WARNING(Service_DLP, "Client BeginGameAck unk2 is not 0x9 yet");
                    continue;
                }
                cl->IncrSeqNum();
            } else {
                LOG_ERROR(Service_DLP, "Unknown packet type: 0x{:x}", p_head->type);
            }
        }

        if (is_distributing && !any_needs_distribute()) {
            is_distributing = false;
            srvr_state = DLP_Srvr_State::NeedToSendPassphrase; // causes begingame to run
            is_waiting_for_passphrase = true;
            dlp_srvr_poll_rate_ms = dlp_poll_rate_normal;
        } else if (is_waiting_for_passphrase && all_got_passphrase()) {
            is_waiting_for_passphrase = false;
            srvr_state = DLP_Srvr_State::Complete;
            // we have to stop the server client manager thread
            // right now to avoid a race condition where we're waiting
            // for one client to leave, but another client joins our server again
            // thinking it's a raw UDS server, so it shows up as a connecting client.
            // and if we just ignored it, it wouldn't try to reconnect to our real server,
            // meaning that not doing this would case an unrecoverable error.
            // a real 3DS gets around this by using different RF channels and
            // the built-in DLP server password, which will prevent the client from
            // joining our server.
            is_hosting = false;

            // force set all clients to disconnected state
            for (auto& cl : client_states) {
                cl.state = ClientState::NotJoined;
            }
        }
    }

    uds->UnbindHLE(dlp_bind_node_id);
    uds->DestroyNetworkHLE();

    LOG_INFO(Service_DLP, "Ended");
}

void DLP_SRVR::SendAuthPacket(ClientState& cl) {
    std::scoped_lock lock(client_states_mutex);
    auto aes = GenDLPChecksumKey(host_mac_address);
    auto s_body = PGen_SetPK<DLPSrvr_Auth>(dl_pk_head_auth_header, 1, default_resp_id);
    s_body->unk1 = 0x0;
    PGen_SendPK(aes, cl.network_node_id, dlp_client_data_channel);
    if (cl.state == ClientState::NeedsAuth) {
        cl.state = ClientState::NeedsAuthAck;
    }
}

bool DLP_SRVR::SendNextCIAFragment(ClientState& cl, u8 block_frag_index) {
    std::scoped_lock lock(client_states_mutex);
    size_t cur_frag_index = cl.GetTotalFragIndex(block_frag_index);
    size_t frag_offset = content_fragment_size * cur_frag_index;
    if (frag_offset > distribution_content.size()) {
        LOG_ERROR(Service_DLP, "Frag offset is larger than the CIA size");
        return false;
    }
    u16 frag_size =
        std::min<size_t>(content_fragment_size, distribution_content.size() - frag_offset);
    if (frag_offset + frag_size > distribution_content.size()) {
        LOG_ERROR(Service_DLP, "Frag size is too large to properly fit the CIA content");
        return false;
    }
    auto aes = GenDLPChecksumKey(host_mac_address);
    std::span<u8> send_frag{distribution_content.begin() + frag_offset, frag_size};
    // uses default resp id because it's supposed to be broadcast
    auto s_body = PGen_SetPK<DLPSrvr_ContentDistributionFragment>(dl_pk_head_distribute_header, 1,
                                                                  default_resp_id);
    PGen_AddPacketData(s_body, frag_size);

    // only checks if the low byte is 0x1
    s_body->unk2 = 0x1;
    s_body->padding = {0x8A, 0x4A, 0xE3};
    s_body->content_block = cl.current_content_block;
    s_body->frag_index = block_frag_index;
    s_body->frag_size = frag_size;
    memcpy(s_body->content_fragment, send_frag.data(), frag_size);

    PGen_SendPK(aes, cl.network_node_id, dlp_client_data_channel);
    return true;
}

void DLP_SRVR::EndConnectionManager() {
    is_hosting = false;
    if (server_connection_worker.joinable()) {
        server_connection_worker.join();
    }
}

// make sure to lock the client states mutex before
// calling this
DLP_SRVR::ClientState* DLP_SRVR::GetClState(u8 node_id, bool should_error) {
    u8 cl_state_index = node_id - first_client_node_id;
    if (cl_state_index >= client_states.size() && should_error) {
        LOG_CRITICAL(Service_DLP, "Out of range node id {}", node_id);
        return nullptr;
    }
    return &client_states[cl_state_index];
}

// can cause race conditions, only use this
// in places where the core loop isn't running
void DLP_SRVR::EnsureEndBroadcaster() {
    std::scoped_lock lock(broadcast_mutex);
    is_broadcasting = false;
    system.CoreTiming().UnscheduleEvent(title_broadcast_event, 0);
}

DLP_SRVR::DLP_SRVR() : ServiceFramework("dlp:SRVR", 1), DLP_Base(Core::System::GetInstance()) {
    static const FunctionInfo functions[] = {
        // clang-format off
        {0x0001, &DLP_SRVR::Initialize, "Initialize"},
        {0x0002, &DLP_SRVR::Finalize, "Finalize"},
        {0x0003, &DLP_SRVR::GetServerState, "GetServerState"},
        {0x0004, &DLP_SRVR::GetEventDescription, "GetEventDescription"},
        {0x0005, &DLP_SRVR::StartHosting, "StartHosting"},
        {0x0006, &DLP_SRVR::EndHosting, "EndHosting"},
        {0x0007, &DLP_SRVR::StartDistribution, "StartDistribution"},
        {0x0008, &DLP_SRVR::BeginGame, "BeginGame"},
        {0x0009, &DLP_SRVR::AcceptClient, "AcceptClient"},
        {0x000A, &DLP_SRVR::DisconnectClient, "DisconnectClient"},
        {0x000B, &DLP_SRVR::GetConnectingClients, "GetConnectingClients"},
        {0x000C, &DLP_SRVR::GetClientInfo, "GetClientInfo"},
        {0x000D, &DLP_SRVR::GetClientState, "GetClientState"},
        {0x000E, &DLP_SRVR::IsChild, "IsChild"},
        {0x000F, &DLP_SRVR::InitializeWithName, "InitializeWithName"},
        {0x0010, &DLP_SRVR::GetDupNoticeNeed, "GetDupNoticeNeed"},
        // clang-format on
    };

    RegisterHandlers(functions);

    title_broadcast_event = system.CoreTiming().RegisterEvent(
        "dlp:SRVR::TitleBroadcastCallback", [this](std::uintptr_t user_data, s64 cycles_late) {
            TitleBroadcastCallback(user_data, cycles_late);
        });
}

DLP_SRVR::~DLP_SRVR() {
    EnsureEndBroadcaster();
    EndConnectionManager();
}

} // namespace Service::DLP
