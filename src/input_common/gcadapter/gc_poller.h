// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include <memory>
#include "common/settings.h"
#include "core/frontend/input.h"
#include "input_common/gcadapter/gc_adapter.h"
#include "input_common/main.h"

namespace InputCommon {

/**
 * A button device factory representing a gcpad. It receives gcpad events and forward them
 * to all button devices it created.
 */
class GCButtonFactory final : public Input::Factory<Input::ButtonDevice>,
                              public Polling::DevicePoller {
public:
public:
    explicit GCButtonFactory(std::shared_ptr<GCAdapter::Adapter> adapter_);

    std::unique_ptr<Input::ButtonDevice> Create(const Common::ParamPackage& params) override;

    Common::ParamPackage GetNextInput() override;
    Common::ParamPackage GetGcTo3DSMappedButton(int port, Settings::NativeButton::Values button);

    /// For device input configuration/polling
    void Start() override;
    void Stop() override;

    bool IsPolling() const {
        return polling;
    }

private:
    std::shared_ptr<GCAdapter::Adapter> adapter;
    bool polling{false};
};

/// An analog device factory that creates analog devices from GC Adapter
class GCAnalogFactory final : public Input::Factory<Input::AnalogDevice>,
                              public Polling::DevicePoller {
public:
    explicit GCAnalogFactory(std::shared_ptr<GCAdapter::Adapter> adapter_);

    std::unique_ptr<Input::AnalogDevice> Create(const Common::ParamPackage& params) override;

    Common::ParamPackage GetNextInput() override;
    Common::ParamPackage GetGcTo3DSMappedAnalog(int port, Settings::NativeAnalog::Values analog);

    /// For device input configuration/polling
    void Start() override;
    void Stop() override;

    bool IsPolling() const {
        return polling;
    }

private:
    std::shared_ptr<GCAdapter::Adapter> adapter;
    int analog_x_axis{-1};
    int analog_y_axis{-1};
    int controller_number{-1};
    bool polling{false};
};

} // namespace InputCommon
