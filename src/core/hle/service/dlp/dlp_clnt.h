// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/hle/service/service.h"
#include "dlp_clt_base.h"

namespace Service::DLP {

class DLP_CLNT final : public ServiceFramework<DLP_CLNT>, public DLP_Clt_Base {
public:
    DLP_CLNT();
    virtual ~DLP_CLNT() = default;
    
    virtual std::shared_ptr<Kernel::SessionRequestHandler> GetServiceFrameworkSharedPtr();
private:
    SERVICE_SERIALIZATION_SIMPLE
    
    virtual bool IsFKCL() {return false;}
    
    bool is_preparing_for_update = false;
    bool is_updating = false;
    Network::MacAddress mac_addr_update;
    
    u32 ClientNeedsDup();
    
    void Initialize(Kernel::HLERequestContext& ctx);
    void Finalize(Kernel::HLERequestContext& ctx);
    void GetCupVersion(Kernel::HLERequestContext& ctx);
    void StartTitleDownload(Kernel::HLERequestContext& ctx);
    void PrepareForSystemDownload(Kernel::HLERequestContext& ctx);
    void StartSystemDownload(Kernel::HLERequestContext& ctx);
    void GetDupAvailability(Kernel::HLERequestContext& ctx);
};

} // namespace Service::DLP

BOOST_CLASS_EXPORT_KEY(Service::DLP::DLP_CLNT)
