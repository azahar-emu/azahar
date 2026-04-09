// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/archives.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/service/mcu/mcu_hwc.h"
#include "core/hle/service/mcu/mcu_rtc.h"

SERVICE_CONSTRUCT_IMPL(Service::MCU::HWC)
SERIALIZE_EXPORT_IMPL(Service::MCU::HWC)

namespace Service::MCU {

HWC::HWC(Core::System& _system) : ServiceFramework("mcu::HWC", 1), system(_system) {
    static const FunctionInfo functions[] = {
        // clang-format off
        {0x0001, nullptr, "ReadRegister"},
        {0x0002, nullptr, "WriteRegister"},
        {0x0003, nullptr, "GetInfoRegisters"},
        {0x0004, nullptr, "GetBatteryVoltage"},
        {0x0005, nullptr, "GetBatteryLevel"},
        {0x0006, nullptr, "SetPowerLEDPattern"},
        {0x0007, nullptr, "SetWifiLEDState"},
        {0x0008, nullptr, "SetCameraLEDPattern"},
        {0x0009, nullptr, "Set3DLEDState"},
        {0x000A, &HWC::SetInfoLEDPattern, "SetInfoLEDPattern"},
        {0x000B, nullptr, "GetSoundVolume"},
        {0x000C, nullptr, "SetTopScreenFlicker"},
        {0x000D, nullptr, "SetBottomScreenFlicker"},
        {0x000F, nullptr, "GetRtcTime"},
        {0x0010, nullptr, "GetMcuFwVerHigh"},
        {0x0011, nullptr, "GetMcuFwVerLow"},
        // clang-format on
    };
    RegisterHandlers(functions);
}

void HWC::SetInfoLEDPattern(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    auto pat = rp.PopRaw<MCU::InfoLedPattern>();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);

    auto mcu_rtc = MCU::RTC::GetService(system);
    if (mcu_rtc) {
        mcu_rtc->UpdateInfoLEDPattern(pat);
        rb.Push(ResultSuccess);
    } else {
        rb.Push(ResultUnknown);
    }
}

} // namespace Service::MCU
