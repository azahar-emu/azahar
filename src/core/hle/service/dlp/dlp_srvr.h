// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/hle/service/service.h"
#include "dlp_base.h"

namespace Service::DLP {

class DLP_SRVR final : public ServiceFramework<DLP_SRVR>, public DLP_Base {
public:
    DLP_SRVR();
    ~DLP_SRVR() = default;

    virtual std::shared_ptr<Kernel::SessionRequestHandler> GetServiceFrameworkSharedPtr();
    virtual bool IsHost() {
        return true;
    }

private:
    void IsChild(Kernel::HLERequestContext& ctx);

    SERVICE_SERIALIZATION_SIMPLE
};

} // namespace Service::DLP

BOOST_CLASS_EXPORT_KEY(Service::DLP::DLP_SRVR)
