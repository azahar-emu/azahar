// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#include <atomic>
#include <list>
#include <mutex>
#include <utility>
#include "common/assert.h"
#include "common/threadsafe_queue.h"
#include "input_common/gcadapter/gc_adapter.h"
#include "input_common/gcadapter/gc_poller.h"

namespace InputCommon {
namespace {
constexpr std::array<GCAdapter::PadButton, Settings::NativeButton::NumButtons> gc_to_3ds_mapping{{
    GCAdapter::PadButton::ButtonA,
    GCAdapter::PadButton::ButtonB,
    GCAdapter::PadButton::ButtonX,
    GCAdapter::PadButton::ButtonY,
    GCAdapter::PadButton::ButtonUp,
    GCAdapter::PadButton::ButtonDown,
    GCAdapter::PadButton::ButtonLeft,
    GCAdapter::PadButton::ButtonRight,
    GCAdapter::PadButton::TriggerL,
    GCAdapter::PadButton::TriggerR,
    GCAdapter::PadButton::ButtonStart,
    GCAdapter::PadButton::TriggerZ,
    GCAdapter::PadButton::Undefined,
    GCAdapter::PadButton::Undefined,
    GCAdapter::PadButton::Undefined,
    GCAdapter::PadButton::Undefined,
    GCAdapter::PadButton::Undefined,
}};
}
class GCButton final : public Input::ButtonDevice {
public:
    explicit GCButton(int port_, int button_, GCAdapter::Adapter* adapter)
        : port(port_), button(button_), gcadapter(adapter) {}

    ~GCButton() override;

    bool GetStatus() const override {
        if (gcadapter->DeviceConnected(port)) {
            return (gcadapter->GetPadState(port).buttons & button) != 0;
        }
        return false;
    }

private:
    const int port;
    const int button;
    GCAdapter::Adapter* gcadapter;
};

class GCAxisButton final : public Input::ButtonDevice {
public:
    explicit GCAxisButton(int port_, int axis_, float threshold_, bool trigger_if_greater_,
                          GCAdapter::Adapter* adapter)
        : port(port_), axis(axis_), threshold(threshold_), trigger_if_greater(trigger_if_greater_),
          gcadapter(adapter) {}

    bool GetStatus() const override {
        if (gcadapter->DeviceConnected(port)) {
            const float current_axis_value = gcadapter->GetPadState(port).axis_values.at(axis);
            const float axis_value = current_axis_value / 128.0f;
            if (trigger_if_greater) {
                return axis_value > threshold;
            }
            return axis_value < -threshold;
        }
        return false;
    }

private:
    const u32 port;
    const u32 axis;
    float threshold;
    bool trigger_if_greater;
    const GCAdapter::Adapter* gcadapter;
};

GCButtonFactory::GCButtonFactory(std::shared_ptr<GCAdapter::Adapter> adapter_)
    : adapter(std::move(adapter_)) {}

GCButton::~GCButton() = default;

std::unique_ptr<Input::ButtonDevice> GCButtonFactory::Create(const Common::ParamPackage& params) {
    const int button_id = params.Get("button", 0);
    const int port = params.Get("port", 0);

    constexpr s32 PAD_STICK_ID = static_cast<s32>(GCAdapter::PadButton::Stick);

    // button is not an axis/stick button
    if (button_id != PAD_STICK_ID) {
        return std::make_unique<GCButton>(port, button_id, adapter.get());
    }

    // For Axis buttons, used by the binary sticks.
    if (button_id == PAD_STICK_ID) {
        const int axis = params.Get("axis", 0);
        const float threshold = params.Get("threshold", 0.25f);
        const std::string direction_name = params.Get("direction", "");
        bool trigger_if_greater;
        if (direction_name == "+") {
            trigger_if_greater = true;
        } else if (direction_name == "-") {
            trigger_if_greater = false;
        } else {
            trigger_if_greater = true;
            LOG_ERROR(Input, "Unknown direction {}", direction_name);
        }
        return std::make_unique<GCAxisButton>(port, axis, threshold, trigger_if_greater,
                                              adapter.get());
    }

    UNREACHABLE();
    return nullptr;
}

Common::ParamPackage GCButtonFactory::GetNextInput() {
    Common::ParamPackage params;
    GCAdapter::GCPadStatus pad;
    auto& queue = adapter->GetPadQueue();
    while (queue.Pop(pad)) {
        // This while loop will break on the earliest detected button
        params.Set("engine", "gcpad");
        params.Set("port", static_cast<s32>(pad.port));
        if (pad.button != GCAdapter::PadButton::Undefined) {
            params.Set("button", static_cast<u16>(pad.button));
        }

        // For Axis button implementation
        if (pad.axis != GCAdapter::PadAxes::Undefined) {
            params.Set("axis", static_cast<u8>(pad.axis));
            params.Set("button", static_cast<u16>(GCAdapter::PadButton::Stick));
            params.Set("threshold", "0.25");
            if (pad.axis_value > 0) {
                params.Set("direction", "+");
            } else {
                params.Set("direction", "-");
            }
            break;
        }
    }
    return params;
}

Common::ParamPackage GCButtonFactory::GetGcTo3DSMappedButton(
    int port, Settings::NativeButton::Values button) {
    Common::ParamPackage params({{"engine", "gcpad"}});
    params.Set("port", port);
    auto mapped_button = gc_to_3ds_mapping[static_cast<int>(button)];
    if (mapped_button != GCAdapter::PadButton::Undefined) {
        params.Set("button", static_cast<u16>(mapped_button));
    }
    return params;
}

void GCButtonFactory::Start() {
    polling = true;
    adapter->BeginConfiguration();
}

void GCButtonFactory::Stop() {
    polling = false;
    adapter->EndConfiguration();
}

class GCAnalog final : public Input::AnalogDevice {
public:
    explicit GCAnalog(u32 port_, u32 axis_x_, u32 axis_y_, float deadzone_,
                      const GCAdapter::Adapter* adapter)
        : port(port_), axis_x(axis_x_), axis_y(axis_y_), deadzone(deadzone_), gcadapter(adapter) {}

    float GetAxis(u32 axis) const {
        if (gcadapter->DeviceConnected(port)) {
            std::lock_guard lock{mutex};
            const auto axis_value =
                static_cast<float>(gcadapter->GetPadState(port).axis_values.at(axis));
            return (axis_value) / 50.0f;
        }
        return 0.0f;
    }

    std::pair<float, float> GetAnalog(u32 analog_axis_x, u32 analog_axis_y) const {
        float x = GetAxis(analog_axis_x);
        float y = GetAxis(analog_axis_y);
        // Make sure the coordinates are in the unit circle,
        // otherwise normalize it.
        float r = x * x + y * y;
        if (r > 1.0f) {
            r = std::sqrt(r);
            x /= r;
            y /= r;
        }

        return {x, y};
    }

    std::tuple<float, float> GetStatus() const override {
        const auto [x, y] = GetAnalog(axis_x, axis_y);
        const float r = std::sqrt((x * x) + (y * y));
        if (r > deadzone) {
            return {x / r * (r - deadzone) / (1 - deadzone),
                    y / r * (r - deadzone) / (1 - deadzone)};
        }
        return {0.0f, 0.0f};
    }

private:
    const u32 port;
    const u32 axis_x;
    const u32 axis_y;
    const float deadzone;
    const GCAdapter::Adapter* gcadapter;
    mutable std::mutex mutex;
};

/// An analog device factory that creates analog devices from GC Adapter
GCAnalogFactory::GCAnalogFactory(std::shared_ptr<GCAdapter::Adapter> adapter_)
    : adapter(std::move(adapter_)) {}

/**
 * Creates analog device from joystick axes
 * @param params contains parameters for creating the device:
 *     - "port": the nth gcpad on the adapter
 *     - "axis_x": the index of the axis to be bind as x-axis
 *     - "axis_y": the index of the axis to be bind as y-axis
 */
std::unique_ptr<Input::AnalogDevice> GCAnalogFactory::Create(const Common::ParamPackage& params) {
    const auto port = static_cast<u32>(params.Get("port", 0));
    const auto axis_x = static_cast<u32>(params.Get("axis_x", 0));
    const auto axis_y = static_cast<u32>(params.Get("axis_y", 1));
    const auto deadzone = std::clamp(params.Get("deadzone", 0.0f), 0.0f, 1.0f);

    return std::make_unique<GCAnalog>(port, axis_x, axis_y, deadzone, adapter.get());
}

void GCAnalogFactory::Start() {
    polling = true;
    adapter->BeginConfiguration();
}

void GCAnalogFactory::Stop() {
    polling = false;
    adapter->EndConfiguration();
}

Common::ParamPackage GCAnalogFactory::GetNextInput() {
    GCAdapter::GCPadStatus pad;
    Common::ParamPackage params;
    auto& queue = adapter->GetPadQueue();
    while (queue.Pop(pad)) {
        if (pad.button != GCAdapter::PadButton::Undefined) {
            params.Set("engine", "gcpad");
            params.Set("port", static_cast<s32>(pad.port));
            params.Set("button", static_cast<u16>(pad.button));
            return params;
        }
        if (pad.axis == GCAdapter::PadAxes::Undefined ||
            std::abs(static_cast<float>(pad.axis_value) / 128.0f) < 0.1f) {
            continue;
        }
        // An analog device needs two axes, so we need to store the axis for later and wait for
        // a second input event. The axes also must be from the same joystick.
        const u8 axis = static_cast<u8>(pad.axis);
        if (axis == 0 || axis == 1) {
            analog_x_axis = 0;
            analog_y_axis = 1;
            controller_number = static_cast<s32>(pad.port);
            break;
        }
        if (axis == 2 || axis == 3) {
            analog_x_axis = 2;
            analog_y_axis = 3;
            controller_number = static_cast<s32>(pad.port);
            break;
        }

        if (analog_x_axis == -1) {
            analog_x_axis = axis;
            controller_number = static_cast<s32>(pad.port);
        } else if (analog_y_axis == -1 && analog_x_axis != axis &&
                   controller_number == static_cast<s32>(pad.port)) {
            analog_y_axis = axis;
            break;
        }
    }
    if (analog_x_axis != -1 && analog_y_axis != -1) {
        params.Set("engine", "gcpad");
        params.Set("port", controller_number);
        params.Set("axis_x", analog_x_axis);
        params.Set("axis_y", analog_y_axis);
        analog_x_axis = -1;
        analog_y_axis = -1;
        controller_number = -1;
        return params;
    }
    return params;
}

Common::ParamPackage GCAnalogFactory::GetGcTo3DSMappedAnalog(
    int port, Settings::NativeAnalog::Values analog) {
    int x_axis, y_axis;
    Common::ParamPackage params({{"engine", "gcpad"}});
    params.Set("port", port);
    if (analog == Settings::NativeAnalog::Values::CirclePad) {
        x_axis = static_cast<s32>(GCAdapter::PadAxes::StickX);
        y_axis = static_cast<s32>(GCAdapter::PadAxes::StickY);
    } else if (analog == Settings::NativeAnalog::Values::CStick) {
        x_axis = static_cast<s32>(GCAdapter::PadAxes::SubstickX);
        y_axis = static_cast<s32>(GCAdapter::PadAxes::SubstickY);
    } else {
        LOG_WARNING(Input, "analog value out of range {}", analog);
        return {{}};
    }
    params.Set("axis_x", x_axis);
    params.Set("axis_y", y_axis);
    return params;
}

} // namespace InputCommon
