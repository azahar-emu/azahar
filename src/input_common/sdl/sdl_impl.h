// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <unordered_map>
#include "common/settings.h"
#include "common/threadsafe_queue.h"
#include "input_common/sdl/sdl.h"
#include "input_common/sdl/sdl_joystick.h"

union SDL_Event;
using SDL_Joystick = struct _SDL_Joystick;
using SDL_JoystickID = s32;
using SDL_GameController = struct _SDL_GameController;

namespace InputCommon::SDL {

class SDLButtonFactory;
class SDLAnalogFactory;
class SDLMotionFactory;
class SDLTouchFactory;

class SDLState : public State {
public:
    /// Initializes and registers SDL device factories
    SDLState();

    /// Unregisters SDL device factories and shut them down.
    ~SDLState() override;

    /// Handle SDL_Events for joysticks from SDL_PollEvent
    void HandleGameControllerEvent(const SDL_Event& event);

    // returns a pointer to a vector of joysticks that match the needs of this device.
    // will be a pointer because it will be updated when new joysticks are added
    std::shared_ptr<std::vector<std::shared_ptr<SDLJoystick>>> GetJoysticksByGUID(
        const std::string& guid);
    std::shared_ptr<SDLJoystick> GetSDLJoystickBySDLID(SDL_JoystickID sdl_id);

    Common::ParamPackage GetSDLControllerButtonBind(const std::string& guid, int port,
                                                    Settings::NativeButton::Values button);
    Common::ParamPackage GetSDLControllerAnalogBindByGUID(const std::string& guid, int port,
                                                          Settings::NativeAnalog::Values analog);

    /// Get all DevicePoller that use the SDL backend for a specific device type
    Pollers GetPollers(Polling::DeviceType type) override;

    /// Used by the Pollers during config
    std::atomic<bool> polling = false;
    Common::SPSCQueue<SDL_Event> event_queue;

private:
    void InitJoystick(int joystick_index);
    void CloseJoystick(SDL_JoystickID instance_id);

    /// Needs to be called before SDL_QuitSubSystem.
    void CloseJoysticks();

    /// Map of GUID of a list of corresponding virtual Joysticks
    std::unordered_map<std::string, std::shared_ptr<std::vector<std::shared_ptr<SDLJoystick>>>>
        joystick_map;
    // This vector keeps a list of all joysticks, ignoring guid
    std::shared_ptr<std::vector<std::shared_ptr<SDLJoystick>>> joystick_vector =
        std::make_shared<std::vector<std::shared_ptr<SDLJoystick>>>();
    std::mutex joystick_map_mutex;

    std::shared_ptr<SDLTouchFactory> touch_factory;
    std::shared_ptr<SDLButtonFactory> button_factory;
    std::shared_ptr<SDLAnalogFactory> analog_factory;
    std::shared_ptr<SDLMotionFactory> motion_factory;

    bool start_thread = false;
    std::atomic<bool> initialized = false;

    std::thread poll_thread;
};
} // namespace InputCommon::SDL
