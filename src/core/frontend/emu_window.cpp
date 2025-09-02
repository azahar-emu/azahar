// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cmath>
#include <mutex>
#include "common/settings.h"
#include "core/3ds.h"
#include "core/frontend/emu_window.h"
#include "core/frontend/input.h"

namespace Frontend {
/// We need a global touch state that is shared across the different window instances
static std::weak_ptr<EmuWindow::TouchState> global_touch_state;

GraphicsContext::~GraphicsContext() = default;

class EmuWindow::TouchState : public Input::Factory<Input::TouchDevice>,
                              public std::enable_shared_from_this<TouchState> {
public:
    std::unique_ptr<Input::TouchDevice> Create(const Common::ParamPackage&) override {
        return std::make_unique<Device>(shared_from_this());
    }

    std::mutex mutex;

    bool touch_pressed = false; ///< True if touchpad area is currently pressed, otherwise false
    float touch_x = 0.0f;       ///< Touchpad X-position
    float touch_y = 0.0f;       ///< Touchpad Y-position
    int touched_index =
        -1; /// keeps track of the Screen index of the last-touched screen, for clipping

private:
    class Device : public Input::TouchDevice {
    public:
        explicit Device(std::weak_ptr<TouchState>&& touch_state) : touch_state(touch_state) {}
        std::tuple<float, float, bool> GetStatus() const override {
            if (auto state = touch_state.lock()) {
                std::scoped_lock guard{state->mutex};
                return std::make_tuple(state->touch_x, state->touch_y, state->touch_pressed);
            }
            return std::make_tuple(0.0f, 0.0f, false);
        }

    private:
        std::weak_ptr<TouchState> touch_state;
    };
};

EmuWindow::EmuWindow() {
    CreateTouchState();
};

EmuWindow::EmuWindow(bool is_secondary_) : is_secondary{is_secondary_} {
    CreateTouchState();
}

EmuWindow::~EmuWindow() = default;

Settings::StereoRenderOption EmuWindow::get3DMode() const {
    Settings::StereoRenderOption render_3d_mode = Settings::values.render_3d.GetValue();
#ifndef ANDROID
    // on desktop, if separate windows and this is the bottom screen, then no stereo
    if (Settings::values.layout_option.GetValue() == Settings::LayoutOption::SeparateWindows &&
        ((!is_secondary && Settings::values.swap_screen.GetValue()) ||
         (is_secondary && !Settings::values.swap_screen.GetValue()))) {
        render_3d_mode = Settings::StereoRenderOption::Off;
    }
#else
    // on mobile, if this is the primary screen and render_3d is set to secondary only
    // then no stereo
    if (!is_secondary && Settings::values.render_3d_secondary_only) {
        render_3d_mode = Settings::StereoRenderOption::Off;
    }
#endif
    return render_3d_mode;
}

int EmuWindow::WhichTouchscreen(const Layout::FramebufferLayout& layout, unsigned framebuffer_x,
                                unsigned framebuffer_y) {

    // iterate thorugh all layout screens looking for bottom screens, then see if we are inside them
    for (int i = 0; i < layout.screens.size(); i++) {
        Layout::Screen s = layout.screens[i];
        if (s.is_bottom && framebuffer_x > s.rect.left && framebuffer_x < s.rect.right &&
            framebuffer_y > s.rect.top && framebuffer_y < s.rect.bottom) {
            return i;
        }
    }
    return -1;
}

std::tuple<unsigned, unsigned> EmuWindow::ClipToTouchScreen(unsigned new_x, unsigned new_y,
                                                            int index) const {

    new_y = std::max(new_y, framebuffer_layout.screens[index].rect.top);
    new_y = std::min(new_y, framebuffer_layout.screens[index].rect.bottom - 1);

    return std::make_tuple(new_x, new_y);
}

void EmuWindow::CreateTouchState() {
    touch_state = global_touch_state.lock();
    if (touch_state) {
        return;
    }
    touch_state = std::make_shared<TouchState>();
    Input::RegisterFactory<Input::TouchDevice>("emu_window", touch_state);
    global_touch_state = touch_state;
}

bool EmuWindow::TouchPressed(unsigned framebuffer_x, unsigned framebuffer_y) {
    int screenIndex = WhichTouchscreen(framebuffer_layout, framebuffer_x, framebuffer_y);
    if (screenIndex < 0)
        return false;
    Settings::StereoRenderOption render_3d_mode = get3DMode();

    if (render_3d_mode == Settings::StereoRenderOption::CardboardVR)
        framebuffer_x -=
            (framebuffer_layout.width / 2) - (framebuffer_layout.cardboard.user_x_shift * 2);

    std::scoped_lock guard(touch_state->mutex);

    touch_state->touch_x =
        static_cast<float>(framebuffer_x - framebuffer_layout.screens[screenIndex].rect.left) /
        (framebuffer_layout.screens[screenIndex].rect.right -
         framebuffer_layout.screens[screenIndex].rect.left);

    touch_state->touch_y =
        static_cast<float>(framebuffer_y - framebuffer_layout.screens[screenIndex].rect.top) /
        (framebuffer_layout.screens[screenIndex].rect.bottom -
         framebuffer_layout.screens[screenIndex].rect.top);

    if (framebuffer_layout.orientation == Layout::DisplayOrientation::Portrait) {
        std::swap(touch_state->touch_x, touch_state->touch_y);
        touch_state->touch_x = 1.f - touch_state->touch_x;
    }

    touch_state->touch_pressed = true;
    touch_state->touched_index = screenIndex;
    return true;
}

void EmuWindow::TouchReleased() {
    std::scoped_lock guard{touch_state->mutex};
    touch_state->touch_pressed = false;
    touch_state->touch_x = 0;
    touch_state->touch_y = 0;
    touch_state->touched_index = -1;
}

void EmuWindow::TouchMoved(unsigned framebuffer_x, unsigned framebuffer_y) {
    if (!touch_state->touch_pressed)
        return;
    int screenIndex = WhichTouchscreen(framebuffer_layout, framebuffer_x, framebuffer_y);
    if (screenIndex == -1)
        std::tie(framebuffer_x, framebuffer_y) =
            ClipToTouchScreen(framebuffer_x, framebuffer_y, screenIndex);

    TouchPressed(framebuffer_x, framebuffer_y);
}

void EmuWindow::UpdateCurrentFramebufferLayout(u32 width, u32 height, bool is_portrait_mode) {
    Layout::FramebufferLayout layout;
    Settings::LayoutOption layout_option = Settings::values.layout_option.GetValue();
    const Settings::StereoRenderOption stereo_option = get3DMode();
    bool swapped = Settings::values.swap_screen.GetValue();
    const bool upright = Settings::values.upright_screen.GetValue();
    const bool swap_eyes =
        stereo_option == Settings::StereoRenderOption::Off
            ? Settings::values.mono_render_option.GetValue() == Settings::MonoRenderOption::RightEye
            : Settings::values.swap_eyes_3d.GetValue();
    bool is_bottom = is_secondary;
    bool is_mobile = false;
#ifdef ANDROID
    is_mobile = true;
#endif
    if (Settings::values.swap_screen.GetValue())
        is_bottom = !is_bottom;

    const Settings::PortraitLayoutOption portrait_layout_option =
        Settings::values.portrait_layout_option.GetValue();
    const auto min_size = is_portrait_mode ? Layout::GetMinimumSizeFromPortraitLayout()
                                           : Layout::GetMinimumSizeFromLayout(layout_option);

    width = std::max(width, min_size.first);
    height = std::max(height, min_size.second);

    if (is_portrait_mode) {
        layout = Layout::CreatePortraitLayout(portrait_layout_option, width, height, swapped,
                                              upright, stereo_option, swap_eyes);
    } else if (is_mobile && is_secondary) { // TODO: Let Pablo look at this and help make it better?
        layout = Layout::CreateMobileSecondaryLayout(
            Settings::values.secondary_display_layout.GetValue(), width, height, swapped, upright,
            stereo_option, swap_eyes);
    } else {
#ifndef ANDROID
        if (layout_option == Settings::LayoutOption::SeparateWindows) {
            layout_option = Settings::LayoutOption::SingleScreen;
            swapped = is_bottom;
        }
#endif
        layout = Layout::CreateLayout(layout_option, width, height, swapped, upright, stereo_option,
                                      swap_eyes);
    }

    UpdateMinimumWindowSize(min_size);

    if (Settings::values.render_3d.GetValue() == Settings::StereoRenderOption::CardboardVR) {
        layout = Layout::GetCardboardSettings(layout);
    }
    NotifyFramebufferLayoutChanged(layout);
}

void EmuWindow::UpdateMinimumWindowSize(std::pair<unsigned, unsigned> min_size) {
    WindowConfig new_config = config;
    new_config.min_client_area_size = min_size;
    SetConfig(new_config);
    ProcessConfigurationChanges();
}

} // namespace Frontend
