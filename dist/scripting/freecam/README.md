# Free camera

`azahar_freecam.py` allows moving the camera of a running application with a controller.

## Setup

1. Use an Azahar build with the free camera (2127 or later, enabled on the desktop builds).
2. Turn on Emulation > Configure > Debug > Enable RPC server, then start the game.
3. Install pygame (`pip install pygame`), connect a controller and run `python azahar_freecam.py`.

Settings such as speeds, field of view and button numbers are constants at the top of the script
that can be adjusted in real time or before running the script. Values may need tuning per scene.

## Controls

| Button | Action |
| --- | --- |
| Start | free camera on / off |
| Back | reset the camera |
| Left stick | move |
| Right stick | look around |
| RB / LB | move up / down |
| RT / LT (held) | move faster / slower |
| D-pad up / down | field of view |
| D-pad left / right | near plane (also scales the movement speed) |
| Y / X | far plane |

The button numbers are those of an Xbox controller, you can adjust them in the script.

## How it works and its limits

The 3DS runs the game's own vertex shaders, so Azahar never sees the game's camera, only the final
position of each vertex on screen. The script guesses the game's projection, undoes it, moves the
camera and projects again. This works without any game specific code, but it has limits compared
with emulators of consoles with a fixed vertex pipeline (such as Dolphin's free look), which know
the real camera, or with game specific camera patches:

- The projection is a guess, and games often change it between scenes. If the scene looks
  stretched or moves at the wrong speed, adjust the field of view and the near and far planes with
  the controller.
- The game still decides what to draw for its own camera, so objects outside its view may be
  missing, and geometry near or behind its camera gets cut off.
- Shadows, reflections and other effects can break or end up in the wrong place.
- Lighting and fog still follow the game's camera.
- HUD elements are left in place when they are drawn without depth (`SKIP_2D`).
  Some HUDs are not, and move with the camera. If this causes problems set
  `SKIP_2D` to `False`.
