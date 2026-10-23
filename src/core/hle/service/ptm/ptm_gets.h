// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include <memory>
#include "core/hle/service/ptm/ptm.h"

namespace Service::PTM {

class PTM_Gets final : public Module::Interface {
public:
    explicit PTM_Gets(std::shared_ptr<Module> ptm);

private:
    SERVICE_SERIALIZATION(PTM_Gets, ptm, Module)
};

} // namespace Service::PTM

BOOST_CLASS_EXPORT_KEY(Service::PTM::PTM_Gets)
BOOST_SERIALIZATION_CONSTRUCT(Service::PTM::PTM_Gets)
