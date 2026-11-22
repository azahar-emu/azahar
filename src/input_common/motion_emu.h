// Copyright 2017-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include "core/frontend/input.h"

namespace InputCommon {

class MotionEmuDevice;

class MotionEmu : public Input::Factory<Input::MotionDevice> {
public:
    /**
     * Creates a motion device emulated from mouse input
     * @param params contains parameters for creating the device:
     *     - "update_period": update period in milliseconds
     *     - "sensitivity": the coefficient converting mouse movement to tilting angle
     */
    std::unique_ptr<Input::MotionDevice> Create(const Common::ParamPackage& params) override;

    /**
     * Signals that a motion sensor tilt has begun. This also leaves look mode.
     * @param x the x-coordinate of the cursor
     * @param y the y-coordinate of the cursor
     */
    void BeginTilt(int x, int y);

    /**
     * Signals that a motion sensor tilt is occurring.
     * @param x the x-coordinate of the cursor
     * @param y the y-coordinate of the cursor
     */
    void Tilt(int x, int y);

    /**
     * Signals that a motion sensor tilt has ended.
     */
    void EndTilt();

    /**
     * Signals that a motion sensor look has begun. The device is held upright and the cursor
     * movement rotates it in pitch and yaw, continuing from the previous look orientation.
     * @param x the x-coordinate of the cursor
     * @param y the y-coordinate of the cursor
     */
    void BeginLook(int x, int y);

    /**
     * Signals that a motion sensor look is occurring.
     * @param x the x-coordinate of the cursor
     * @param y the y-coordinate of the cursor
     */
    void Look(int x, int y);

    /**
     * Signals that a motion sensor look has ended. The look orientation is kept until the next
     * tilt begins.
     */
    void EndLook();

private:
    std::weak_ptr<MotionEmuDevice> current_device;
};

} // namespace InputCommon
