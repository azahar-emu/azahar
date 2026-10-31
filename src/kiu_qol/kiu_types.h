// Kid Icarus: Uprising single-screen QOL layer for Azahar.
// Licensed under GPLv2 or any later version (same as Azahar).
//
// Plain C++17, no emulator dependencies: everything in kiu_qol/ can be unit-tested on its own.
// Coordinate conventions:
//   * "bottom px": 3DS bottom-screen pixels, 320x240, origin top-left (same as touch coords).
//   * "top logical": 3DS top-screen pixels, 400x240, origin top-left. Widget placement is
//     expressed here so it scales with the window.
//   * "framebuffer": host window pixels.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace KiuQol {

constexpr int kBottomW = 320;
constexpr int kBottomH = 240;
constexpr int kTopW = 400;
constexpr int kTopH = 240;

enum class GameMode : std::uint8_t { Gameplay = 0, Dialogue = 1, Menu = 2 };
constexpr int kModeCount = 3;
using ModeMask = std::uint8_t;
constexpr ModeMask ModeBit(GameMode m) {
    return static_cast<ModeMask>(1u << static_cast<int>(m));
}
constexpr ModeMask kAllModes = 0b111;

const char* ModeName(GameMode m);

struct RectI {
    int x = 0, y = 0, w = 0, h = 0;
    bool Contains(int px, int py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
};

struct RectF {
    float x = 0, y = 0, w = 0, h = 0;
    bool Contains(float px, float py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
};

/// Mirrors Common::Rectangle<float> as used for Azahar's display_texcoords.
struct TexRect {
    float left = 0, top = 0, right = 1, bottom = 1;
};

enum class Anchor {
    TopLeft, TopCenter, TopRight,
    CenterLeft, Center, CenterRight,
    BottomLeft, BottomCenter, BottomRight,
};

struct WidgetConfig {
    std::string name;
    RectI src{0, 0, kBottomW, kBottomH}; ///< region of the bottom screen (bottom px)
    Anchor anchor = Anchor::BottomRight;
    float offset_x = 0, offset_y = 0;    ///< top logical px, applied after anchoring
    float scale = 1.0f;                  ///< 1.0 = one bottom px per top logical px
    float opacity = 1.0f;
    ModeMask modes = ModeBit(GameMode::Gameplay);
    bool touchable = false;              ///< mouse clicks on it become touches at src coords
};

/// "If guest memory at addr (masked) == value, the game is in `mode`."
struct ProbeConfig {
    std::uint32_t addr = 0;
    std::uint8_t size = 4; ///< 1, 2 or 4 bytes
    std::uint32_t mask = 0xFFFFFFFF;
    std::uint32_t value = 0;
    bool not_equal = false;
    GameMode mode = GameMode::Gameplay;
    int priority = 0; ///< highest matching priority wins
};

struct TapStep {
    int x = 0, y = 0;   ///< bottom px
    int hold_ms = 50;   ///< stylus down
    int gap_ms = 34;    ///< stylus up afterwards
};

struct MacroConfig {
    std::string name;
    std::string button; ///< Azahar input param string, e.g. "engine:keyboard,code:49"
    std::vector<TapStep> steps;
    ModeMask modes = kAllModes;
};

struct StylusConfig {
    float mouse_sensitivity = 0.5f; ///< bottom px per framebuffer px of mouse motion
    float stick_speed = 260.0f;     ///< bottom px per second at full deflection
    float stick_deadzone = 0.15f;
    float stick_curve = 1.6f;       ///< response exponent (>1 = finer control near center)
    bool invert_y = false;
    float edge_margin = 28.0f;      ///< recenter when stylus gets this close to an edge
    int settle_ms = 50;             ///< hold still before a recenter lift (kills flick momentum)
    int lift_ms = 34;               ///< how long the stylus stays up during a recenter
    int stick_release_ms = 120;     ///< lift after the stick has been centered this long
    std::string stick_device;       ///< Azahar analog param string (right stick)
};

enum class EnableMode { Auto, On, Off };

struct Config {
    EnableMode enabled = EnableMode::Auto;
    std::vector<std::uint64_t> title_ids;
    std::vector<std::string> title_name_contains{"Kid Icarus", "Icarus"};

    GameMode default_mode = GameMode::Gameplay;
    std::string mode_cycle_button;  ///< cycles Auto -> Gameplay -> Dialogue -> Menu -> Auto
    std::string aim_lock_button;    ///< toggles pointer-lock mouse aiming
    std::string reload_button;      ///< forces a config reload

    StylusConfig stylus;
    std::vector<WidgetConfig> widgets;
    std::vector<ProbeConfig> probes;
    std::vector<MacroConfig> macros;

    std::vector<std::string> warnings; ///< parse problems, surfaced in the log
};

/// Output of the touch pipeline, in bottom px.
struct TouchOut {
    float x = 0, y = 0;
    bool pressed = false;
};

} // namespace KiuQol
