// Licensed under GPLv2 or any later version.
#pragma once

#include <optional>
#include <vector>

#include "kiu_qol/kiu_types.h"

namespace KiuQol {

/// A widget resolved for drawing this frame.
struct PlacedWidget {
    const WidgetConfig* cfg = nullptr;
    RectF dst; ///< framebuffer px
};

/// Where the widget lands in top-screen logical space (400x240).
RectF WidgetLogicalRect(const WidgetConfig& w);

/// Maps a widget into the host framebuffer, given where the top screen is drawn there.
RectF WidgetFramebufferRect(const WidgetConfig& w, const RectF& top_screen_fb);

/// Widgets visible in `mode`, in config order (later entries draw on top).
std::vector<PlacedWidget> PlaceWidgets(const std::vector<WidgetConfig>& widgets, GameMode mode,
                                       const RectF& top_screen_fb);

/// Narrows Azahar's bottom-screen display texcoords to a sub-rectangle given in bottom px.
/// Azahar stores the 3DS framebuffer rotated, so in landscape the screen's X axis runs along
/// [left, right] and its Y axis runs from `bottom` to `top`. Works for any base rectangle,
/// including the resolution-scaled ones the hardware renderer produces.
TexRect CropTexcoords(const TexRect& base, const RectI& src);

/// If a framebuffer point hits a touchable widget, returns the bottom-screen point under it.
/// Topmost (last drawn) widget wins.
std::optional<TouchOut> HitTestTouch(const std::vector<PlacedWidget>& placed, float fb_x,
                                     float fb_y);

/// Maps a framebuffer point into a specific widget's source rect, clamping to its edges.
/// Used while a drag that began on the widget leaves it.
TouchOut MapIntoWidget(const PlacedWidget& w, float fb_x, float fb_y);

} // namespace KiuQol
