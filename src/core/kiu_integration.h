// Licensed under GPLv2 or any later version.
// Glue between Azahar's core and the emulator-independent kiu_qol library.
#pragma once

#include <optional>

#include "common/common_types.h"

namespace Core {
class System;
}
namespace Loader {
class AppLoader;
}

namespace KiuIntegration {

/// Called once the game process exists; decides whether the QOL layer is active.
void OnGameLoaded(Core::System& system, Loader::AppLoader& loader, u64 title_id);
void OnGameStopped();

struct Touch {
    float x, y; ///< bottom-screen pixels
    bool pressed;
};

/// Called from HID every pad update. Returns the touch the game should see, or nullopt when the
/// QOL layer is inactive (HID then uses its normal touch sources).
std::optional<Touch> SampleTouch(Core::System& system, float dt_seconds);

} // namespace KiuIntegration
