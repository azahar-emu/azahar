// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included..

#pragma once

#include "core/hle/service/service.h"

namespace Service::CAM {

class CAM_Q : public ServiceFramework<CAM_Q> {
public:
    CAM_Q();

private:
    SERVICE_SERIALIZATION_SIMPLE
};

} // namespace Service::CAM

BOOST_CLASS_EXPORT_KEY(Service::CAM::CAM_Q)
