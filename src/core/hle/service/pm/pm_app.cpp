// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/archives.h"
#include "core/core.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/service/pm/pm_app.h"

SERVICE_CONSTRUCT_IMPL(Service::PM::PM_APP)
SERIALIZE_EXPORT_IMPL(Service::PM::PM_APP)

namespace Service::PM {

PM_APP::PM_APP(Core::System& _system) : ServiceFramework("pm:app", 3), system(_system) {
    static const FunctionInfo functions[] = {
        // clang-format off
        {0x0001, nullptr, "LaunchTitle"},
        {0x0002, nullptr, "LaunchFIRM"},
        {0x0003, nullptr, "TerminateApplication"},
        {0x0004, nullptr, "TerminateTitle"},
        {0x0005, nullptr, "TerminateProcess"},
        {0x0006, nullptr, "PrepareForReboot"},
        {0x0007, nullptr, "GetFIRMLaunchParams"},
        {0x0008, nullptr, "GetTitleExheaderFlags"},
        {0x0009, nullptr, "SetFIRMLaunchParams"},
        {0x000A, &PM_APP::SetAppResourceLimit, "SetAppResourceLimit"},
        {0x000B, &PM_APP::GetAppResourceLimit, "GetAppResourceLimit"},
        {0x000C, nullptr, "UnregisterProcess"},
        {0x000D, nullptr, "LaunchTitleUpdate"},
        // clang-format on
    };

    RegisterHandlers(functions);
}

Result PM_APP::UpdateResourceLimit(Kernel::ResourceLimitType type, u32 value) {
    auto res_limit =
        system.Kernel().ResourceLimit().GetForCategory(Kernel::ResourceLimitCategory::Application);

    if (type != Kernel::ResourceLimitType::CpuTime) {
        return Result{ErrorDescription::NotImplemented, ErrorModule::PM,
                      ErrorSummary::InvalidArgument, ErrorLevel::Permanent};
    }

    if (value <= res_limit->GetLimitValue(Kernel::ResourceLimitType::CpuTime)) {
        res_limit->SetCurrentValue(Kernel::ResourceLimitType::CpuTime, value);
        system.Kernel().UpdateCore1AppCpuLimit();
    }

    return ResultSuccess;
}

ResultVal<u32> PM_APP::GetResourceLimit(Kernel::ResourceLimitType type) {
    auto res_limit =
        system.Kernel().ResourceLimit().GetForCategory(Kernel::ResourceLimitCategory::Application);

    if (type != Kernel::ResourceLimitType::CpuTime) {
        return Result{ErrorDescription::NotImplemented, ErrorModule::PM,
                      ErrorSummary::InvalidArgument, ErrorLevel::Permanent};
    }

    return static_cast<u32>(res_limit->GetCurrentValue(Kernel::ResourceLimitType::CpuTime));
}

void PM_APP::SetAppResourceLimit(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    rp.Skip(1, false);
    auto type = static_cast<Kernel::ResourceLimitType>(rp.Pop<u32>());
    u32 value = rp.Pop<s32>();
    rp.Skip(2, false);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(UpdateResourceLimit(type, value));
}

void PM_APP::GetAppResourceLimit(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    rp.Skip(1, false);
    auto type = static_cast<Kernel::ResourceLimitType>(rp.Pop<u32>());
    rp.Skip(3, false);

    u64 res_value = 0;
    auto res = GetResourceLimit(type);
    if (res.Succeeded()) {
        res_value = static_cast<u64>(res.Unwrap());
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(3, 0);
    rb.Push(res.Code());
    rb.Push(res_value);
}

std::shared_ptr<PM_APP> GetServiceAPP(Core::System& system) {
    return system.ServiceManager().GetService<PM_APP>("pm:app");
}

} // namespace Service::PM
