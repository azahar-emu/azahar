// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include "core/hle/service/service.h"

namespace Service::NWM {

class NWM_CEC final : public ServiceFramework<NWM_CEC> {
public:
    NWM_CEC();

private:
    SERVICE_SERIALIZATION_SIMPLE
};

} // namespace Service::NWM

BOOST_CLASS_EXPORT_KEY(Service::NWM::NWM_CEC)
