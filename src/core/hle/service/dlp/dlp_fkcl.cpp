// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/archives.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/service/dlp/dlp_fkcl.h"
#include "core/core.h" // class System

#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include "common/archives.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/service/ps/ps_ps.h"
#include "core/hle/service/ssl/ssl_c.h"
#include "core/hw/aes/arithmetic128.h"
#include "core/hw/aes/key.h"

SERIALIZE_EXPORT_IMPL(Service::DLP::DLP_FKCL)

namespace Service::DLP {

std::shared_ptr<Kernel::SessionRequestHandler> DLP_FKCL::GetServiceFrameworkSharedPtr() {
    return shared_from_this();
}

void DLP_FKCL::Initialize(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    
    u32 shared_mem_size = rp.Pop<u32>();
	u32 max_beacons = rp.Pop<u32>();
    constexpr u32 constant_mem_size = 0;
	auto [shared_mem, event] = rp.PopObjects<Kernel::SharedMemory, Kernel::Event>();
    
    InitializeCltBase(shared_mem_size, max_beacons, constant_mem_size, shared_mem, event, String16AsDLPUsername(GetCFG()->GetUsername()));
    
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void DLP_FKCL::InitializeWithName(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    
    u32 shared_mem_size = rp.Pop<u32>();
	u32 max_beacons = rp.Pop<u32>();
    constexpr u32 constant_mem_size = 0;
    auto username = rp.PopRaw<std::array<u16_le, 10>>();
    rp.Skip(1, false); // possible null terminator or unk flags
	auto [shared_mem, event] = rp.PopObjects<Kernel::SharedMemory, Kernel::Event>();
    
    InitializeCltBase(shared_mem_size, max_beacons, constant_mem_size, shared_mem, event, username);
    
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void DLP_FKCL::Finalize(Kernel::HLERequestContext& ctx) {
	IPC::RequestParser rp(ctx);
    
    FinalizeCltBase();
    
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

DLP_FKCL::DLP_FKCL() : ServiceFramework("dlp:FKCL", 1), DLP_Clt_Base(Core::System::GetInstance(), "FKCL") {
    static const FunctionInfo functions[] = {
        // clang-format off
        {0x0001, &DLP_FKCL::Initialize, "Initialize"},
        {0x0002, &DLP_FKCL::Finalize, "Finalize"},
        {0x0003, &DLP_FKCL::GetEventDescription, "GetEventDescription"},
        {0x0004, &DLP_FKCL::GetChannels, "GetChannels"},
        {0x0005, &DLP_FKCL::StartScan, "StartScan"},
        {0x0006, &DLP_FKCL::StopScan, "StopScan"},
        {0x0007, &DLP_FKCL::GetServerInfo, "GetServerInfo"},
        {0x0008, &DLP_FKCL::GetTitleInfo, "GetTitleInfo"},
        {0x0009, &DLP_FKCL::GetTitleInfoInOrder, "GetTitleInfoInOrder"},
        {0x000A, &DLP_FKCL::DeleteScanInfo, "DeleteScanInfo"},
        {0x000B, &DLP_FKCL::StartSession, "StartFakeSession"},
        {0x000C, &DLP_FKCL::GetMyStatus, "GetMyStatus"},
        {0x000D, &DLP_FKCL::GetConnectingNodes, "GetConnectingNodes"},
        {0x000E, &DLP_FKCL::GetNodeInfo, "GetNodeInfo"},
        {0x000F, &DLP_FKCL::GetWirelessRebootPassphrase, "GetWirelessRebootPassphrase"},
        {0x0010, &DLP_FKCL::StopSession, "StopSession"},
        {0x0011, &DLP_FKCL::InitializeWithName, "InitializeWithName"},
        // clang-format on
    };

    RegisterHandlers(functions);
}

} // namespace Service::DLP
