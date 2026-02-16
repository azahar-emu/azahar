// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/archives.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/service/dlp/dlp_clnt.h"

SERIALIZE_EXPORT_IMPL(Service::DLP::DLP_CLNT)

namespace Service::DLP {

std::shared_ptr<Kernel::SessionRequestHandler> DLP_CLNT::GetServiceFrameworkSharedPtr() {
    return shared_from_this();
}

u32 DLP_CLNT::ClientNeedsDup() {
    [[maybe_unused]] constexpr u32 res_needs_system_update = 0x1;
    constexpr u32 res_does_not_need_update = 0x0;
    return res_does_not_need_update;
}

void DLP_CLNT::Initialize(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    u32 shared_mem_size = rp.Pop<u32>();
    u32 max_beacons = rp.Pop<u32>();
    u32 constant_mem_size = rp.Pop<u32>();
    auto [shared_mem, event] = rp.PopObjects<Kernel::SharedMemory, Kernel::Event>();

    InitializeCltBase(shared_mem_size, max_beacons, constant_mem_size, shared_mem, event,
                      String16AsDLPUsername(GetCFG()->GetUsername()));

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void DLP_CLNT::Finalize(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    FinalizeCltBase();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

// returns the version of the currently joined server
void DLP_CLNT::GetCupVersion(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    [[maybe_unused]] auto mac_addr = rp.PopRaw<Network::MacAddress>();
    [[maybe_unused]] u32 tid_low = rp.PopRaw<u32>();
    [[maybe_unused]] u32 tid_high = rp.PopRaw<u32>();

    LOG_WARNING(Service_DLP, "(STUBBED) called");

    IPC::RequestBuilder rb = rp.MakeBuilder(3, 0);

    // TODO: someone decipher this version code
    u64 version_num = 0x0;

    rb.Push(ResultSuccess);
    rb.Push(version_num);
}

// tells us which server to connect to and download an update from
// the dlp app uses this to check whether or not we need the update data
void DLP_CLNT::PrepareForSystemDownload(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    mac_addr_update = rp.PopRaw<Network::MacAddress>();
    [[maybe_unused]] u32 tid_low = rp.PopRaw<u32>();
    [[maybe_unused]] u32 tid_high = rp.PopRaw<u32>();

    if (ClientNeedsDup()) {
        is_preparing_for_update = true;
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);

    rb.Push(ResultSuccess);
    rb.Push(ClientNeedsDup());
}

// runs after the user accepts the license agreement to
// download the update
void DLP_CLNT::StartSystemDownload(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    LOG_WARNING(Service_DLP, "(STUBBED) called");

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);

    if (!is_preparing_for_update) {
        // error
        LOG_ERROR(Service_DLP, "Called without preparing first. We don't have a mac address!");
        // TODO: verify this on hw
        rb.Push(0xD960AC02);
        return;
    }

    is_preparing_for_update = false;
    is_updating = true;

    // TODO: figure out what comes after when
    // hw starts downloading update data via dlp.
    // it could set some missing client states
    // in GetCltState

    rb.Push(ResultSuccess);
}

// i'm assuming this is a secondary check whether or not we
// can download the update data?
void DLP_CLNT::GetDupAvailability(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    mac_addr_update = rp.PopRaw<Network::MacAddress>();
    [[maybe_unused]] u32 tid_low = rp.PopRaw<u32>();
    [[maybe_unused]] u32 tid_high = rp.PopRaw<u32>();

    LOG_WARNING(Service_DLP, "(STUBBED) called");

    [[maybe_unused]] constexpr u32 dup_is_available = 0x1;
    constexpr u32 dup_is_not_available = 0x0;

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);

    rb.Push(ResultSuccess);
    rb.Push(dup_is_not_available);
}

DLP_CLNT::DLP_CLNT()
    : ServiceFramework("dlp:CLNT", 1), DLP_Clt_Base(Core::System::GetInstance(), "CLNT") {
    static const FunctionInfo functions[] = {
        // clang-format off
        {0x0001, &DLP_CLNT::Initialize, "Initialize"},
        {0x0002, &DLP_CLNT::Finalize, "Finalize"},
        {0x0003, &DLP_CLNT::GetEventDescription, "GetEventDescription"},
        {0x0004, &DLP_CLNT::GetChannels, "GetChannel"},
        {0x0005, &DLP_CLNT::StartScan, "StartScan"},
        {0x0006, &DLP_CLNT::StopScan, "StopScan"},
        {0x0007, &DLP_CLNT::GetServerInfo, "GetServerInfo"},
        {0x0008, &DLP_CLNT::GetTitleInfo, "GetTitleInfo"},
        {0x0009, &DLP_CLNT::GetTitleInfoInOrder, "GetTitleInfoInOrder"},
        {0x000A, &DLP_CLNT::DeleteScanInfo, "DeleteScanInfo"},
        {0x000B, &DLP_CLNT::PrepareForSystemDownload, "PrepareForSystemDownload"},
        {0x000C, &DLP_CLNT::StartSystemDownload, "StartSystemDownload"},
        {0x000D, &DLP_CLNT::StartSession, "StartTitleDownload"},
        {0x000E, &DLP_CLNT::GetMyStatus, "GetMyStatus"},
        {0x000F, &DLP_CLNT::GetConnectingNodes, "GetConnectingNodes"},
        {0x0010, &DLP_CLNT::GetNodeInfo, "GetNodeInfo"},
        {0x0011, &DLP_CLNT::GetWirelessRebootPassphrase, "GetWirelessRebootPassphrase"},
        {0x0012, &DLP_CLNT::StopSession, "StopSession"},
        {0x0013, &DLP_CLNT::GetCupVersion, "GetCupVersion"},
        {0x0014, &DLP_CLNT::GetDupAvailability, "GetDupAvailability"},
        // clang-format on
    };

    RegisterHandlers(functions);
}

} // namespace Service::DLP
