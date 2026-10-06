// Copyright 2018-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#include "common/archives.h"
#include "core/core.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/service/pm/pm_dbg.h"

SERVICE_CONSTRUCT_IMPL(Service::PM::PM_DBG)
SERIALIZE_EXPORT_IMPL(Service::PM::PM_DBG)

namespace Service::PM {

PM_DBG::PM_DBG(Core::System& _system) : ServiceFramework("pm:dbg", 3), system(_system) {
    static const FunctionInfo functions[] = {
        // clang-format off
        {0x0001, nullptr, "LaunchAppDebug"},
        {0x0002, nullptr, "LaunchApp"},
        {0x0003, nullptr, "RunQueuedProcess"},
        // Custom
        {0x0101, &PM_DBG::DebugNextApplicationByForce, "DebugNextApplicationByForce"},
        // clang-format on
    };

    RegisterHandlers(functions);
}

void PM_DBG::DebugNextApplicationByForce(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    bool enable = rp.Pop<bool>();

    if (enable) {
        system.SetDebugNextProcessFlag();
    } else {
        system.ClearDebugNextProcessFlag();
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

} // namespace Service::PM
