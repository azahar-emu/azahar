#include <unordered_set>
#include <SDL.h>
#include "sdl_impl.h"

namespace InputCommon::SDL {
class SDLJoystick {
public:
    SDLJoystick(std::string guid_, int port_, SDL_Joystick* joystick,
                SDL_GameController* game_controller)
        : guid{std::move(guid_)}, port{port_}, sdl_joystick{joystick, &SDL_JoystickClose},
          sdl_controller{game_controller, &SDL_GameControllerClose} {
        EnableMotion();
        CreateControllerButtonMap();
    }

    bool IsButtonMappedToController(int button) const {
        return mapped_joystick_buttons.count(button) > 0;
    }

    void EnableMotion() {
        if (!sdl_controller) {
            return;
        }
#if SDL_VERSION_ATLEAST(2, 0, 14)
        SDL_GameController* controller = sdl_controller.get();

        if (HasMotion()) {
            SDL_GameControllerSetSensorEnabled(controller, SDL_SENSOR_ACCEL, SDL_FALSE);
            SDL_GameControllerSetSensorEnabled(controller, SDL_SENSOR_GYRO, SDL_FALSE);
        }
        has_accel = SDL_GameControllerHasSensor(controller, SDL_SENSOR_ACCEL) == SDL_TRUE;
        has_gyro = SDL_GameControllerHasSensor(controller, SDL_SENSOR_GYRO) == SDL_TRUE;
        if (has_accel) {
            SDL_GameControllerSetSensorEnabled(controller, SDL_SENSOR_ACCEL, SDL_TRUE);
        }
        if (has_gyro) {
            SDL_GameControllerSetSensorEnabled(controller, SDL_SENSOR_GYRO, SDL_TRUE);
        }
#endif
    }

    bool HasMotion() const {
        return has_gyro || has_accel;
    }

    bool GetButton(int button, bool isController) const {
        if (!sdl_joystick)
            return false;
        if (isController)
            return SDL_GameControllerGetButton(sdl_controller.get(),
                                               static_cast<SDL_GameControllerButton>(button));
        return SDL_JoystickGetButton(sdl_joystick.get(), button) != 0;
    }

    float GetAxis(int axis, bool isController) const {
        if (!sdl_joystick)
            return 0.0;
        // if we are using the game controller api, assume axis was the gamepad axis not the
        // joystick axis

        if (isController)
            return SDL_GameControllerGetAxis(sdl_controller.get(),
                                             static_cast<SDL_GameControllerAxis>(axis)) /
                   32767.0f;
        return SDL_JoystickGetAxis(sdl_joystick.get(), axis) / 32767.0f;
    }

    std::tuple<float, float> GetAnalog(int axis_x, int axis_y, bool isController) const {
        float x = GetAxis(axis_x, isController);
        float y = GetAxis(axis_y, isController);
        y = -y; // 3DS uses an y-axis inverse from SDL

        // Make sure the coordinates are in the unit circle,
        // otherwise normalize it.
        float r = x * x + y * y;
        if (r > 1.0f) {
            r = std::sqrt(r);
            x /= r;
            y /= r;
        }

        return std::make_tuple(x, y);
    }

    bool GetHatDirection(int hat, Uint8 direction) const {
        // no need to worry about gamecontroller here - that api treats hats as buttons
        if (!sdl_joystick)
            return false;
        return SDL_JoystickGetHat(sdl_joystick.get(), hat) == direction;
    }

    void SetAccel(const float x, const float y, const float z) {
        std::lock_guard lock{mutex};
        state.accel.x = x;
        state.accel.y = y;
        state.accel.z = z;
    }
    void SetGyro(const float pitch, const float yaw, const float roll) {
        std::lock_guard lock{mutex};
        state.gyro.x = pitch;
        state.gyro.y = yaw;
        state.gyro.z = roll;
    }
    std::tuple<Common::Vec3<float>, Common::Vec3<float>> GetMotion() const {
        std::lock_guard lock{mutex};
        return std::make_tuple(state.accel, state.gyro);
    }

    /**
     * The guid of the joystick
     */
    const std::string& GetGUID() const {
        return guid;
    }

    /**
     * The number of joystick from the same type that were connected before this joystick
     */
    int GetPort() const {
        return port;
    }

    SDL_Joystick* GetSDLJoystick() const {
        return sdl_joystick.get();
    }

    SDL_GameController* GetSDLGameController() const {
        return sdl_controller.get();
    }

    void SetSDLJoystick(SDL_Joystick* joystick, SDL_GameController* controller) {
        sdl_joystick.reset(joystick);
        sdl_controller.reset(controller);
    }

private:
    struct State {
        Common::Vec3<float> accel;
        Common::Vec3<float> gyro;
    } state;
    std::string guid;
    int port;
    bool has_gyro{false};
    bool has_accel{false};
    std::unique_ptr<SDL_Joystick, decltype(&SDL_JoystickClose)> sdl_joystick;
    std::unique_ptr<SDL_GameController, decltype(&SDL_GameControllerClose)> sdl_controller;
    mutable std::mutex mutex;
    std::unordered_set<int> mapped_joystick_buttons;
    void CreateControllerButtonMap() {
        mapped_joystick_buttons.clear();

        if (!sdl_controller) {
            return; // Not a controller, no mapped buttons
        }

        // Check all controller buttons
        for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++) {
            auto bind = SDL_GameControllerGetBindForButton(
                sdl_controller.get(), static_cast<SDL_GameControllerButton>(i));

            if (bind.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON) {
                mapped_joystick_buttons.insert(bind.value.button);
            }
        }

        // Also check trigger axes that might be buttons
        auto lt_bind =
            SDL_GameControllerGetBindForAxis(sdl_controller.get(), SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        if (lt_bind.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON) {
            mapped_joystick_buttons.insert(lt_bind.value.button);
        }

        auto rt_bind = SDL_GameControllerGetBindForAxis(sdl_controller.get(),
                                                        SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        if (rt_bind.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON) {
            mapped_joystick_buttons.insert(rt_bind.value.button);
        }
    }
};
} // namespace InputCommon::SDL