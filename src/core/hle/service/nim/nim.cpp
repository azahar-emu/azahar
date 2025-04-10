// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/core.h"
#include "core/hle/service/nim/nim.h"
#include "core/hle/service/nim/nim_aoc.h"
#include "core/hle/service/nim/nim_s.h"
#include "core/hle/service/nim/nim_u.h"

namespace Service::NIM {

void InstallInterfaces(Core::System& system) {
    // Don't register HLE nim on initial setup
    if (system.IsInitialSetup()) {
        return;
    }
    auto& service_manager = system.ServiceManager();
    std::make_shared<NIM_AOC>()->InstallAsService(service_manager);
    std::make_shared<NIM_S>()->InstallAsService(service_manager);
    std::make_shared<NIM_U>(system)->InstallAsService(service_manager);
}

} // namespace Service::NIM
