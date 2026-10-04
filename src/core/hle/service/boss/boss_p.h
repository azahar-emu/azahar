// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included..

#pragma once

#include "core/hle/service/boss/boss.h"

namespace Service::BOSS {

class BOSS_P final : public Module::Interface {
public:
    explicit BOSS_P(std::shared_ptr<Module> boss);

private:
    SERVICE_SERIALIZATION(BOSS_P, boss, Module)
};

} // namespace Service::BOSS

BOOST_CLASS_EXPORT_KEY(Service::BOSS::BOSS_P)
BOOST_SERIALIZATION_CONSTRUCT(Service::BOSS::BOSS_P)
