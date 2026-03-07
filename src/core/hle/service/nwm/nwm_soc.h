// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/hle/service/service.h"

namespace Service::NWM {

class NWM_SOC final : public ServiceFramework<NWM_SOC> {
public:
    explicit NWM_SOC(Core::System& system);

private:
    void GetMACAddress(Kernel::HLERequestContext& ctx);

    Core::System& system;

    SERVICE_SERIALIZATION_SIMPLE
};

} // namespace Service::NWM

SERVICE_CONSTRUCT(Service::NWM::NWM_SOC)
BOOST_CLASS_EXPORT_KEY(Service::NWM::NWM_SOC)
