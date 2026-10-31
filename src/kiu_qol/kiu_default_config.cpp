// Licensed under GPLv2 or any later version.
#include "kiu_qol/kiu_config.h"

namespace KiuQol {

// Written next to Azahar's other config files on first launch. Edits are picked up live
// (within ~1 second) while the game runs, so the fastest way to calibrate a widget is to keep
// this file open in an editor beside the game window.
const char* const kDefaultConfigText = R"INI(; Kid Icarus: Uprising single-screen QOL config
;
; Coordinates:
;   src     = x, y, w, h   in BOTTOM-screen pixels (320x240, origin top-left)
;   offset  = x, y         in TOP-screen pixels (400x240), applied after anchoring
;   taps    = x, y [, hold_ms [, gap_ms]] | ...   bottom-screen pixels
; Tip: take a screenshot in Azahar (it saves both screens) and read pixel coordinates off the
; bottom half in any image editor.
;
; Input bindings use Azahar's input param strings. Easiest way to get one: bind the key or
; button to any 3DS control in Azahar's input settings, then copy the matching line from
; qt-config.ini. Examples:
;   engine:keyboard,code:49                       (Qt key code for '1')
;   engine:sdl,port:0,guid:<guid>,button:9        (gamepad button)
;   engine:sdl,port:0,guid:<guid>,axis_x:2,axis_y:3,deadzone:0.0   (right stick, for stylus)

[general]
; auto = turn on when the running title matches title_ids or title_name_contains
enabled = auto
; Paste your dump's Title ID from Azahar's game list (16 hex digits).
title_ids =
title_name_contains = Kid Icarus
default_mode = gameplay
mode_cycle_button = engine:keyboard,code:16777268   ; F5: auto -> gameplay -> dialogue -> menu
aim_lock_button = engine:keyboard,code:16777269     ; F6: mouse-look (cursor locked)
reload_button = engine:keyboard,code:16777270       ; F7: force config reload

[stylus]
mouse_sensitivity = 0.5
; Right stick drives the stylus. Fill in your controller's param string to enable it.
stick_device =
stick_speed = 260
stick_deadzone = 0.15
stick_curve = 1.6
invert_y = false
edge_margin = 28
settle_ms = 50
lift_ms = 34
stick_release_ms = 120

; ---------------------------------------------------------------------------------------
; Widgets: pieces of the bottom screen redrawn on top of the top screen.
; Drawn in file order; later widgets sit on top. Clicks on a touchable widget become
; touches at the matching bottom-screen point.
; ---------------------------------------------------------------------------------------

; Placeholder until the radar/power regions are calibrated: the whole bottom screen as a
; small clickable inset during gameplay. Delete or set opacity = 0 once the crops below work.
[widget.bottom_inset]
src = 0, 0, 320, 240
anchor = bottom_right
offset = -6, -6
scale = 0.35
opacity = 0.9
modes = gameplay, dialogue
touchable = true

; Menus live on the bottom screen, so show it large and clickable there.
[widget.menu_screen]
src = 0, 0, 320, 240
anchor = center
offset = 0, 0
scale = 0.75
opacity = 1.0
modes = menu
touchable = true

; CALIBRATE: radar / minimap region of the in-battle bottom screen.
;[widget.radar]
;src = 0, 0, 120, 120
;anchor = top_right
;offset = -6, 6
;scale = 0.8
;opacity = 0.85
;modes = gameplay
;touchable = false

; CALIBRATE: power selection strip.
;[widget.powers]
;src = 0, 180, 320, 60
;anchor = bottom_left
;offset = 6, -6
;scale = 0.6
;opacity = 0.9
;modes = gameplay
;touchable = true

; CALIBRATE: anything conversation-related the game puts on the bottom screen.
;[widget.dialogue]
;src = 0, 0, 320, 240
;anchor = bottom_center
;offset = 0, -4
;scale = 0.5
;opacity = 0.95
;modes = dialogue
;touchable = true

; ---------------------------------------------------------------------------------------
; Mode probes: guest-memory checks that pick gameplay / dialogue / menu automatically.
; Find candidate addresses with a RAM search (value changes between battle and menus) and
; verify them across several chapters. Highest matching priority wins; no match uses
; default_mode. Until you have some, use the mode_cycle_button.
; ---------------------------------------------------------------------------------------
;[probe.in_menu]
;addr = 0x00000000
;size = 1
;mask = 0xFF
;value = 0x01
;op = ==
;mode = menu
;priority = 10

; ---------------------------------------------------------------------------------------
; Macros: a button press becomes one or more taps on the bottom screen (powers, items,
; on-screen buttons). One macro plays at a time.
; ---------------------------------------------------------------------------------------
;[macro.power_1]
;button = engine:keyboard,code:49
;taps = 30, 200, 60
;modes = gameplay
)INI";

} // namespace KiuQol
