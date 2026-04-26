// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/core_timing.h"
#include "core/hle/service/service.h"

namespace Service::MCU {
class InfoLedHandler;

struct InfoLedPattern {
    static constexpr size_t PATTERN_INDEX_COUNT = 32;

    struct Header {
        u8 ticks_per_index{};   // Amount of ticks to stay in the current index (1 tick == 1/512 s)
        u8 ticks_to_progress{}; // Amount of ticks to go from the previous value to the current
                                // index value. Normally, this only makes sense to be set to 0 to
                                // disable interpolation, or equal to "ticks_per_index" for linear
                                // interpolation. Any other value breaks the interpolation math.
        u8 last_index_repeat_times{}; // Amount of times to repeat the last index, as if the color
                                      // array had "last_index_repeat_times" more elements equal to
                                      // the last array value. (0xFF means repeat forever)
        u8 padding{};
    } header;

    // RGB color elements, corresponding to the LED PWM duty cycle.
    // (0x0 -> fully off, 0xFF -> fully on)
    std::array<u8, PATTERN_INDEX_COUNT> r{};
    std::array<u8, PATTERN_INDEX_COUNT> g{};
    std::array<u8, PATTERN_INDEX_COUNT> b{};

    friend class boost::serialization::access;
    template <class Archive>
    void serialize(Archive& ar, const unsigned int) {
        ar & header.ticks_per_index;
        ar & header.ticks_to_progress;
        ar & header.last_index_repeat_times;
        ar & header.padding;

        ar & r;
        ar & g;
        ar & b;
    }
};
static_assert(sizeof(InfoLedPattern) == 0x64);

class RTC final : public ServiceFramework<RTC> {
public:
    explicit RTC(Core::System& _system);
    ~RTC();

    void UpdateInfoLEDPattern(const InfoLedPattern& pat);

    void UpdateInfoLEDHeader(const InfoLedPattern::Header& header);

    bool GetInfoLEDStatusFinished();

    static std::shared_ptr<RTC> GetService(Core::System& system);

private:
    void SetInfoLEDPattern(Kernel::HLERequestContext& ctx);

    void SetInfoLEDPatternHeader(Kernel::HLERequestContext& ctx);

    void GetInfoLEDStatus(Kernel::HLERequestContext& ctx);

    Core::System& system;

    std::unique_ptr<InfoLedHandler> info_led;
    Core::TimingEventType* info_led_tick_event{};
    bool info_led_ticking{};

    template <class Archive>
    void serialize(Archive& ar, const unsigned int);
    friend class boost::serialization::access;
};

} // namespace Service::MCU

SERVICE_CONSTRUCT(Service::MCU::RTC)
BOOST_CLASS_EXPORT_KEY(Service::MCU::RTC)
