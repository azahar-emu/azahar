// Copyright 2018-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/hle/service/service.h"

namespace Service::PM {

class PM_DBG final : public ServiceFramework<PM_DBG> {
public:
    explicit PM_DBG(Core::System& system);
    ~PM_DBG() = default;

private:
    Core::System& system;

    void DebugNextApplicationByForce(Kernel::HLERequestContext& ctx);

    SERVICE_SERIALIZATION_SIMPLE
};

} // namespace Service::PM

SERVICE_CONSTRUCT(Service::PM::PM_DBG)
BOOST_CLASS_EXPORT_KEY(Service::PM::PM_DBG)
