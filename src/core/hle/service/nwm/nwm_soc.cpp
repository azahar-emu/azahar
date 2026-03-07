// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/archives.h"
#include "core/core.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/service/cfg/cfg.h"
#include "core/hle/service/cfg/cfg_u.h"
#include "core/hle/service/nwm/nwm_soc.h"
#include "core/hle/service/nwm/nwm_uds.h"

SERIALIZE_EXPORT_IMPL(Service::NWM::NWM_SOC)
SERVICE_CONSTRUCT_IMPL(Service::NWM::NWM_SOC)

namespace Service::NWM {

void NWM_SOC::GetMACAddress(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u32 size = rp.Pop<u32>();

    std::vector<u8> mac_buffer(size);
    MacAddress mac;

    if (auto cfg = system.ServiceManager().GetService<Service::CFG::CFG_U>("cfg:u")) {
        auto cfg_module = cfg->GetModule();
        mac = Service::CFG::MacToArray(cfg_module->GetMacAddress());
    }

    std::copy(mac.begin(), mac.begin() + std::min(mac.size(), mac_buffer.size()),
              mac_buffer.begin());

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(ResultSuccess);
    rb.PushStaticBuffer(mac_buffer, 0);
}

NWM_SOC::NWM_SOC(Core::System& _system) : ServiceFramework("nwm::SOC"), system(_system) {

    static const FunctionInfo functions[] = {
        {0x0008, &NWM_SOC::GetMACAddress, "GetMACAddress"},
    };
    RegisterHandlers(functions);
}

} // namespace Service::NWM
