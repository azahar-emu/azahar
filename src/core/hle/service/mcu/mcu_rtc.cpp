// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include "common/archives.h"
#include "common/vector_math.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/service/mcu/mcu.h"
#include "core/hle/service/mcu/mcu_rtc.h"

SERVICE_CONSTRUCT_IMPL(Service::MCU::RTC)
SERIALIZE_EXPORT_IMPL(Service::MCU::RTC)

namespace Service::MCU {

class InfoLedHandler {
public:
    InfoLedHandler() = default;
    ~InfoLedHandler() = default;

    static constexpr s64 CALLBACK_PERIOD_NS = 1'000'000'000ll / 60;  // 60Hz (~16ms)
    static constexpr s64 MCU_TICK_PERIOD_NS = 1'000'000'000ll / 512; // 512Hz (~2ms)

    void SetPattern(const InfoLedPattern& p) {
        current_pattern = p;
        pattern_changed = true;
    }

    void SetHeader(const InfoLedPattern::Header& header) {
        current_pattern.header = header;
        pattern_changed = true;
    }

    // The MCU led code is updated with a frequency of 512Hz on real hardware. However
    // it is not a very relevant feature for emulation, so to prevent slicing the core
    // timing too much let's update it every frame instead (60Hz) and adjust for it.
    void Tick(s64 cycles_late) {

        const s64 late_ns = cyclesToNs(cycles_late);

        // Accumulate elapsed time.
        arm_time_ns += CALLBACK_PERIOD_NS + late_ns;
        if (arm_time_ns < 0)
            arm_time_ns = 0;

        // Sync the MCU state up to the current ARM time
        while (arm_time_ns >= MCU_TICK_PERIOD_NS) {
            arm_time_ns -= MCU_TICK_PERIOD_NS;
            TickMCULed();
        }
    }

    Common::Vec3<u8> Color() const {
        return result_color;
    }

    // To save CPU time, do not tick if all smooth state has finished
    // and the pattern is all zero.
    bool NeedsTicking() {
        auto patAllZero = [this]() -> bool {
            u32* data = reinterpret_cast<u32*>(&current_pattern);
            for (size_t i = 0; i < sizeof(InfoLedPattern) / sizeof(u32); i++) {
                if (data[i])
                    return false;
            }
            return true;
        };

        return !patAllZero() || !state_r.Finished() || !state_g.Finished() || !state_b.Finished();
    }

    bool Status() const {
        return status_finished;
    }

private:
    struct LedSmoothState {
        s16 target = 0;
        s16 increment = 0;
        s16 current = 0;

        bool Finished() {
            return current == target;
        }

        friend class boost::serialization::access;
        template <class Archive>
        void serialize(Archive& ar, const unsigned int) {
            ar & target;
            ar & increment;
            ar & current;
        }
    };

    // Decompilation of MCU function at address 0x2f44
    void setSmoothState(LedSmoothState& state, u8 color) {
        // Looks like the color is multiplied for better precision
        state.target = static_cast<s16>(color) * 128;

        // Real HW makes sure ticks_to_progress is not 0 when the led pattern
        // is set through I2C. We check for it here instead as it's equivalent.
        const u8 ticks = std::max<u8>(current_pattern.header.ticks_to_progress, 1);
        state.increment = (state.target - state.current) / ticks;
    }

    // Decompilation of MCU function at address 0x2dc0
    static u8 updateSmoothState(LedSmoothState& status) {
        if (!status.Finished()) {
            if (std::abs(status.target - status.current) > std::abs(status.increment)) {
                status.current += status.increment;
            } else {
                status.current = status.target;
            }
        }

        return static_cast<u8>(status.current / 128);
    }

    // Decompilation of MCU function at address 0x2f6b
    // This function is called every 1/512 seconds
    void TickMCULed() {

        // Here, a few things happen.
        // If a global variable is set to 2 (0xff904), the led state is cleared.
        // If a global variable bit 0 is set (0xffe98), this function does not run at all.
        // If a global variable bit 7 is set (0xffe97), this function takes another path which
        //  runs function 0x2f1d instead of setSmoothState() to set the LED smooth status.
        //  This function seems to setup smooth to fade to off state.
        // TODO(PabloMK7): Figure out what those mean. Maybe power on/off related

        if (pattern_changed) {
            pattern_changed = false;
            status_finished = false;
            ticks_to_next_index = 0;
            index = 0;
        } else {
            if (ticks_to_next_index == 0) {
                ticks_to_next_index = current_pattern.header.ticks_per_index;

                if (index < InfoLedPattern::PATTERN_INDEX_COUNT - 1) {
                    status_finished = false;
                    index = (index + 1) % InfoLedPattern::PATTERN_INDEX_COUNT;
                    last_index_repeat_times = 0;
                } else {
                    status_finished = true;
                    if (current_pattern.header.last_index_repeat_times != 0xFF) {
                        last_index_repeat_times++;
                        if (last_index_repeat_times >
                            current_pattern.header.last_index_repeat_times) {
                            index = 0;
                        }
                    }
                }

                // Set smooth for the next index
                setSmoothState(state_r, current_pattern.r[index]);
                setSmoothState(state_g, current_pattern.g[index]);
                setSmoothState(state_b, current_pattern.b[index]);
            }
            ticks_to_next_index--;
        }

        // Update smooth state
        result_color.r() = updateSmoothState(state_r);
        result_color.g() = updateSmoothState(state_g);
        result_color.b() = updateSmoothState(state_b);
    }

private:
    InfoLedPattern current_pattern{};

    bool pattern_changed = false;
    bool status_finished = false;

    u8 ticks_to_next_index = 0;
    u8 index = 0;
    u8 last_index_repeat_times = 0;

    LedSmoothState state_r{};
    LedSmoothState state_g{};
    LedSmoothState state_b{};

    Common::Vec3<u8> result_color{};

    s64 arm_time_ns = 0;

    friend class boost::serialization::access;
    template <class Archive>
    void serialize(Archive& ar, const unsigned int) {
        ar & current_pattern;
        ar & pattern_changed;
        ar & status_finished;
        ar & ticks_to_next_index;
        ar & index;
        ar & last_index_repeat_times;
        ar & state_r;
        ar & state_g;
        ar & state_b;
        ar & result_color;
        ar & arm_time_ns;
    }
};

RTC::RTC(Core::System& _system) : ServiceFramework("mcu::RTC", 1), system(_system) {
    static const FunctionInfo functions[] = {
        // clang-format off
        {0x003B, &RTC::SetInfoLEDPattern, "SetInfoLEDPattern"},
        {0x003C, &RTC::SetInfoLEDPatternHeader, "SetInfoLEDPattern"},
        {0x003D, &RTC::GetInfoLEDStatus, "SetInfoLEDPattern"},
        // clang-format on
    };
    RegisterHandlers(functions);

    info_led = std::make_unique<InfoLedHandler>();
    info_led_tick_event =
        system.Kernel().timing.RegisterEvent("MCUTickInfoLED", [this](u64, s64 cycles_late) {
            info_led->Tick(cycles_late);
            system.SetInfoLEDColor(info_led->Color());
            if (info_led->NeedsTicking()) {
                system.Kernel().timing.ScheduleEvent(nsToCycles(InfoLedHandler::CALLBACK_PERIOD_NS),
                                                     info_led_tick_event, 0, 1);
            } else {
                info_led_ticking = false;
            }
        });
}

RTC::~RTC() {}

void RTC::UpdateInfoLEDPattern(const InfoLedPattern& pat) {
    info_led->SetPattern(pat);
    if (!info_led_ticking) {
        system.Kernel().timing.ScheduleEvent(0, info_led_tick_event, 0, 1);
        info_led_ticking = true;
    }
}

void RTC::UpdateInfoLEDHeader(const InfoLedPattern::Header& header) {
    info_led->SetHeader(header);
    if (!info_led_ticking) {
        system.Kernel().timing.ScheduleEvent(0, info_led_tick_event, 0, 1);
        info_led_ticking = true;
    }
}

bool RTC::GetInfoLEDStatusFinished() {
    return info_led->Status();
}

std::shared_ptr<RTC> RTC::GetService(Core::System& system) {
    return system.ServiceManager().GetService<RTC>("mcu::RTC");
}

void RTC::SetInfoLEDPattern(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    auto pat = rp.PopRaw<InfoLedPattern>();

    UpdateInfoLEDPattern(pat);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void RTC::SetInfoLEDPatternHeader(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    auto head = rp.PopRaw<InfoLedPattern::Header>();

    UpdateInfoLEDHeader(head);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void RTC::GetInfoLEDStatus(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);
    rb.Push(static_cast<u8>(GetInfoLEDStatusFinished()));
}

template <class Archive>
void RTC::serialize(Archive& ar, const unsigned int) {
    DEBUG_SERIALIZATION_POINT;
    ar& boost::serialization::base_object<Kernel::SessionRequestHandler>(*this);
    ar & info_led;
    ar & info_led_ticking;
}

} // namespace Service::MCU
