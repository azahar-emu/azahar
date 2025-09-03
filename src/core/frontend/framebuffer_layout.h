// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/math_util.h"
#include "common/settings.h"

namespace Layout {

/// Orientation of the 3DS displays
enum class DisplayOrientation {
    Landscape,        // Default orientation of the 3DS
    Portrait,         // 3DS rotated 90 degrees counter-clockwise
    LandscapeFlipped, // 3DS rotated 180 degrees counter-clockwise
    PortraitFlipped,  // 3DS rotated 270 degrees counter-clockwise
};

/// Describes the horizontal coordinates for the right eye screen when using Cardboard VR
struct CardboardSettings {
    u32 top_screen_right_eye;
    u32 bottom_screen_right_eye;
    s32 user_x_shift;
};

struct Screen {
    Common::Rectangle<u32> rect;
    bool is_bottom;
    bool right_eye = false;
    bool enabled = true;
};

/// Describes the layout of the window framebuffer (size and top/bottom screen positions)
struct FramebufferLayout {
    u32 width;
    u32 height;
    std::vector<Screen> screens;
    DisplayOrientation orientation = DisplayOrientation::Landscape;
    bool is_portrait = false;
    Settings::StereoRenderOption render_3d_mode = Settings::values.render_3d.GetValue();
    CardboardSettings cardboard = {};
    /**
     * Returns the ratio of pixel size of the top screen, compared to the native size of the 3DS
     * screen.
     */
    u32 GetScalingRatio() const;

    static float GetAspectRatioValue(Settings::AspectRatio aspect_ratio);
};

/**
 * Method to create a rotated copy of a framebuffer layout, used to rotate to upright mode
 */
FramebufferLayout reverseLayout(FramebufferLayout layout);

/**
 * Method to duplicate a framebuffer layout to the right side with the other eye, creating
 * full-width stereo behavior
 */
FramebufferLayout ApplyFullStereo(FramebufferLayout layout, bool swap_eyes);

/**
 * Method to duplicate a framebuffer layout to the right side with the other eye, creating
 * half-width stereo behavior
 */
FramebufferLayout ApplyHalfStereo(FramebufferLayout layout, bool swap_eyes);

/**
 * Factory method for constructing a standard landscape layout based on a layout_option
 * @param layout_option The Layout Option to use
 * @param width Window framebuffer width in pixels
 * @param height Window framebuffer height in pixels
 * @param swapped if true, the bottom screen will be displayed above the top screen
 * @param upright if true, rotate the screens 90 degrees counter-clockwise
 * @param render_3d the StereoRenderOption to apply to this layout
 * @param swap_eyes in mono mode, this will make mono do the right eye. In stereo modes, it will
 * swap left eye and right eye
 * @return Newly created FramebufferLayout object with default screen regions initialized
 */
FramebufferLayout CreateLayout(Settings::LayoutOption layout_option, u32 width, u32 height,
                               bool swapped = false, bool upright = false, Settings::StereoRenderOption render_3d = Settings::StereoRenderOption::Off,
                               bool swap_eyes = false);

/**
 * Factory method for constructing a portrait layout based on a layout_option
 * @param layout_option The Layout Option to use
 * @param width Window framebuffer width in pixels
 * @param height Window framebuffer height in pixels
 * @param swapped if true, the bottom screen will be displayed above the top screen
 * @param upright if true, rotate the screens 90 degrees counter-clockwise
 * @param render_3d the StereoRenderOption to apply to this layout
 * @param swap_eyes in mono mode, this will make mono do the right eye. In stereo modes, it will
 * swap left eye and right eye
 * @return Newly created FramebufferLayout object with default screen regions initialized
 */
FramebufferLayout CreatePortraitLayout(Settings::PortraitLayoutOption layout_option, u32 width,
                                       u32 height, bool swapped, bool upright,
                                       Settings::StereoRenderOption render_3d,
                                       bool swap_eyes = false);

/**
 * Factory method for constructing the layout for the secondary mobile screen, if enabled
 * @param layout_option The Layout Option to use
 * @param width Window framebuffer width in pixels
 * @param height Window framebuffer height in pixels
 * @param swapped if true, the bottom screen will be displayed above the top screen
 * @param upright if true, rotate the screens 90 degrees counter-clockwise
 * @param render_3d the StereoRenderOption to apply to this layout
 * @param swap_eyes in mono mode, this will make mono do the right eye. In stereo modes, it will
 * swap left eye and right eye
 * @return Newly created FramebufferLayout object with default screen regions initialized
 */
FramebufferLayout CreateMobileSecondaryLayout(Settings::SecondaryDisplayLayout layout_option,
                                              u32 width, u32 height, bool swapped, bool upright,
                                              Settings::StereoRenderOption render_3d,
                                              bool swap_eyes = false);

/**
 * Factory method for constructing a default FramebufferLayout with only one screen
 * @param width Window framebuffer width in pixels
 * @param height Window framebuffer height in pixels
 * @param is_bottom if true, the bottom screen will be displayed
 * @param swap_eyes in mono mode, this will make mono do the right eye. In stereo modes, it will
 * swap left eye and right eye
 * @return Newly created FramebufferLayout object with default screen regions initialized
 */

FramebufferLayout SingleFrameLayout(u32 width, u32 height, bool is_bottom, bool swap_eyes = false);

/**
 * Factory method for constructing a Frame with top and bottom screens, arranged in a variety of
 * ways
 * @param width Window framebuffer width in pixels
 * @param height Window framebuffer height in pixels
 * @param swapped if true, the bottom screen will be displayed above the top screen
 * @param scale_factor The ratio between the large screen with respect to the smaller screen
 * @param vertical_alignment The vertical alignment of the smaller screen relative to the larger
 * screen
 * @param swap_eyes in mono mode, this will make mono do the right eye. In stereo modes, it will
 * swap left eye and right eye
 * @return Newly created FramebufferLayout object with default screen regions initialized
 */
FramebufferLayout LargeFrameLayout(u32 width, u32 height, bool is_swapped, float scale_factor,
                                   Settings::SmallScreenPosition small_screen_position,
                                   bool swap_eyes = false);
/**
 * Factory method for constructing a frame with 2.5 times bigger top screen on the right,
 * and 1x top and bottom screen on the left
 * @param width Window framebuffer width in pixels
 * @param height Window framebuffer height in pixels
 * @param is_swapped if true, the bottom screen will be the large display
 * @param swap_eyes in mono mode, this will make mono do the right eye. In stereo modes, it will
 * swap left eye and right eye
 * @return Newly created FramebufferLayout object with default screen regions initialized
 */
FramebufferLayout HybridScreenLayout(u32 width, u32 height, bool swapped, bool swap_eyes);

/**
 * Factory method for constructing a framebuffer based on custom settings
 * @param width Window framebuffer width in pixels
 * @param height Window framebuffer height in pixels
 * @param is_swapped switches the top and bottom displays (looks weird!)
 * @param is_portrait_mode used for mobile portrait, has its own custom settings
 * @param swap_eyes swaps left and right eyes OR renders right eye instead of left
 * @return Newly created FramebufferLayout object with default screen regions initialized
 */

FramebufferLayout CustomFrameLayout(u32 width, u32 height, bool is_swapped,
                                    bool is_portrait_mode = false, bool swap_eyes = false);

/**
 * Convenience method to get frame layout by resolution scale
 * Read from the current settings to determine which layout to use.
 * @param res_scale resolution scale factor
 * @param is_portrait_mode defaults to false
 */
FramebufferLayout FrameLayoutFromResolutionScale(u32 res_scale, bool is_secondary = false,
                                                 bool is_portrait_mode = false);

/**
 * Convenience method for transforming a frame layout when using Cardboard VR
 * @param layout frame layout to transform
 * @return layout transformed with the user cardboard settings
 */
FramebufferLayout GetCardboardSettings(const FramebufferLayout& layout);

std::pair<unsigned, unsigned> GetMinimumSizeFromLayout(Settings::LayoutOption layout);

std::pair<unsigned, unsigned> GetMinimumSizeFromPortraitLayout();

} // namespace Layout
