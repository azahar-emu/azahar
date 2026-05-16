// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/hle/kernel/resource_limit.h"
#include "core/hle/service/service.h"

namespace Service::PM {

class PM_APP final : public ServiceFramework<PM_APP> {
public:
    explicit PM_APP(Core::System& system);
    ~PM_APP() = default;

    Result UpdateResourceLimit(Kernel::ResourceLimitType type, u32 value);

    ResultVal<u32> GetResourceLimit(Kernel::ResourceLimitType type);

private:
    Core::System& system;

    void SetAppResourceLimit(Kernel::HLERequestContext& ctx);

    void GetAppResourceLimit(Kernel::HLERequestContext& ctx);

    SERVICE_SERIALIZATION_SIMPLE
};

std::shared_ptr<PM_APP> GetServiceAPP(Core::System& system);

} // namespace Service::PM

SERVICE_CONSTRUCT(Service::PM::PM_APP)
BOOST_CLASS_EXPORT_KEY(Service::PM::PM_APP)
