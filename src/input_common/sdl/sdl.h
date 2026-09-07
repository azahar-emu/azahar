// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <vector>
#include "core/frontend/input.h"
#include "input_common/main.h"

union SDL_Event;

namespace Common {
class ParamPackage;
} // namespace Common

namespace InputCommon::Polling {
class DevicePoller;
enum class DeviceType;
} // namespace InputCommon::Polling

namespace InputCommon::SDL {

class State {
public:
    using Pollers = std::vector<std::unique_ptr<Polling::DevicePoller>>;

    /// Unregisters SDL device factories and shut them down.
    virtual ~State() = default;

    virtual Pollers GetPollers(Polling::DeviceType type) = 0;

    virtual float GetSystemBatteryLevel() = 0;
    virtual bool GetSystemBatteryChargeState() = 0;
};

class NullState : public State {
public:
    Pollers GetPollers(Polling::DeviceType type) override {
        return {};
    }

    virtual float GetSystemBatteryLevel() {
        return 1.0;
    }

    virtual bool GetSystemBatteryChargeState() {
        return true;
    }
};

std::unique_ptr<State> Init();

} // namespace InputCommon::SDL
