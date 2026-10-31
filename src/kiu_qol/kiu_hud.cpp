// Licensed under GPLv2 or any later version.
#include "kiu_qol/kiu_hud.h"

#include <algorithm>

namespace KiuQol {

RectF WidgetLogicalRect(const WidgetConfig& w) {
    const float ww = static_cast<float>(w.src.w) * w.scale;
    const float wh = static_cast<float>(w.src.h) * w.scale;
    float x = 0, y = 0;
    switch (w.anchor) {
    case Anchor::TopLeft:
    case Anchor::CenterLeft:
    case Anchor::BottomLeft:
        x = 0;
        break;
    case Anchor::TopCenter:
    case Anchor::Center:
    case Anchor::BottomCenter:
        x = (kTopW - ww) * 0.5f;
        break;
    default:
        x = kTopW - ww;
        break;
    }
    switch (w.anchor) {
    case Anchor::TopLeft:
    case Anchor::TopCenter:
    case Anchor::TopRight:
        y = 0;
        break;
    case Anchor::CenterLeft:
    case Anchor::Center:
    case Anchor::CenterRight:
        y = (kTopH - wh) * 0.5f;
        break;
    default:
        y = kTopH - wh;
        break;
    }
    return {x + w.offset_x, y + w.offset_y, ww, wh};
}

RectF WidgetFramebufferRect(const WidgetConfig& w, const RectF& top) {
    const RectF l = WidgetLogicalRect(w);
    const float sx = top.w / kTopW;
    const float sy = top.h / kTopH;
    return {top.x + l.x * sx, top.y + l.y * sy, l.w * sx, l.h * sy};
}

std::vector<PlacedWidget> PlaceWidgets(const std::vector<WidgetConfig>& widgets, GameMode mode,
                                       const RectF& top) {
    std::vector<PlacedWidget> out;
    for (const auto& w : widgets) {
        if ((w.modes & ModeBit(mode)) && w.opacity > 0.0f)
            out.push_back({&w, WidgetFramebufferRect(w, top)});
    }
    return out;
}

TexRect CropTexcoords(const TexRect& b, const RectI& src) {
    const float fx0 = static_cast<float>(src.x) / kBottomW;
    const float fx1 = static_cast<float>(src.x + src.w) / kBottomW;
    const float fy0 = static_cast<float>(src.y) / kBottomH;
    const float fy1 = static_cast<float>(src.y + src.h) / kBottomH;
    TexRect r;
    r.left = b.left + fx0 * (b.right - b.left);
    r.right = b.left + fx1 * (b.right - b.left);
    r.bottom = b.bottom + fy0 * (b.top - b.bottom);
    r.top = b.bottom + fy1 * (b.top - b.bottom);
    return r;
}

TouchOut MapIntoWidget(const PlacedWidget& p, float fb_x, float fb_y) {
    const auto& s = p.cfg->src;
    const float u = std::clamp((fb_x - p.dst.x) / p.dst.w, 0.0f, 1.0f);
    const float v = std::clamp((fb_y - p.dst.y) / p.dst.h, 0.0f, 1.0f);
    TouchOut t;
    t.x = std::min(s.x + u * s.w, static_cast<float>(s.x + s.w - 1));
    t.y = std::min(s.y + v * s.h, static_cast<float>(s.y + s.h - 1));
    t.pressed = true;
    return t;
}

std::optional<TouchOut> HitTestTouch(const std::vector<PlacedWidget>& placed, float fb_x,
                                     float fb_y) {
    for (auto it = placed.rbegin(); it != placed.rend(); ++it) {
        if (it->dst.Contains(fb_x, fb_y)) {
            // The topmost widget swallows the click even if it isn't touchable, so clicking a
            // passive overlay (e.g. the radar) never leaks through as an aim input.
            if (!it->cfg->touchable)
                return TouchOut{0, 0, false};
            return MapIntoWidget(*it, fb_x, fb_y);
        }
    }
    return std::nullopt;
}

} // namespace KiuQol
