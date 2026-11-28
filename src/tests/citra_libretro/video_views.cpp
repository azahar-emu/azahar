// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "citra_libretro/core_settings.h"
#include "citra_libretro/emu_window/libretro_window.h"
#include "citra_libretro/video_views.h"
#include "common/logging/backend.h"
#include "common/settings.h"
#include "core/frontend/emu_window.h"

namespace {

using LibRetro::VideoViews::Mode;

constexpr Mode own{};
constexpr Mode pair_2d{.active = true};
constexpr Mode pair_stereo{.active = true, .stereo = true};
constexpr Mode single_2d{.active = true, .single = true};
constexpr Mode single_stereo{.active = true, .stereo = true, .single = true};

class TestWindow final : public Frontend::EmuWindow {
public:
    void PollEvents() override {}
};

/// Puts back every setting these tests change.
struct SettingsGuard {
    Settings::LayoutOption layout_option = Settings::values.layout_option.GetValue();
    Settings::StereoRenderOption render_3d = Settings::values.render_3d.GetValue();
    Settings::StereoWhichDisplay which_display =
        Settings::values.render_3d_which_display.GetValue();
    u32 factor_3d = Settings::values.factor_3d.GetValue();
    bool swap_screen = Settings::values.swap_screen.GetValue();
    u32 resolution_factor = Settings::values.resolution_factor.GetValue();
    LibRetro::CoreSettings core = LibRetro::settings;
    Mode mode = LibRetro::VideoViews::CurrentMode();

    ~SettingsGuard() {
        Settings::values.layout_option = layout_option;
        Settings::values.render_3d = render_3d;
        Settings::values.render_3d_which_display = which_display;
        Settings::values.factor_3d = factor_3d;
        Settings::values.swap_screen = swap_screen;
        Settings::values.resolution_factor = resolution_factor;
        LibRetro::settings = core;
        LibRetro::VideoViews::SetCurrentMode(mode);
    }
};

struct ExpectedView {
    unsigned x, y, width, height, screen, eye;
};

constexpr unsigned EYE_NONE = RETRO_VIDEO_VIEW_EYE_NONE;
constexpr unsigned EYE_LEFT = RETRO_VIDEO_VIEW_EYE_LEFT;
constexpr unsigned EYE_RIGHT = RETRO_VIDEO_VIEW_EYE_RIGHT;

void RequireViews(const std::vector<retro_video_view>& views,
                  const std::vector<ExpectedView>& expected) {
    REQUIRE(views.size() == expected.size());
    for (std::size_t i = 0; i < views.size(); ++i) {
        INFO("view " << i);
        REQUIRE(views[i].x == expected[i].x);
        REQUIRE(views[i].y == expected[i].y);
        REQUIRE(views[i].width == expected[i].width);
        REQUIRE(views[i].height == expected[i].height);
        REQUIRE(views[i].screen == expected[i].screen);
        REQUIRE(views[i].eye == expected[i].eye);
        REQUIRE(views[i].aspect_ratio == 0.0f);
    }
}

/// The map for the layout Azahar computes when it packs for views.
std::vector<retro_video_view> MapFor(const Mode& mode, bool swapped, u32 scale) {
    LibRetro::VideoViews::SetCurrentMode(mode);
    LibRetro::ApplyLayoutSettings();
    Settings::values.swap_screen = swapped;
    const auto [width, height] = LibRetro::VideoViews::PackedSize(mode, swapped, scale);
    TestWindow window;
    window.UpdateCurrentFramebufferLayout(width, height);
    const auto& layout = window.GetFramebufferLayout();
    REQUIRE(layout.width == width);
    REQUIRE(layout.height == height);
    return LibRetro::VideoViews::BuildMap(layout, mode.stereo, swapped);
}

template <typename Display>
std::set<std::string> Keys(const Display& display, bool visible) {
    std::set<std::string> keys;
    for (const auto& option : display) {
        if (option.visible == visible) {
            keys.insert(option.key);
        }
    }
    return keys;
}

/// The frontend's visibility of the Layout options the views override.
std::map<std::string, bool> LayoutDisplay(bool visible) {
    return {{"citra_large_screen_proportion", visible}, {"citra_render_3d", visible}};
}

/// Answers the calls the core options' visibility makes.
namespace FakeFrontend {

unsigned views_status = 0;
const char* frontend_layout = "auto";
/// As RetroArch while it reads a running core's system info.
bool ignore_calls = false;
retro_core_options_update_display_callback_t update_display = nullptr;
/// The options the core has hidden or shown; the others show.
std::map<std::string, bool> display;
const retro_core_options_v2* options = nullptr;

bool Environment(unsigned cmd, void* data) {
    if (ignore_calls) {
        return false;
    }
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *static_cast<unsigned*>(data) = 2;
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        display.clear();
        options = static_cast<const retro_core_options_v2*>(data);
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
        update_display =
            static_cast<const retro_core_options_update_display_callback*>(data)->callback;
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY: {
        const auto* option = static_cast<const retro_core_option_display*>(data);
        display[option->key] = option->visible;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VIDEO_VIEWS_STATUS:
        *static_cast<unsigned*>(data) = views_status;
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        auto* var = static_cast<retro_variable*>(data);
        if (std::string(var->key) != "citra_frontend_layout") {
            return false;
        }
        var->value = frontend_layout;
        return true;
    }
    default:
        return false;
    }
}

} // namespace FakeFrontend

} // namespace

TEST_CASE("Video views API values match RetroArch", "[libretro]") {
    REQUIRE(RETRO_ENVIRONMENT_SET_VIDEO_VIEWS == (95 | RETRO_ENVIRONMENT_EXPERIMENTAL));
    REQUIRE(RETRO_ENVIRONMENT_GET_VIDEO_VIEWS_STATUS == (96 | RETRO_ENVIRONMENT_EXPERIMENTAL));
    REQUIRE(RETRO_VIDEO_VIEWS_STATUS_PRESENTS == 1);
    REQUIRE(RETRO_VIDEO_VIEWS_STATUS_STEREO == 2);
    REQUIRE(RETRO_VIDEO_VIEWS_STATUS_HMD == 4);
    REQUIRE(RETRO_VIDEO_VIEW_EYE_NONE == 0);
    REQUIRE(RETRO_VIDEO_VIEW_EYE_LEFT == 1);
    REQUIRE(RETRO_VIDEO_VIEW_EYE_RIGHT == 2);
    REQUIRE(RETRO_VIDEO_VIEWS_MAX == 8);
    REQUIRE(RETRO_VIDEO_VIEWS_FLAG_TRACKED == 1);
    REQUIRE(sizeof(retro_video_view) == 6 * sizeof(unsigned) + sizeof(float));
    // The frontend writes the recommended view size back into the struct, so a short one would
    // let it write past the end.
    REQUIRE(offsetof(retro_video_views, recommended_view_height) ==
            sizeof(void*) + 4 * sizeof(unsigned));
}

TEST_CASE("VideoViews::SelectMode", "[libretro]") {
    using LibRetro::VideoViews::SelectMode;
    using Settings::LayoutOption;
    using Settings::StereoRenderOption;
    constexpr unsigned presents = RETRO_VIDEO_VIEWS_STATUS_PRESENTS;
    constexpr unsigned stereo = RETRO_VIDEO_VIEWS_STATUS_STEREO;
    constexpr unsigned headset = RETRO_VIDEO_VIEWS_STATUS_HMD | presents | stereo;
    constexpr auto def = LayoutOption::Default;
    constexpr auto large = LayoutOption::LargeScreen;
    constexpr auto single = LayoutOption::SingleScreen;
    constexpr auto off = StereoRenderOption::Off;
    constexpr auto anaglyph = StereoRenderOption::Anaglyph;
    // Prominent 3DS Screen.
    constexpr bool top = false;
    constexpr bool bottom = true;

    REQUIRE(SelectMode(true, def, off, top, presents, true) == pair_2d);
    REQUIRE(SelectMode(true, def, off, top, presents | stereo, true) == pair_stereo);
    // The software renderer draws one eye.
    REQUIRE(SelectMode(true, def, off, top, presents | stereo, false) == pair_2d);
    REQUIRE(SelectMode(true, def, off, top, stereo, true) == own);
    REQUIRE(SelectMode(true, def, off, top, 0, true) == own);
    // Swapped screens still pair up.
    REQUIRE(SelectMode(true, def, off, bottom, presents, true) == pair_2d);
    REQUIRE(SelectMode(true, def, off, bottom, presents | stereo, true) == pair_stereo);

    // In 2D a layout or 3D mode of the user's own stays; the frontend's stereo takes over.
    REQUIRE(SelectMode(true, large, off, top, presents, true) == own);
    REQUIRE(SelectMode(true, large, off, top, presents | stereo, true) == pair_stereo);
    REQUIRE(SelectMode(true, def, anaglyph, top, presents, true) == own);
    REQUIRE(SelectMode(true, def, anaglyph, top, presents | stereo, true) == pair_stereo);
    REQUIRE(SelectMode(true, large, off, top, presents | stereo, false) == pair_2d);
    REQUIRE(SelectMode(true, large, off, top, 0, true) == own);

    // Single Screen Only sends the prominent screen alone, in 2D as in stereo.
    REQUIRE(SelectMode(true, single, off, top, presents, true) == single_2d);
    REQUIRE(SelectMode(true, single, off, bottom, presents, true) == single_2d);
    REQUIRE(SelectMode(true, single, off, top, presents | stereo, true) == single_stereo);
    REQUIRE(SelectMode(true, single, off, top, presents | stereo, false) == single_2d);
    REQUIRE(SelectMode(true, single, anaglyph, top, presents, true) == own);
    REQUIRE(SelectMode(true, single, anaglyph, top, presents | stereo, true) == single_stereo);
    REQUIRE(SelectMode(true, single, off, top, 0, true) == own);
    // Only the top screen has 3D, so the bottom screen alone is drawn once.
    REQUIRE(SelectMode(true, single, off, bottom, presents | stereo, true) == single_2d);

    // A headset shows both eyes, even from a frontend whose window shows no views.
    REQUIRE(SelectMode(true, large, anaglyph, top, headset, true) == pair_stereo);
    REQUIRE(SelectMode(true, large, anaglyph, top, headset, false) == pair_2d);
    REQUIRE(SelectMode(true, single, anaglyph, top, headset, true) == single_stereo);
    REQUIRE(SelectMode(true, single, anaglyph, bottom, headset, true) == single_2d);

    // "Frontend Layout and 3D" is Off.
    for (const auto layout : {def, large, single}) {
        for (const auto mode_3d : {off, anaglyph}) {
            for (const bool swapped : {top, bottom}) {
                for (const unsigned status : {0u, presents, presents | stereo, headset}) {
                    REQUIRE(SelectMode(false, layout, mode_3d, swapped, status, true) == own);
                }
            }
        }
    }
}

TEST_CASE("VideoViews::PackedSize", "[libretro]") {
    using LibRetro::VideoViews::PackedSize;
    using Size = std::pair<u32, u32>;
    REQUIRE(PackedSize(pair_2d, false, 1) == Size{400, 480});
    REQUIRE(PackedSize(pair_stereo, false, 1) == Size{800, 480});
    REQUIRE(PackedSize(pair_2d, false, 3) == Size{1200, 1440});
    REQUIRE(PackedSize(pair_stereo, false, 10) == Size{8000, 4800});
    // Swapped screens pack the same frame.
    REQUIRE(PackedSize(pair_2d, true, 1) == Size{400, 480});
    REQUIRE(PackedSize(pair_stereo, true, 1) == Size{800, 480});

    // Single Screen Only packs the prominent screen alone.
    REQUIRE(PackedSize(single_2d, false, 1) == Size{400, 240});
    REQUIRE(PackedSize(single_stereo, false, 1) == Size{800, 240});
    REQUIRE(PackedSize(single_2d, false, 3) == Size{1200, 720});
    REQUIRE(PackedSize(single_2d, true, 1) == Size{320, 240});
    REQUIRE(PackedSize(single_2d, true, 3) == Size{960, 720});
}

TEST_CASE("The 2D map follows Azahar's default layout", "[libretro]") {
    Common::Log::DisableLoggingInTests();
    SettingsGuard guard;
    RequireViews(MapFor(pair_2d, false, 1),
                 {{0, 0, 400, 240, 0, EYE_NONE}, {40, 240, 320, 240, 1, EYE_NONE}});
    RequireViews(MapFor(pair_2d, false, 2),
                 {{0, 0, 800, 480, 0, EYE_NONE}, {80, 480, 640, 480, 1, EYE_NONE}});
}

TEST_CASE("The stereo map follows the full side-by-side draw", "[libretro]") {
    Common::Log::DisableLoggingInTests();
    SettingsGuard guard;
    RequireViews(MapFor(pair_stereo, false, 1), {{0, 0, 400, 240, 0, EYE_LEFT},
                                                 {400, 0, 400, 240, 0, EYE_RIGHT},
                                                 {40, 240, 320, 240, 1, EYE_NONE}});
    RequireViews(MapFor(pair_stereo, false, 3), {{0, 0, 1200, 720, 0, EYE_LEFT},
                                                 {1200, 0, 1200, 720, 0, EYE_RIGHT},
                                                 {120, 720, 960, 720, 1, EYE_NONE}});
}

TEST_CASE("Swapped screens put the bottom screen first", "[libretro]") {
    Common::Log::DisableLoggingInTests();
    SettingsGuard guard;
    RequireViews(MapFor(pair_2d, true, 1),
                 {{0, 240, 400, 240, 1, EYE_NONE}, {40, 0, 320, 240, 0, EYE_NONE}});
    RequireViews(MapFor(pair_stereo, true, 1), {{0, 240, 400, 240, 1, EYE_LEFT},
                                                {400, 240, 400, 240, 1, EYE_RIGHT},
                                                {40, 0, 320, 240, 0, EYE_NONE}});
}

TEST_CASE("Single Screen Only maps the prominent screen alone", "[libretro]") {
    Common::Log::DisableLoggingInTests();
    SettingsGuard guard;
    RequireViews(MapFor(single_2d, false, 1), {{0, 0, 400, 240, 0, EYE_NONE}});
    RequireViews(MapFor(single_2d, false, 2), {{0, 0, 800, 480, 0, EYE_NONE}});
    RequireViews(MapFor(single_2d, true, 1), {{0, 0, 320, 240, 0, EYE_NONE}});
    RequireViews(MapFor(single_stereo, false, 1),
                 {{0, 0, 400, 240, 0, EYE_LEFT}, {400, 0, 400, 240, 0, EYE_RIGHT}});
}

TEST_CASE("Views mode overrides Azahar's layout and gates the 3D slider", "[libretro]") {
    Common::Log::DisableLoggingInTests();
    SettingsGuard guard;
    LibRetro::settings.layout_option = Settings::LayoutOption::LargeScreen;
    LibRetro::settings.render_3d = Settings::StereoRenderOption::Anaglyph;
    LibRetro::settings.factor_3d = 50;

    // Azahar's own layout and 3D mode.
    LibRetro::VideoViews::SetCurrentMode(Mode{});
    LibRetro::ApplyLayoutSettings();
    REQUIRE(Settings::values.layout_option.GetValue() == Settings::LayoutOption::LargeScreen);
    REQUIRE(Settings::values.render_3d.GetValue() == Settings::StereoRenderOption::Anaglyph);
    REQUIRE(Settings::values.render_3d_which_display.GetValue() ==
            Settings::StereoWhichDisplay::Both);
    REQUIRE(Settings::values.factor_3d.GetValue() == 50u);

    // Views in 2D: one eye is drawn, so the slider is down.
    LibRetro::VideoViews::SetCurrentMode(pair_2d);
    LibRetro::ApplyLayoutSettings();
    REQUIRE(Settings::values.layout_option.GetValue() == Settings::LayoutOption::Default);
    REQUIRE(Settings::values.render_3d.GetValue() == Settings::StereoRenderOption::Off);
    REQUIRE(Settings::values.render_3d_which_display.GetValue() ==
            Settings::StereoWhichDisplay::None);
    REQUIRE(Settings::values.factor_3d.GetValue() == 0u);

    // Views in stereo: the full side-by-side draw, slider at Depth.
    LibRetro::VideoViews::SetCurrentMode(pair_stereo);
    LibRetro::ApplyLayoutSettings();
    REQUIRE(Settings::values.layout_option.GetValue() == Settings::LayoutOption::Default);
    REQUIRE(Settings::values.render_3d.GetValue() == Settings::StereoRenderOption::SideBySideFull);
    REQUIRE(Settings::values.render_3d_which_display.GetValue() ==
            Settings::StereoWhichDisplay::Both);
    REQUIRE(Settings::values.factor_3d.GetValue() == 50u);

    // Single Screen Only packs one screen, in stereo or in 2D.
    LibRetro::VideoViews::SetCurrentMode(single_stereo);
    LibRetro::ApplyLayoutSettings();
    REQUIRE(Settings::values.layout_option.GetValue() == Settings::LayoutOption::SingleScreen);
    REQUIRE(Settings::values.render_3d.GetValue() == Settings::StereoRenderOption::SideBySideFull);
    REQUIRE(Settings::values.factor_3d.GetValue() == 50u);
    LibRetro::VideoViews::SetCurrentMode(single_2d);
    LibRetro::ApplyLayoutSettings();
    REQUIRE(Settings::values.layout_option.GetValue() == Settings::LayoutOption::SingleScreen);
    REQUIRE(Settings::values.render_3d.GetValue() == Settings::StereoRenderOption::Off);
    REQUIRE(Settings::values.factor_3d.GetValue() == 0u);

    // Azahar's own 3D mode Off: the slider is down.
    LibRetro::settings.render_3d = Settings::StereoRenderOption::Off;
    LibRetro::VideoViews::SetCurrentMode(Mode{});
    LibRetro::ApplyLayoutSettings();
    REQUIRE(Settings::values.render_3d.GetValue() == Settings::StereoRenderOption::Off);
    REQUIRE(Settings::values.render_3d_which_display.GetValue() ==
            Settings::StereoWhichDisplay::None);
    REQUIRE(Settings::values.factor_3d.GetValue() == 0u);
}

TEST_CASE("Views geometry and frame limits", "[libretro]") {
    Common::Log::DisableLoggingInTests();
    SettingsGuard guard;
    Settings::values.layout_option = Settings::LayoutOption::Default;
    retro_system_av_info info{};

    Settings::values.swap_screen = false;

    LibRetro::VideoViews::SetCurrentMode(pair_stereo);
    Settings::values.resolution_factor = 10;
    retro_get_system_av_info(&info);
    REQUIRE(info.geometry.base_width == 8000u);
    REQUIRE(info.geometry.base_height == 4800u);
    REQUIRE(info.geometry.max_width >= info.geometry.base_width);
    REQUIRE(info.geometry.max_height >= info.geometry.base_height);

    LibRetro::VideoViews::SetCurrentMode(pair_2d);
    Settings::values.resolution_factor = 1;
    retro_get_system_av_info(&info);
    REQUIRE(info.geometry.base_width == 400u);
    REQUIRE(info.geometry.base_height == 480u);
    REQUIRE(ComputeLayoutGeometry().emulated_pointer);

    // Single Screen Only: the prominent screen alone. The top screen has no touch screen to
    // point at.
    LibRetro::VideoViews::SetCurrentMode(single_stereo);
    retro_get_system_av_info(&info);
    REQUIRE(info.geometry.base_width == 800u);
    REQUIRE(info.geometry.base_height == 240u);
    REQUIRE_FALSE(ComputeLayoutGeometry().emulated_pointer);
    Settings::values.swap_screen = true;
    LibRetro::VideoViews::SetCurrentMode(single_2d);
    retro_get_system_av_info(&info);
    REQUIRE(info.geometry.base_width == 320u);
    REQUIRE(info.geometry.base_height == 240u);
    REQUIRE(ComputeLayoutGeometry().emulated_pointer);

    // Outside views mode Azahar's own layout sizes the frame.
    LibRetro::VideoViews::SetCurrentMode(Mode{});
    Settings::values.layout_option = Settings::LayoutOption::SideScreen;
    retro_get_system_av_info(&info);
    REQUIRE(info.geometry.base_width == 720u);
    REQUIRE(info.geometry.base_height == 240u);
}

TEST_CASE("Stereo views hide the Layout options they override", "[libretro]") {
    using LibRetro::LayoutOptionsDisplay;
    constexpr unsigned presents = RETRO_VIDEO_VIEWS_STATUS_PRESENTS;
    constexpr unsigned stereo = RETRO_VIDEO_VIEWS_STATUS_STEREO;
    // Prominent 3DS Screen, Screen Swap Mode and Stereoscopic 3D Depth still apply, and Screen
    // Layout picks whether one screen is sent or both.
    const std::set<std::string> overridden{"citra_large_screen_proportion", "citra_render_3d"};

    REQUIRE(Keys(LayoutOptionsDisplay(true, presents | stereo), false) == overridden);
    REQUIRE(Keys(LayoutOptionsDisplay(true, presents | stereo), true).empty());
    REQUIRE(Keys(LayoutOptionsDisplay(true, RETRO_VIDEO_VIEWS_STATUS_HMD | presents | stereo),
                 false) == overridden);

    // In the frontend's 2D mode, Screen Layout and Stereoscopic 3D Mode decide whether views
    // are used.
    REQUIRE(Keys(LayoutOptionsDisplay(true, presents), true) == overridden);
    REQUIRE(Keys(LayoutOptionsDisplay(true, stereo), true) == overridden);
    REQUIRE(Keys(LayoutOptionsDisplay(true, 0), true) == overridden);

    // "Frontend Layout and 3D" is Off.
    for (const unsigned status : {0u, presents, presents | stereo}) {
        REQUIRE(Keys(LayoutOptionsDisplay(false, status), true) == overridden);
    }
}

TEST_CASE("Option visibility reaches the frontend", "[libretro]") {
    Common::Log::DisableLoggingInTests();
    constexpr unsigned presents = RETRO_VIDEO_VIEWS_STATUS_PRESENTS;
    constexpr unsigned stereo = RETRO_VIDEO_VIEWS_STATUS_STEREO;
    const auto hidden = LayoutDisplay(false);
    const auto shown = LayoutDisplay(true);
    const auto& display = FakeFrontend::display;
    FakeFrontend::views_status = presents | stereo;
    FakeFrontend::frontend_layout = "auto";

    retro_set_environment(FakeFrontend::Environment);
    REQUIRE(FakeFrontend::update_display != nullptr);
    REQUIRE(FakeFrontend::update_display());
    REQUIRE(display == hidden);
    REQUIRE_FALSE(FakeFrontend::update_display());

    // The frontend asks after the option changes, before the core reads it.
    FakeFrontend::frontend_layout = "off";
    REQUIRE(FakeFrontend::update_display());
    REQUIRE(display == shown);

    // Each run follows the views status.
    REQUIRE(LibRetro::UpdateLayoutOptionsDisplay(true, presents | stereo));
    REQUIRE(display == hidden);
    REQUIRE_FALSE(LibRetro::UpdateLayoutOptionsDisplay(true, presents | stereo));
    REQUIRE(LibRetro::UpdateLayoutOptionsDisplay(true, presents));
    REQUIRE(display == shown);

    // New core options start visible.
    REQUIRE(LibRetro::UpdateLayoutOptionsDisplay(true, presents | stereo));
    FakeFrontend::frontend_layout = "auto";
    retro_set_environment(FakeFrontend::Environment);
    REQUIRE(display.empty());
    REQUIRE(FakeFrontend::update_display());
    REQUIRE(display == hidden);

    // RetroArch sets a running core's environment again to read its system info, ignoring
    // what the core asks for; its options stay as they were.
    FakeFrontend::ignore_calls = true;
    retro_set_environment(FakeFrontend::Environment);
    FakeFrontend::ignore_calls = false;
    FakeFrontend::frontend_layout = "off";
    REQUIRE(FakeFrontend::update_display());
    REQUIRE(display == shown);
}

TEST_CASE("Stereoscopic 3D Depth starts halfway", "[libretro]") {
    Common::Log::DisableLoggingInTests();
    retro_set_environment(FakeFrontend::Environment);
    REQUIRE(FakeFrontend::options != nullptr);

    const retro_core_option_v2_definition* depth = nullptr;
    for (auto* option = FakeFrontend::options->definitions; option->key; ++option) {
        if (std::string(option->key) == "citra_factor_3d") {
            depth = option;
        }
    }
    REQUIRE(depth != nullptr);
    REQUIRE(std::string(depth->default_value) == "50");
}
