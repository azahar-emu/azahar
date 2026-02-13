// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/hle/service/service.h"
#include "dlp_clt_base.h"

namespace Service::DLP {

class DLP_FKCL final : public ServiceFramework<DLP_FKCL>, public DLP_Clt_Base {
public:
    DLP_FKCL();
    ~DLP_FKCL() = default;

    virtual std::shared_ptr<Kernel::SessionRequestHandler> GetServiceFrameworkSharedPtr();
private:
    SERVICE_SERIALIZATION_SIMPLE

    virtual bool IsFKCL() {return true;}

    void Initialize(Kernel::HLERequestContext& ctx);
    void Finalize(Kernel::HLERequestContext& ctx);
    void InitializeWithName(Kernel::HLERequestContext& ctx);
};

} // namespace Service::DLP

BOOST_CLASS_EXPORT_KEY(Service::DLP::DLP_FKCL)
