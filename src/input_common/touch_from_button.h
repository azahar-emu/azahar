// Copyright 2020 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include <memory>
#include "core/frontend/input.h"

namespace InputCommon {

/**
 * A touch device factory that takes a list of button devices and combines them into a touch device.
 */
class TouchFromButtonFactory final : public Input::Factory<Input::TouchDevice> {
public:
    /**
     * Creates a touch device from a list of button devices
     */
    std::unique_ptr<Input::TouchDevice> Create(const Common::ParamPackage& params) override;
};

} // namespace InputCommon
