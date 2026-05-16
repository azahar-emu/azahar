// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/core.h"
#include "core/hle/service/mcu/mcu.h"
#include "core/hle/service/mcu/mcu_hwc.h"
#include "core/hle/service/mcu/mcu_rtc.h"

namespace Service::MCU {

void InstallInterfaces(Core::System& system) {
    auto& service_manager = system.ServiceManager();
    std::make_shared<HWC>(system)->InstallAsService(service_manager);
    std::make_shared<RTC>(system)->InstallAsService(service_manager);
}

} // namespace Service::MCU
