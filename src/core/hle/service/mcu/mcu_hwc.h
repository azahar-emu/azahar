// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/hle/service/service.h"

namespace Service::MCU {

class HWC final : public ServiceFramework<HWC> {
public:
    explicit HWC(Core::System& _system);

private:
    Core::System& system;

    void SetInfoLEDPattern(Kernel::HLERequestContext& ctx);

    SERVICE_SERIALIZATION_SIMPLE
};

} // namespace Service::MCU

SERVICE_CONSTRUCT(Service::MCU::HWC)
BOOST_CLASS_EXPORT_KEY(Service::MCU::HWC)
