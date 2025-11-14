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
    // adjust the StereoRenderOption setting to Off if appropriate on mobile
    Settings::StereoWhichDisplay whichDisplay = Settings::values.render_3d_which_display.GetValue();
    if (whichDisplay == Settings::StereoWhichDisplay::None ||
        whichDisplay == Settings::StereoWhichDisplay::PrimaryOnly && is_secondary ||
        whichDisplay == Settings::StereoWhichDisplay::SecondaryOnly && !is_secondary) {
        render_3d_mode = Settings::StereoRenderOption::Off;
    }
#endif
    return render_3d_mode;
}

bool EmuWindow::IsWithinTouchscreen(const Layout::FramebufferLayout& layout, unsigned framebuffer_x,
                                    unsigned framebuffer_y) {
#ifndef ANDROID
    // If separate windows and the touch is in the primary (top) screen, ignore it.
    if (Settings::values.layout_option.GetValue() == Settings::LayoutOption::SeparateWindows &&
        ((!is_secondary && !Settings::values.swap_screen.GetValue()) ||
         (is_secondary && Settings::values.swap_screen.GetValue()))) {
        return false;
    }
#endif
    Settings::StereoRenderOption render_3d_mode = get3DMode();

    if (framebuffer_x > layout.width / 2 &&
        render_3d_mode == Settings::StereoRenderOption::SideBySideFull) {
        framebuffer_x = static_cast<unsigned>(framebuffer_x - layout.width / 2);
    }
    if (render_3d_mode == Settings::StereoRenderOption::SideBySide) {
        return (framebuffer_y >= layout.bottom_screen.top &&
                framebuffer_y < layout.bottom_screen.bottom &&
                ((framebuffer_x >= layout.bottom_screen.left / 2 &&
                  framebuffer_x < layout.bottom_screen.right / 2) ||
                 (framebuffer_x >= (layout.bottom_screen.left / 2) + (layout.width / 2) &&
                  framebuffer_x < (layout.bottom_screen.right / 2) + (layout.width / 2))));
    } else if (render_3d_mode == Settings::StereoRenderOption::CardboardVR) {
        return (framebuffer_y >= layout.bottom_screen.top &&
                framebuffer_y < layout.bottom_screen.bottom &&
                ((framebuffer_x >= layout.bottom_screen.left &&
                  framebuffer_x < layout.bottom_screen.right) ||
                 (framebuffer_x >= layout.cardboard.bottom_screen_right_eye + (layout.width / 2) &&
                  framebuffer_x < layout.cardboard.bottom_screen_right_eye +
                                      layout.bottom_screen.GetWidth() + (layout.width / 2))));
    } else {
        return (framebuffer_y >= layout.bottom_screen.top &&
                framebuffer_y < layout.bottom_screen.bottom &&
                framebuffer_x >= layout.bottom_screen.left &&
                framebuffer_x < layout.bottom_screen.right);
    }
}

std::tuple<unsigned, unsigned> EmuWindow::ClipToTouchScreen(unsigned new_x, unsigned new_y) const {

    Settings::StereoRenderOption render_3d_mode = get3DMode();

    if (new_x >= framebuffer_layout.width / 2) {
        if (render_3d_mode == Settings::StereoRenderOption::SideBySide ||
            render_3d_mode == Settings::StereoRenderOption::SideBySideFull)
            new_x -= framebuffer_layout.width / 2;
        else if (render_3d_mode == Settings::StereoRenderOption::CardboardVR)
            new_x -=
                (framebuffer_layout.width / 2) - (framebuffer_layout.cardboard.user_x_shift * 2);
    }

    if (render_3d_mode == Settings::StereoRenderOption::SideBySide) {
        new_x = std::max(new_x, framebuffer_layout.bottom_screen.left / 2);
        new_x = std::min(new_x, framebuffer_layout.bottom_screen.right / 2 - 1);
    } else {
        new_x = std::max(new_x, framebuffer_layout.bottom_screen.left);
        new_x = std::min(new_x, framebuffer_layout.bottom_screen.right - 1);
    }

    new_y = std::max(new_y, framebuffer_layout.bottom_screen.top);
    new_y = std::min(new_y, framebuffer_layout.bottom_screen.bottom - 1);

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
    if (!IsWithinTouchscreen(framebuffer_layout, framebuffer_x, framebuffer_y))
        return false;
    Settings::StereoRenderOption render_3d_mode = get3DMode();

    if (framebuffer_x >= framebuffer_layout.width / 2) {
        if (render_3d_mode == Settings::StereoRenderOption::SideBySide ||
            render_3d_mode == Settings::StereoRenderOption::SideBySideFull)
            framebuffer_x -= framebuffer_layout.width / 2;
        else if (render_3d_mode == Settings::StereoRenderOption::CardboardVR)
            framebuffer_x -=
                (framebuffer_layout.width / 2) - (framebuffer_layout.cardboard.user_x_shift * 2);
    }

    std::scoped_lock guard(touch_state->mutex);

    if (render_3d_mode == Settings::StereoRenderOption::SideBySide) {
        touch_state->touch_x =
            static_cast<float>(framebuffer_x - framebuffer_layout.bottom_screen.left / 2) /
            (framebuffer_layout.bottom_screen.right / 2 -
             framebuffer_layout.bottom_screen.left / 2);
    } else {
        touch_state->touch_x =
            static_cast<float>(framebuffer_x - framebuffer_layout.bottom_screen.left) /
            (framebuffer_layout.bottom_screen.right - framebuffer_layout.bottom_screen.left);
    }
    touch_state->touch_y =
        static_cast<float>(framebuffer_y - framebuffer_layout.bottom_screen.top) /
        (framebuffer_layout.bottom_screen.bottom - framebuffer_layout.bottom_screen.top);

    if (!framebuffer_layout.is_rotated) {
        std::swap(touch_state->touch_x, touch_state->touch_y);
        touch_state->touch_x = 1.f - touch_state->touch_x;
    }

    touch_state->touch_pressed = true;
    return true;
}

void EmuWindow::TouchReleased() {
    std::scoped_lock guard{touch_state->mutex};
    touch_state->touch_pressed = false;
    touch_state->touch_x = 0;
    touch_state->touch_y = 0;
}

void EmuWindow::TouchMoved(unsigned framebuffer_x, unsigned framebuffer_y) {
    if (!touch_state->touch_pressed)
        return;

    if (!IsWithinTouchscreen(framebuffer_layout, framebuffer_x, framebuffer_y))
        std::tie(framebuffer_x, framebuffer_y) = ClipToTouchScreen(framebuffer_x, framebuffer_y);

    TouchPressed(framebuffer_x, framebuffer_y);
}

void EmuWindow::UpdateCurrentFramebufferLayout(u32 width, u32 height, bool is_portrait_mode) {
    Layout::FramebufferLayout layout;
    const Settings::LayoutOption layout_option = Settings::values.layout_option.GetValue();
    const Settings::StereoRenderOption stereo_option = get3DMode();
    bool render_full_stereo = (stereo_option == Settings::StereoRenderOption::SideBySideFull);
    bool is_bottom = is_secondary;
    if (Settings::values.swap_screen.GetValue())
        is_bottom = !is_bottom;

    const Settings::PortraitLayoutOption portrait_layout_option =
        Settings::values.portrait_layout_option.GetValue();
    const auto min_size = is_portrait_mode
                              ? Layout::GetMinimumSizeFromPortraitLayout()
                              : Layout::GetMinimumSizeFromLayout(
                                    layout_option, Settings::values.upright_screen.GetValue());

    width = std::max(width, min_size.first);
    height = std::max(height, min_size.second);
    if (render_full_stereo) {
        width = width / 2;
    }
    if (is_portrait_mode) {
        switch (portrait_layout_option) {
        case Settings::PortraitLayoutOption::PortraitTopFullWidth:
            layout = Layout::PortraitTopFullFrameLayout(width, height,
                                                        Settings::values.swap_screen.GetValue(),
                                                        Settings::values.upright_screen.GetValue());
            break;
        case Settings::PortraitLayoutOption::PortraitCustomLayout:
            layout = Layout::CustomFrameLayout(
                width, height, Settings::values.swap_screen.GetValue(), is_portrait_mode);
            break;
        case Settings::PortraitLayoutOption::PortraitOriginal:
            layout = Layout::PortraitOriginalLayout(width, height,
                                                    Settings::values.swap_screen.GetValue(),
                                                    Settings::values.upright_screen.GetValue());
            break;
        }
    } else {
        switch (layout_option) {
        case Settings::LayoutOption::CustomLayout:
            layout = Layout::CustomFrameLayout(
                width, height, Settings::values.swap_screen.GetValue(), is_portrait_mode);
            break;
        case Settings::LayoutOption::SingleScreen:
            layout =
                Layout::SingleFrameLayout(width, height, Settings::values.swap_screen.GetValue(),
                                          Settings::values.upright_screen.GetValue());
            break;
        case Settings::LayoutOption::LargeScreen:
            layout =
                Layout::LargeFrameLayout(width, height, Settings::values.swap_screen.GetValue(),
                                         Settings::values.upright_screen.GetValue(),
                                         Settings::values.large_screen_proportion.GetValue(),
                                         Settings::values.small_screen_position.GetValue());
            break;
        case Settings::LayoutOption::HybridScreen:
            layout =
                Layout::HybridScreenLayout(width, height, Settings::values.swap_screen.GetValue(),
                                           Settings::values.upright_screen.GetValue());
            break;
        case Settings::LayoutOption::SideScreen:
            layout =
                Layout::LargeFrameLayout(width, height, Settings::values.swap_screen.GetValue(),
                                         Settings::values.upright_screen.GetValue(), 1.0f,
                                         Settings::SmallScreenPosition::MiddleRight);
            break;
#ifndef ANDROID
        case Settings::LayoutOption::SeparateWindows:
            layout = Layout::SeparateWindowsLayout(width, height, is_secondary,
                                                   Settings::values.upright_screen.GetValue());
            break;
#endif
        case Settings::LayoutOption::Default:
        default:
            layout =
                Layout::DefaultFrameLayout(width, height, Settings::values.swap_screen.GetValue(),
                                           Settings::values.upright_screen.GetValue());
            break;
        }
    }
#ifdef ANDROID
    if (is_secondary) {
        layout = Layout::AndroidSecondaryLayout(width, height);
    }
#endif

    if (render_full_stereo) {
        layout.width = width * 2;
    }
    UpdateMinimumWindowSize(min_size);

    if (Settings::values.render_3d.GetValue() == Settings::StereoRenderOption::CardboardVR) {
        layout = Layout::GetCardboardSettings(layout);
    }
    layout.render_3d_mode = stereo_option;
    NotifyFramebufferLayoutChanged(layout);
}

void EmuWindow::UpdateMinimumWindowSize(std::pair<unsigned, unsigned> min_size) {
    WindowConfig new_config = config;
    new_config.min_client_area_size = min_size;
    SetConfig(new_config);
    ProcessConfigurationChanges();
}

} // namespace Frontend
