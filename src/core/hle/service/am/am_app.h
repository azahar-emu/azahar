// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included..

#pragma once

#include "core/hle/service/am/am.h"

namespace Service::AM {

class AM_APP final : public Module::Interface {
public:
    explicit AM_APP(std::shared_ptr<Module> am);

private:
    SERVICE_SERIALIZATION(AM_APP, am, Module)
};

} // namespace Service::AM

BOOST_CLASS_EXPORT_KEY(Service::AM::AM_APP)
BOOST_SERIALIZATION_CONSTRUCT(Service::AM::AM_APP)
