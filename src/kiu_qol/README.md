# Kid Icarus: Uprising single-screen QOL (Azahar fork)

Phase 1 of the KIU PC recompilation project: an Azahar fork that plays the game on one screen.
The top screen fills the window, and pieces of the bottom screen (radar, powers, menus,
anything else) are composited over it as HUD widgets. Aiming and camera use the mouse or the
right stick through a "virtual stylus", and powers can be bound to keys.

You need your own legally dumped copy of the game. Nothing here contains Nintendo code or assets.

## What it does

| Feature | How |
|---|---|
| One screen | When the game is detected, the window switches to a top-screen-only layout. |
| HUD widgets | Any rectangle of the bottom screen can be drawn anywhere on the top screen, scaled and translucent, per game mode. |
| Clickable widgets | Clicking a `touchable` widget becomes a touch at the matching bottom-screen point. |
| Mouse aim | Hold LMB anywhere outside a widget and drag, or press F6 for locked mouse-look. |
| Right-stick aim | Stick deflection drives the stylus at a configurable speed and curve. |
| Power macros | A key or gamepad button plays one or more taps on the bottom screen. |
| Game modes | `gameplay`, `dialogue` and `menu` each show their own widgets. Modes come from guest-memory probes once you find the addresses, or from the F5 cycle key until then. |
| Live config | `kiu_qol.ini` reloads within a second of saving, so calibration is edit-and-look. |

### The virtual stylus

The game reads aiming and camera from a stylus drag, and a lift while moving is read as a
flick that keeps the camera spinning. The virtual stylus presses at the screen center, follows
your relative motion, and when it nears an edge it holds still for `settle_ms` (killing the
momentum), lifts for `lift_ms`, and presses again at the center. Motion during that gap is
buffered and applied after the re-press. Tune `edge_margin`, `settle_ms` and `lift_ms` if
turning feels hitchy or the camera drifts after a recenter.

## Building

Apply the patch to Azahar at commit `6e42de8`, then build normally:

```sh
git clone --recursive https://github.com/azahar-emu/azahar.git
cd azahar && git checkout 6e42de8
git apply kiu_qol.patch
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The core library has standalone tests that need no emulator:

```sh
g++ -std=c++17 -I src src/kiu_qol/*.cpp src/kiu_qol/tests/kiu_qol_tests.cpp -o kiu_tests && ./kiu_tests
```

They also build as `kiu_qol_tests` when `ENABLE_TESTS` is on.

## First run

1. Launch the game. On first run `kiu_qol.ini` is written to Azahar's config folder (the log
   prints the exact path).
2. The layer turns on when the game title contains "Kid Icarus". For other regions, paste the
   Title ID from Azahar's game list into `title_ids`, or set `enabled = on`.
3. You start with the whole bottom screen as a small clickable inset in the corner, and as a
   large panel in `menu` mode. Everything is playable from there.
4. Calibrate. Take an Azahar screenshot during a battle, read the radar and power coordinates
   off the bottom half, uncomment `[widget.radar]` and `[widget.powers]`, and fill in `src`.
   Save and the change appears in about a second.
5. Bind powers with `[macro.*]` sections, and set `stick_device` for right-stick aiming. Copy
   the param strings from `qt-config.ini` after binding the same input to any 3DS control.

Default hotkeys: **F5** cycles the mode override (auto → gameplay → dialogue → menu),
**F6** toggles mouse-look, **F7** forces a config reload.

## Finding mode probes

Mode probes make widget switching automatic. Look for a value that differs between battle,
cutscene/dialogue and menus (a scene ID, a pause flag, a "touch UI active" flag). Use a RAM
search tool or the GDB stub, confirm the address holds across several chapters and a reload,
then add a `[probe.*]` section. Each probe compares `(value & mask)` with `==` or `!=`, and the
highest matching `priority` wins.

## Where the code lives

| File | Role |
|---|---|
| `src/kiu_qol/` | Emulator-independent core: config, HUD math, stylus, macros, controller, tests. |
| `src/core/kiu_integration.*` | Builds input devices from the config, reads guest memory for probes. |
| `src/core/hle/service/hid/hid.cpp` | Lets the layer own the touch screen each pad update. |
| `src/core/core.cpp` | Activates on game load, resets on shutdown. |
| `src/core/frontend/emu_window.*` | Top-only layout and routing of pointer input to the layer. |
| `src/video_core/renderer_opengl/`, `renderer_vulkan/` | Composite the cropped bottom-screen widgets. |
| `src/citra_qt/bootmanager.cpp` | Cursor lock for mouse-look. |

## Known limits

- HUD widgets render with 3D off. OpenGL also supports anaglyph and interlaced; side-by-side
  and VR are not supported yet.
- Desktop landscape only. Portrait and Android layouts are left untouched.
- Widgets show the bottom screen's pixels as the game drew them. Re-authored HD HUD art comes
  with the recompilation phase, when the draw calls themselves can be redirected.
