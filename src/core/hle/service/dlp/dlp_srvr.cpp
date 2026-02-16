// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/archives.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/result.h"
#include "core/hle/service/dlp/dlp_srvr.h"
#include "core/hle/service/fs/fs_user.h"

SERIALIZE_EXPORT_IMPL(Service::DLP::DLP_SRVR)

namespace Service::DLP {

std::shared_ptr<Kernel::SessionRequestHandler> DLP_SRVR::GetServiceFrameworkSharedPtr() {
    return shared_from_this();
}

void DLP_SRVR::IsChild(Kernel::HLERequestContext& ctx) {
    auto fs = system.ServiceManager().GetService<Service::FS::FS_USER>("fs:USER");

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
        LOG_INFO(Service_DLP, "Checked on tid high: {:x} (low {:x})", tid[1], tid[0]);
        child = (tid[1] & 0xFFFFC000) == 0x40000 && (tid[1] & 0xFFFF) == 0x1;
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

DLP_SRVR::DLP_SRVR() : ServiceFramework("dlp:SRVR", 1), DLP_Base(Core::System::GetInstance()) {
    static const FunctionInfo functions[] = {
        // clang-format off
        {0x0001, nullptr, "Initialize"},
        {0x0002, nullptr, "Finalize"},
        {0x0003, nullptr, "GetServerState"},
        {0x0004, nullptr, "GetEventDescription"},
        {0x0005, nullptr, "StartAccepting"},
        {0x0006, nullptr, "EndAccepting"},
        {0x0007, nullptr, "StartDistribution"},
        {0x0008, nullptr, "SendWirelessRebootPassphrase"},
        {0x0009, nullptr, "AcceptClient"},
        {0x000A, nullptr, "DisconnectClient"},
        {0x000B, nullptr, "GetConnectingClients"},
        {0x000C, nullptr, "GetClientInfo"},
        {0x000D, nullptr, "GetClientState"},
        {0x000E, &DLP_SRVR::IsChild, "IsChild"},
        {0x000F, nullptr, "InitializeWithName"},
        {0x0010, nullptr, "GetDupNoticeNeed"},
        // clang-format on
    };

    RegisterHandlers(functions);
}

} // namespace Service::DLP
