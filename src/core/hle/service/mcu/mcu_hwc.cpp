// Copyright 2024-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#include "common/archives.h"
#include "core/core.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/shared_page.h"
#include "core/hle/service/mcu/mcu_hwc.h"
#include "core/hle/service/mcu/mcu_rtc.h"
#include "input_common/main.h"

SERVICE_CONSTRUCT_IMPL(Service::MCU::HWC)
SERIALIZE_EXPORT_IMPL(Service::MCU::HWC)

namespace Service::MCU {

HWC::HWC(Core::System& _system) : ServiceFramework("mcu::HWC", 1), system(_system) {
    static const FunctionInfo functions[] = {
        // clang-format off
        {0x0001, nullptr, "ReadRegister"},
        {0x0002, nullptr, "WriteRegister"},
        {0x0003, nullptr, "GetInfoRegisters"},
        {0x0004, &HWC::GetBatteryVoltage, "GetBatteryVoltage"},
        {0x0005, &HWC::GetBatteryLevel, "GetBatteryLevel"},
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

#pragma optimize("", off)

void HWC::GetBatteryVoltage(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);

    // Extrapolate value from information on 3DBrew and shared page data.
    // On real hardware the module talks to the MCU instead.
    constexpr float max_level = 3.9f;
    constexpr float min_level = 3.4f;
    constexpr float charging_offset = 0.2f;
    constexpr float vref = 5.0f;

    const auto& battery_state = system.Kernel().GetSharedPageHandler().GetBatteryState();
    float curr_level =
        (((battery_state.charge_level.Value() / 5.f) * (max_level - min_level)) + min_level) +
        (battery_state.is_charging ? charging_offset : 0.f);

    u8 final_val = static_cast<u8>((curr_level / vref) * 256);
    rb.Push(final_val);
}

void HWC::GetBatteryLevel(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);

    // This function returns the percentage value, so needs to be
    // calculated from the settings.
    float percentage{};
    switch (Settings::values.battery_state_source.GetValue()) {
    case Settings::BatteryLevelSource::System:
        percentage = InputCommon::GetSystemBatteryState().percentage;
        break;
    case Settings::BatteryLevelSource::Fixed:
        constexpr float level_to_percentage[] = {0.05f, 0.1f, 0.25f, 0.50f, 1.f};
        percentage =
            level_to_percentage[std::min<u8>(Settings::values.battery_level.GetValue(), 4)];
        break;
    }
    rb.Push(static_cast<u8>(percentage * 100));
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
