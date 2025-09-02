// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cmath>

#include "common/assert.h"
#include "common/settings.h"
#include "core/3ds.h"
#include "core/frontend/framebuffer_layout.h"

namespace Layout {

static constexpr float TOP_SCREEN_ASPECT_RATIO =
    static_cast<float>(Core::kScreenTopHeight) / Core::kScreenTopWidth;
static constexpr float BOT_SCREEN_ASPECT_RATIO =
    static_cast<float>(Core::kScreenBottomHeight) / Core::kScreenBottomWidth;

u32 FramebufferLayout::GetScalingRatio() const {
    int core_width = screens[0].is_bottom ? Core::kScreenBottomWidth : Core::kScreenTopWidth;
    int core_height = screens[0].is_bottom ? Core::kScreenBottomHeight : Core::kScreenTopHeight;
    if (orientation == DisplayOrientation::Landscape ||
        orientation == DisplayOrientation::LandscapeFlipped) {

        return static_cast<u32>(((screens[0].rect.GetWidth() - 1) / core_width) + 1);
    } else {
        return static_cast<u32>(((screens[0].rect.GetWidth() - 1) / core_height) + 1);
    }
}

// Finds the largest size subrectangle contained in window area that is confined to the aspect ratio
template <class T>
static Common::Rectangle<T> MaxRectangle(Common::Rectangle<T> window_area,
                                         float window_aspect_ratio) {
    float scale = std::min(static_cast<float>(window_area.GetWidth()),
                           window_area.GetHeight() / window_aspect_ratio);
    return Common::Rectangle<T>{0, 0, static_cast<T>(std::round(scale)),
                                static_cast<T>(std::round(scale * window_aspect_ratio))};
}

FramebufferLayout CreateLayout(Settings::LayoutOption layout_option, u32 width, u32 height,
                               bool swapped, bool upright, Settings::StereoRenderOption render_3d,
                               bool swap_eyes) {
    ASSERT(width > 0);
    ASSERT(height > 0);
    if (render_3d == Settings::StereoRenderOption::SideBySideFull) {
        width /= 2;
    }
    if (upright) {
        std::swap(width, height);
    }

    FramebufferLayout res;
    switch (layout_option) {
    case Settings::LayoutOption::SingleScreen:
#ifndef ANDROID
    case Settings::LayoutOption::SeparateWindows: // should not happen, emuwindow should handle
#endif
    {
        res = SingleFrameLayout(width, height, swapped, swap_eyes);
    }
    case Settings::LayoutOption::SideScreen: {
        res = LargeFrameLayout(width, height, swapped, 1.0f,
                               Settings::SmallScreenPosition::MiddleRight, swap_eyes);
        break;
    }
    case Settings::LayoutOption::LargeScreen: {
        res = LargeFrameLayout(width, height, swapped,
                               Settings::values.large_screen_proportion.GetValue(),
                               Settings::values.small_screen_position.GetValue(), swap_eyes);
        break;
    }
    case Settings::LayoutOption::HybridScreen: {
        res = HybridScreenLayout(width, height, swapped, swap_eyes);
        break;
    }
    case Settings::LayoutOption::CustomLayout: {
        res = CustomFrameLayout(width, height, swapped, false, swap_eyes);
        break;
    }
    case Settings::LayoutOption::Default:
    default: {
        res = LargeFrameLayout(width, height, swapped, 1.0f,
                               Settings::SmallScreenPosition::BelowLarge, swap_eyes);
        break;
    }
    }
    res.render_3d_mode = render_3d;
    if (upright) {
        res.orientation = DisplayOrientation::Portrait;
        res = reverseLayout(res);
    }

    if (render_3d == Settings::StereoRenderOption::SideBySideFull) {
        res.width *= 2;
        res = ApplyFullStereo(res, swap_eyes);
    } else if (render_3d == Settings::StereoRenderOption::SideBySide) {
        res = ApplyHalfStereo(res, swap_eyes);
    }
    return res;
}

FramebufferLayout CreateMobileSecondaryLayout(Settings::SecondaryDisplayLayout layout_option,
                                              u32 width, u32 height, bool swapped, bool upright,
                                              Settings::StereoRenderOption render_3d,
                                              bool swap_eyes) {
    ASSERT(width > 0);
    ASSERT(height > 0);
    if (render_3d == Settings::StereoRenderOption::SideBySideFull) {
        width /= 2;
    }
    if (upright) {
        std::swap(width, height);
    }

    FramebufferLayout res;
    switch (layout_option) {

    case Settings::SecondaryDisplayLayout::SideBySide: {
        res = LargeFrameLayout(width, height, swapped, 1.0f,
                               Settings::SmallScreenPosition::MiddleRight, swap_eyes);
        break;
    }
    case Settings::SecondaryDisplayLayout::BottomScreenOnly: {
        res = SingleFrameLayout(width, height, true, swap_eyes);
        break;
    }
    case Settings::SecondaryDisplayLayout::None: // this should not happen
    case Settings::SecondaryDisplayLayout::TopScreenOnly:
    default: {
        res = SingleFrameLayout(width, height, false, swap_eyes);
        break;
    }
    }
    res.render_3d_mode = render_3d;
    if (upright) {
        res.orientation = DisplayOrientation::Portrait;
        res = reverseLayout(res);
    }

    if (render_3d == Settings::StereoRenderOption::SideBySideFull) {
        res.width *= 2;
        res = ApplyFullStereo(res, swap_eyes);
    } else if (render_3d == Settings::StereoRenderOption::SideBySide) {
        res = ApplyHalfStereo(res, swap_eyes);
    }
    return res;
}

FramebufferLayout CreatePortraitLayout(Settings::PortraitLayoutOption layout_option, u32 width,
                                       u32 height, bool swapped, bool upright,
                                       Settings::StereoRenderOption render_3d, bool swap_eyes) {
    ASSERT(width > 0);
    ASSERT(height > 0);
    if (upright) {
        std::swap(width, height);
    }
    FramebufferLayout res;
    switch (layout_option) {
    case Settings::PortraitLayoutOption::PortraitTopFullWidth: {
        const float scale_factor = swapped ? 1.25f : 0.8f;
        FramebufferLayout res =
            LargeFrameLayout(width, height, swapped, scale_factor,
                             Settings::SmallScreenPosition::BelowLarge, swap_eyes);
        const int shiftY = res.screens[0].rect.top;
        res.screens[0].rect = res.screens[0].rect.TranslateY(shiftY);
        res.screens[1].rect = res.screens[1].rect.TranslateY(shiftY);
        break;
    }
    case Settings::PortraitLayoutOption::PortraitCustomLayout: {
        res = CustomFrameLayout(width, height, swapped, true, swap_eyes);
        break;
    }
    case Settings::PortraitLayoutOption::PortraitOriginal:
    default: {
        const float scale_factor = 1;
        FramebufferLayout res =
            LargeFrameLayout(width, height, swapped, scale_factor,
                             Settings::SmallScreenPosition::BelowLarge, swap_eyes);
        const int shiftY = res.screens[0].rect.top;
        res.screens[0].rect = res.screens[0].rect.TranslateY(shiftY);
        res.screens[1].rect = res.screens[1].rect.TranslateY(shiftY);
        break;
    }
    }
    res.render_3d_mode = render_3d;
    if (upright) {
        res.orientation = DisplayOrientation::Portrait;
        res = reverseLayout(res);
    }

    if (render_3d == Settings::StereoRenderOption::SideBySideFull) {
        res.width *= 2;
        res = ApplyFullStereo(res, swap_eyes);
    } else if (render_3d == Settings::StereoRenderOption::SideBySide) {
        res = ApplyHalfStereo(res, swap_eyes);
    }
    return res;
}

FramebufferLayout SingleFrameLayout(u32 width, u32 height, bool is_bottom, bool upright,
                                    bool render_stereo, bool swap_eyes) {

    FramebufferLayout res{width, height, {}};
    if (!render_stereo)
        res.render_3d_mode = Settings::StereoRenderOption::Off;

    Common::Rectangle<u32> screen_window_area{0, 0, width, height};
    Common::Rectangle<u32> top_screen;
    Common::Rectangle<u32> bot_screen;

    // TODO: This is kind of gross, make it platform agnostic. -OS
#ifdef ANDROID
    const float window_aspect_ratio = static_cast<float>(height) / width;
    const auto aspect_ratio_setting = Settings::values.aspect_ratio.GetValue();

    float emulation_aspect_ratio = (is_bottom) ? BOT_SCREEN_ASPECT_RATIO : TOP_SCREEN_ASPECT_RATIO;
    switch (aspect_ratio_setting) {
    case Settings::AspectRatio::Default:
        break;
    case Settings::AspectRatio::Stretch:
        emulation_aspect_ratio = window_aspect_ratio;
        break;
    default:
        emulation_aspect_ratio = res.GetAspectRatioValue(aspect_ratio_setting);
    }

    top_screen = MaxRectangle(screen_window_area, emulation_aspect_ratio);
    bot_screen = MaxRectangle(screen_window_area, emulation_aspect_ratio);

    if (window_aspect_ratio < emulation_aspect_ratio) {
        top_screen =
            top_screen.TranslateX((screen_window_area.GetWidth() - top_screen.GetWidth()) / 2);
        bot_screen =
            bot_screen.TranslateX((screen_window_area.GetWidth() - bot_screen.GetWidth()) / 2);
    } else {
        top_screen = top_screen.TranslateY((height - top_screen.GetHeight()) / 2);
        bot_screen = bot_screen.TranslateY((height - bot_screen.GetHeight()) / 2);
    }
#else
    top_screen = MaxRectangle(screen_window_area, TOP_SCREEN_ASPECT_RATIO);
    bot_screen = MaxRectangle(screen_window_area, BOT_SCREEN_ASPECT_RATIO);

    const bool stretched = (Settings::values.screen_top_stretch.GetValue() && !is_bottom) ||
                           (Settings::values.screen_bottom_stretch.GetValue() && is_bottom);
    if (stretched) {
        top_screen = {Settings::values.screen_top_leftright_padding.GetValue(),
                      Settings::values.screen_top_topbottom_padding.GetValue(),
                      width - Settings::values.screen_top_leftright_padding.GetValue(),
                      height - Settings::values.screen_top_topbottom_padding.GetValue()};
        bot_screen = {Settings::values.screen_bottom_leftright_padding.GetValue(),
                      Settings::values.screen_bottom_topbottom_padding.GetValue(),
                      width - Settings::values.screen_bottom_leftright_padding.GetValue(),
                      height - Settings::values.screen_bottom_topbottom_padding.GetValue()};
    } else {
        top_screen = top_screen.TranslateX((width - top_screen.GetWidth()) / 2)
                         .TranslateY((height - top_screen.GetHeight()) / 2);
        bot_screen = bot_screen.TranslateX((width - bot_screen.GetWidth()) / 2)
                         .TranslateY((height - bot_screen.GetHeight()) / 2);
    }
#endif
    Screen top{top_screen, false, swap_eyes, true};
    Screen bot{bot_screen, true, swap_eyes, true};
    if (is_bottom) {
        res.screens.push_back(bot);
    } else {
        res.screens.push_back(top);
    }
    return res;
}

FramebufferLayout LargeFrameLayout(u32 width, u32 height, bool swapped, bool upright,
                                   float scale_factor,
                                   Settings::SmallScreenPosition small_screen_position,
                                   bool render_stereo, bool swap_eyes) {

    const bool vertical = (small_screen_position == Settings::SmallScreenPosition::AboveLarge ||
                           small_screen_position == Settings::SmallScreenPosition::BelowLarge);
    FramebufferLayout res{width, height, {}};
    if (!render_stereo)
        res.render_3d_mode = Settings::StereoRenderOption::Off;

    if (upright)
        res.orientation = DisplayOrientation::Portrait;
    // Split the window into two parts. Give proportional width to the smaller screen
    // To do that, find the total emulation box and maximize that based on window size
    u32 gap = (u32)(Settings::values.screen_gap.GetValue() * scale_factor);

    float large_height =
        swapped ? Core::kScreenBottomHeight * scale_factor : Core::kScreenTopHeight * scale_factor;
    float small_height =
        static_cast<float>(swapped ? Core::kScreenTopHeight : Core::kScreenBottomHeight);
    float large_width =
        swapped ? Core::kScreenBottomWidth * scale_factor : Core::kScreenTopWidth * scale_factor;
    float small_width =
        static_cast<float>(swapped ? Core::kScreenTopWidth : Core::kScreenBottomWidth);

    float emulation_width;
    float emulation_height;
    if (vertical) {
        // width is just the larger size at this point
        emulation_width = std::max(large_width, small_width);
        emulation_height = large_height + small_height + gap;
    } else {
        emulation_width = large_width + small_width + gap;
        emulation_height = std::max(large_height, small_height);
    }

    const float window_aspect_ratio = static_cast<float>(height) / static_cast<float>(width);
    const float emulation_aspect_ratio = emulation_height / emulation_width;

    Common::Rectangle<u32> screen_window_area{0, 0, width, height};
    Common::Rectangle<u32> total_rect = MaxRectangle(screen_window_area, emulation_aspect_ratio);
    // TODO: Wtf does this `scale_amount` value represent? -OS
    const float scale_amount = static_cast<float>(total_rect.GetHeight()) / emulation_height;
    gap = static_cast<u32>(static_cast<float>(gap) * scale_amount);

    Common::Rectangle<u32> large_screen =
        Common::Rectangle<u32>{total_rect.left, total_rect.top,
                               static_cast<u32>(large_width * scale_amount + total_rect.left),
                               static_cast<u32>(large_height * scale_amount + total_rect.top)};
    Common::Rectangle<u32> small_screen =
        Common::Rectangle<u32>{total_rect.left, total_rect.top,
                               static_cast<u32>(small_width * scale_amount + total_rect.left),
                               static_cast<u32>(small_height * scale_amount + total_rect.top)};

    if (window_aspect_ratio < emulation_aspect_ratio) {
        // shift the large screen so it is at the left position of the bounding rectangle
        large_screen = large_screen.TranslateX((width - total_rect.GetWidth()) / 2);
    } else {
        // shift the large screen so it is at the top position of the bounding rectangle
        large_screen = large_screen.TranslateY((height - total_rect.GetHeight()) / 2);
    }

    switch (small_screen_position) {
    case Settings::SmallScreenPosition::TopRight:
        // Shift the small screen to the top right corner
        small_screen = small_screen.TranslateX(large_screen.right + gap);
        small_screen = small_screen.TranslateY(large_screen.top);
        break;
    case Settings::SmallScreenPosition::MiddleRight:
        // Shift the small screen to the center right
        small_screen = small_screen.TranslateX(large_screen.right + gap);
        small_screen = small_screen.TranslateY(
            ((large_screen.GetHeight() - small_screen.GetHeight()) / 2) + large_screen.top);
        break;
    case Settings::SmallScreenPosition::BottomRight:
        // Shift the small screen to the bottom right corner
        small_screen = small_screen.TranslateX(large_screen.right + gap);
        small_screen = small_screen.TranslateY(large_screen.bottom - small_screen.GetHeight());
        break;
    case Settings::SmallScreenPosition::TopLeft:
        // shift the small screen to the upper left then shift the large screen to its right
        small_screen = small_screen.TranslateX(large_screen.left);
        large_screen = large_screen.TranslateX(small_screen.GetWidth() + gap);
        small_screen = small_screen.TranslateY(large_screen.top);
        break;
    case Settings::SmallScreenPosition::MiddleLeft:
        // shift the small screen to the middle left and shift the large screen to its right
        small_screen = small_screen.TranslateX(large_screen.left);
        large_screen = large_screen.TranslateX(small_screen.GetWidth() + gap);
        small_screen = small_screen.TranslateY(
            ((large_screen.GetHeight() - small_screen.GetHeight()) / 2) + large_screen.top);
        break;
    case Settings::SmallScreenPosition::BottomLeft:
        // shift the small screen to the bottom left and shift the large screen to its right
        small_screen = small_screen.TranslateX(large_screen.left);
        large_screen = large_screen.TranslateX(small_screen.GetWidth() + gap);
        small_screen = small_screen.TranslateY(large_screen.bottom - small_screen.GetHeight());
        break;
    case Settings::SmallScreenPosition::AboveLarge:
        // shift the large screen down and the bottom screen above it
        small_screen = small_screen.TranslateY(large_screen.top);
        large_screen = large_screen.TranslateY(small_screen.GetHeight() + gap);
        // If the "large screen" is actually smaller, center it
        if (large_screen.GetWidth() < total_rect.GetWidth()) {
            large_screen =
                large_screen.TranslateX((total_rect.GetWidth() - large_screen.GetWidth()) / 2);
        }
        small_screen = small_screen.TranslateX(large_screen.left + large_screen.GetWidth() / 2 -
                                               small_screen.GetWidth() / 2);
        break;
    case Settings::SmallScreenPosition::BelowLarge:
        // shift the bottom_screen down and then over to the center
        // If the "large screen" is actually smaller, center it
        if (large_screen.GetWidth() < total_rect.GetWidth()) {
            large_screen =
                large_screen.TranslateX((total_rect.GetWidth() - large_screen.GetWidth()) / 2);
        }
        small_screen = small_screen.TranslateY(large_screen.bottom + gap);
        small_screen = small_screen.TranslateX(large_screen.left + large_screen.GetWidth() / 2 -
                                               small_screen.GetWidth() / 2);
        break;
    default:
        UNREACHABLE();
        break;
    }
    Screen large = Screen{large_screen, swapped, swap_eyes, true};
    Screen small = Screen{small_screen, !swapped, swap_eyes, true};
    res.screens.push_back(large);
    res.screens.push_back(small);
    return res;
}

FramebufferLayout HybridScreenLayout(u32 width, u32 height, bool swapped, bool swap_eyes) {
    FramebufferLayout res =
        LargeFrameLayout(width, height, swapped, 2.25f, Settings::SmallScreenPosition::TopRight);

    // screens[0] is the large screen, screens[1] is the small screen. Add the other small screen.
    Screen small_screen =
        Screen{Common::Rectangle<u32>{res.screens[1].rect.left, res.screens[1].rect.bottom,
                                      res.screens[1].rect.right, res.screens[0].rect.bottom},
               res.screens[0].is_bottom, swap_eyes, true};
    res.screens.push_back(small_screen);
    return res;
}

FramebufferLayout CustomFrameLayout(u32 width, u32 height, bool is_swapped, bool is_portrait_mode,
                                    bool swap_eyes) {

    FramebufferLayout res{width, height, {}};
    const u16 top_x = is_portrait_mode ? Settings::values.custom_portrait_top_x.GetValue()
                                       : Settings::values.custom_top_x.GetValue();
    const u16 top_width = is_portrait_mode ? Settings::values.custom_portrait_top_width.GetValue()
                                           : Settings::values.custom_top_width.GetValue();
    const u16 top_y = is_portrait_mode ? Settings::values.custom_portrait_top_y.GetValue()
                                       : Settings::values.custom_top_y.GetValue();
    const u16 top_height = is_portrait_mode ? Settings::values.custom_portrait_top_height.GetValue()
                                            : Settings::values.custom_top_height.GetValue();
    const u16 bottom_x = is_portrait_mode ? Settings::values.custom_portrait_bottom_x.GetValue()
                                          : Settings::values.custom_bottom_x.GetValue();
    const u16 bottom_width = is_portrait_mode
                                 ? Settings::values.custom_portrait_bottom_width.GetValue()
                                 : Settings::values.custom_bottom_width.GetValue();
    const u16 bottom_y = is_portrait_mode ? Settings::values.custom_portrait_bottom_y.GetValue()
                                          : Settings::values.custom_bottom_y.GetValue();
    const u16 bottom_height = is_portrait_mode
                                  ? Settings::values.custom_portrait_bottom_height.GetValue()
                                  : Settings::values.custom_bottom_height.GetValue();

    Common::Rectangle<u32> top_screen{top_x, top_y, (u32)(top_x + top_width),
                                      (u32)(top_y + top_height)};
    Common::Rectangle<u32> bot_screen{bottom_x, bottom_y, (u32)(bottom_x + bottom_width),
                                      (u32)(bottom_y + bottom_height)};
    if (is_swapped) {
        res.screens.push_back(Screen{bot_screen, true, swap_eyes, true});
        res.screens.push_back(Screen{top_screen, false, swap_eyes, true});
    } else {
        res.screens.push_back(Screen{top_screen, false, swap_eyes, true});
        res.screens.push_back(Screen{bot_screen, true, swap_eyes, true});
    }

    return res;
}

FramebufferLayout FrameLayoutFromResolutionScale(u32 res_scale, bool is_secondary,
                                                 bool is_portrait) {
    const std::pair<unsigned, unsigned> min_size =
        is_portrait ? GetMinimumSizeFromPortraitLayout()
                    : GetMinimumSizeFromLayout(Settings::values.layout_option.GetValue());
    u32 width = min_size.first * res_scale;
    u32 height = min_size.second * res_scale;
    const bool swapped = Settings::values.swap_screen.GetValue();
    const bool upright = Settings::values.upright_screen.GetValue();
    const Settings::StereoRenderOption render_3d = Settings::values.render_3d.GetValue();

    FramebufferLayout layout;
    if (is_portrait) {
        auto layout_option = Settings::values.portrait_layout_option.GetValue();
        if (layout_option == Settings::PortraitLayoutOption::PortraitCustomLayout) {
            u32 leftMost = std::min(Settings::values.custom_portrait_top_x.GetValue(),
                                    Settings::values.custom_portrait_bottom_x.GetValue());
            u32 topMost = std::min(Settings::values.custom_portrait_top_y.GetValue(),
                                   Settings::values.custom_portrait_bottom_y.GetValue());
            u32 rightMost = std::max(Settings::values.custom_portrait_top_x.GetValue() +
                                         Settings::values.custom_portrait_top_width.GetValue(),
                                     Settings::values.custom_portrait_bottom_x.GetValue() +
                                         Settings::values.custom_portrait_bottom_width.GetValue());
            u32 bottomMost =
                std::max(Settings::values.custom_portrait_top_y.GetValue() +
                             Settings::values.custom_portrait_top_height.GetValue(),
                         Settings::values.custom_portrait_bottom_y.GetValue() +
                             Settings::values.custom_portrait_bottom_height.GetValue());
            width = rightMost - leftMost;
            height = bottomMost - topMost;
        }
        layout =
            CreatePortraitLayout(layout_option, width, height, swapped, upright, render_3d, false);
    } else {
        auto layout_option = Settings::values.layout_option.GetValue();
        if (layout_option == Settings::LayoutOption::CustomLayout) {
            u32 leftMost = std::min(Settings::values.custom_portrait_top_x.GetValue(),
                                    Settings::values.custom_portrait_bottom_x.GetValue());
            u32 topMost = std::min(Settings::values.custom_top_y.GetValue(),
                                   Settings::values.custom_bottom_y.GetValue());
            u32 rightMost = std::max(Settings::values.custom_top_x.GetValue() +
                                         Settings::values.custom_top_width.GetValue(),
                                     Settings::values.custom_bottom_x.GetValue() +
                                         Settings::values.custom_bottom_width.GetValue());
            u32 bottomMost = std::max(Settings::values.custom_top_y.GetValue() +
                                          Settings::values.custom_top_height.GetValue(),
                                      Settings::values.custom_bottom_y.GetValue() +
                                          Settings::values.custom_bottom_height.GetValue());
            width = rightMost - leftMost;
            height = bottomMost - topMost;
        }
        layout = CreateLayout(layout_option, width, height, swapped, upright, render_3d, false);
    }
    return layout;
    UNREACHABLE();
}

// TODO: confirm this works correctly under new system, currently naive
FramebufferLayout GetCardboardSettings(const FramebufferLayout& layout) {
    u32 top_screen_left = 0;
    u32 top_screen_top = 0;
    u32 bottom_screen_left = 0;
    u32 bottom_screen_top = 0;

    u32 cardboard_screen_scale = Settings::values.cardboard_screen_size.GetValue();
    u32 top_screen_width = ((layout.screens[0].rect.GetWidth() / 2) * cardboard_screen_scale) / 100;
    u32 top_screen_height =
        ((layout.screens[0].rect.GetHeight() / 2) * cardboard_screen_scale) / 100;
    u32 bottom_screen_width =
        ((layout.screens[1].rect.GetWidth() / 2) * cardboard_screen_scale) / 100;
    u32 bottom_screen_height =
        ((layout.screens[1].rect.GetHeight() / 2) * cardboard_screen_scale) / 100;
    const bool is_swapped = Settings::values.swap_screen.GetValue();
    const bool is_portrait = layout.height > layout.width;

    u32 cardboard_screen_width;
    u32 cardboard_screen_height;
    if (is_portrait) {
        switch (Settings::values.portrait_layout_option.GetValue()) {
        case Settings::PortraitLayoutOption::PortraitTopFullWidth:
        case Settings::PortraitLayoutOption::PortraitOriginal:
            cardboard_screen_width = top_screen_width;
            cardboard_screen_height = top_screen_height + bottom_screen_height;
            bottom_screen_left += (top_screen_width - bottom_screen_width) / 2;
            if (is_swapped)
                top_screen_top += bottom_screen_height;
            else
                bottom_screen_top += top_screen_height;
            break;
        default:
            cardboard_screen_width = is_swapped ? bottom_screen_width : top_screen_width;
            cardboard_screen_height = is_swapped ? bottom_screen_height : top_screen_height;
        }
    } else {
        switch (Settings::values.layout_option.GetValue()) {
        case Settings::LayoutOption::SideScreen:
            cardboard_screen_width = top_screen_width + bottom_screen_width;
            cardboard_screen_height = is_swapped ? bottom_screen_height : top_screen_height;
            if (is_swapped)
                top_screen_left += bottom_screen_width;
            else
                bottom_screen_left += top_screen_width;
            break;

        case Settings::LayoutOption::SingleScreen:
        default:

            cardboard_screen_width = is_swapped ? bottom_screen_width : top_screen_width;
            cardboard_screen_height = is_swapped ? bottom_screen_height : top_screen_height;
            break;
        }
    }
    s32 cardboard_max_x_shift = (layout.width / 2 - cardboard_screen_width) / 2;
    s32 cardboard_user_x_shift =
        (Settings::values.cardboard_x_shift.GetValue() * cardboard_max_x_shift) / 100;
    s32 cardboard_max_y_shift = (layout.height - cardboard_screen_height) / 2;
    s32 cardboard_user_y_shift =
        (Settings::values.cardboard_y_shift.GetValue() * cardboard_max_y_shift) / 100;

    // Center the screens and apply user Y shift
    FramebufferLayout new_layout = layout;
    new_layout.screens[0].rect.left = top_screen_left + cardboard_max_x_shift;
    new_layout.screens[0].rect.top =
        top_screen_top + cardboard_max_y_shift + cardboard_user_y_shift;
    new_layout.screens[1].rect.left = bottom_screen_left + cardboard_max_x_shift;
    new_layout.screens[1].rect.top =
        bottom_screen_top + cardboard_max_y_shift + cardboard_user_y_shift;

    // Set the X coordinates for the right eye and apply user X shift
    new_layout.cardboard.top_screen_right_eye =
        new_layout.screens[0].rect.left - cardboard_user_x_shift;
    new_layout.screens[0].rect.left += cardboard_user_x_shift;
    new_layout.cardboard.bottom_screen_right_eye =
        new_layout.screens[1].rect.left - cardboard_user_x_shift;
    new_layout.screens[1].rect.left += cardboard_user_x_shift;
    new_layout.cardboard.user_x_shift = cardboard_user_x_shift;

    // Update right/bottom instead of passing new variables for width/height
    new_layout.screens[0].rect.right = new_layout.screens[0].rect.left + top_screen_width;
    new_layout.screens[0].rect.bottom = new_layout.screens[0].rect.top + top_screen_height;
    new_layout.screens[1].rect.right = new_layout.screens[1].rect.left + bottom_screen_width;
    new_layout.screens[1].rect.bottom = new_layout.screens[1].rect.top + bottom_screen_height;

    return new_layout;
}

FramebufferLayout reverseLayout(FramebufferLayout layout) {
    std::swap(layout.height, layout.width);
    u32 oldLeft, oldRight, oldTop, oldBottom;
    for (Screen s : layout.screens) {
        oldLeft = s.rect.left;
        oldRight = s.rect.right;
        oldTop = s.rect.top;
        oldBottom = s.rect.bottom;
        s.rect.left = oldTop;
        s.rect.right = oldBottom;
        s.rect.top = layout.height - oldRight;
        s.rect.bottom = layout.height - oldLeft;
    }
    return layout;
}

FramebufferLayout ApplyFullStereo(FramebufferLayout layout, bool swap_eyes) {
    // assumptions: the screens have already been set up so they are on the left side
    // if swap_eyes is true, those screens have already been set to right eye
    for (Screen s : layout.screens) {
        Screen new_screen = s;
        new_screen.rect.left += layout.width / 2;
        new_screen.right_eye = !swap_eyes;
        layout.screens.push_back(new_screen);
    }
    return layout;
}

FramebufferLayout ApplyHalfStereo(FramebufferLayout layout, bool swap_eyes) {
    for (Screen s : layout.screens) {
        s.rect.right /= 2;
        Screen new_screen = s;
        new_screen.rect.left += layout.width / 2;
        new_screen.rect.right += layout.width / 2;
        new_screen.right_eye = !swap_eyes;
        layout.screens.push_back(new_screen);
    }
    return layout;
}

std::pair<unsigned, unsigned> GetMinimumSizeFromPortraitLayout() {
    const u32 gap = Settings::values.screen_gap.GetValue();
    const u32 min_width = Core::kScreenTopWidth;
    const u32 min_height = Core::kScreenTopHeight + Core::kScreenBottomHeight + gap;
    return std::make_pair(min_width, min_height);
}

std::pair<unsigned, unsigned> GetMinimumSizeFromLayout(Settings::LayoutOption layout) {
    u32 min_width, min_height, gap;
    const bool swapped = Settings::values.swap_screen.GetValue();
    gap = Settings::values.screen_gap.GetValue();

    switch (layout) {
    case Settings::LayoutOption::SingleScreen:
#ifndef ANDROID
    case Settings::LayoutOption::SeparateWindows:
#endif
        min_width = Settings::values.swap_screen ? Core::kScreenBottomWidth : Core::kScreenTopWidth;
        min_height = Core::kScreenBottomHeight;
        break;
    case Settings::LayoutOption::LargeScreen: {
        const int largeWidth = swapped ? Core::kScreenBottomWidth : Core::kScreenTopWidth;
        const int largeHeight = swapped ? Core::kScreenBottomHeight : Core::kScreenTopHeight;
        int smallWidth = swapped ? Core::kScreenTopWidth : Core::kScreenBottomWidth;
        int smallHeight = swapped ? Core::kScreenTopHeight : Core::kScreenBottomHeight;
        smallWidth =
            static_cast<int>(smallWidth / Settings::values.large_screen_proportion.GetValue());
        smallHeight =
            static_cast<int>(smallHeight / Settings::values.large_screen_proportion.GetValue());
        min_width = static_cast<u32>(Settings::values.small_screen_position.GetValue() ==
                                                 Settings::SmallScreenPosition::AboveLarge ||
                                             Settings::values.small_screen_position.GetValue() ==
                                                 Settings::SmallScreenPosition::BelowLarge
                                         ? std::max(largeWidth, smallWidth)
                                         : largeWidth + smallWidth + gap);
        min_height = static_cast<u32>(Settings::values.small_screen_position.GetValue() ==
                                                  Settings::SmallScreenPosition::AboveLarge ||
                                              Settings::values.small_screen_position.GetValue() ==
                                                  Settings::SmallScreenPosition::BelowLarge
                                          ? largeHeight + smallHeight + gap
                                          : std::max(largeHeight, smallHeight));
        break;
    }
    case Settings::LayoutOption::SideScreen:
        min_width = Core::kScreenTopWidth + Core::kScreenBottomWidth + gap;
        min_height = Core::kScreenBottomHeight;
        break;
    case Settings::LayoutOption::Default:
    default:
        min_width = Core::kScreenTopWidth;
        min_height = Core::kScreenTopHeight + Core::kScreenBottomHeight + gap;
        break;
    }
    return std::make_pair(min_width, min_height);
}

float FramebufferLayout::GetAspectRatioValue(Settings::AspectRatio aspect_ratio) {
    switch (aspect_ratio) {
    case Settings::AspectRatio::R16_9:
        return 9.0f / 16.0f;
    case Settings::AspectRatio::R4_3:
        return 3.0f / 4.0f;
    case Settings::AspectRatio::R21_9:
        return 9.0f / 21.0f;
    case Settings::AspectRatio::R16_10:
        return 10.0f / 16.0f;
    default:
        LOG_ERROR(Frontend, "Unknown aspect ratio enum value: {}",
                  static_cast<std::underlying_type<Settings::AspectRatio>::type>(aspect_ratio));
        return 1.0f; // Arbitrary fallback value
    }
}

} // namespace Layout
