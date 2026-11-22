<p align="center">
  <img src="src/citra_ios/Resources/Assets.xcassets/AppIcon.appiconset/icon-1024.png" width="160" alt="Neroli app icon">
</p>

<h1 align="center">Neroli</h1>

<p align="center">
  <b>A Nintendo 3DS emulator for iPhone and iPad, built on Azahar with a native Metal renderer.</b>
</p>

<p align="center">
  <a href="LICENSE.txt"><img src="https://img.shields.io/badge/license-GPLv3%2B-blue" alt="License: GPLv3 or later"></a>
  <img src="https://img.shields.io/badge/iOS-16%2B-black?logo=apple" alt="iOS 16 or later">
  <img src="https://img.shields.io/badge/renderer-Metal-orange" alt="Metal renderer">
</p>

---

Neroli is an iOS port of [Azahar](https://github.com/azahar-emu/azahar), the open-source 3DS
emulator that continues the work of Citra. It replaces Azahar's desktop and Android frontends with
a SwiftUI app made for iPhone and iPad, and renders through a Metal backend written for Apple GPUs,
with no Vulkan or MoltenVK layer in between.

The name comes from neroli oil, distilled from orange blossoms. *Azahar* is Spanish for orange
blossom.

> [!NOTE]
> Neroli is an independent fork. It is **not affiliated with or endorsed by the Azahar developers
> or by Nintendo**. Please report problems with Neroli here, not to the Azahar project.

## Features

### Graphics

- **Native Metal renderer** tuned for Apple's tile-based GPUs: hidden surface removal, depth-only
  pipelines, hardware scissoring and in-pass clears.
- **Hardware shaders**: the 3DS's vertex programs run on the GPU as Metal shaders.
- **Shader cache** that persists between sessions (a per-game disk cache plus a Metal binary
  archive), with asynchronous compilation, so stutter fades after the first play.
- **Higher internal resolutions**, **MSAA anti-aliasing**, anisotropic filtering, texture
  filters (Anime4K, ScaleForce, xBRZ, MMPX and more) and custom texture packs.
- **MetalFX upscaling** and sharpening, including contrast adaptive sharpening.
- **Post-processing**: built-in effects plus your own `.metal` shaders.
- **3D modes**: side-by-side, anaglyph and interlaced, with per-eye options.

### Smoothness

- **Frame generation** (iOS 26): Apple's VideoToolbox or MetalFX frame interpolation turns 60 FPS
  into 120 FPS on ProMotion displays.
  - **Adaptive** sizing, **Pause When Behind**, **Frame Buffer** and **Top Screen Only**.
- **Frame pacing** driven by the display, a per-game **60 Hz or 120 Hz** refresh rate, and a
  30 FPS frame cap for games that run at 30.

### Playing

- Game library with grid and list views, per-game settings and play statistics.
- **On-screen controller** you can rearrange, resize and save as layouts.
- **Game controllers** with button mapping and profiles, motion controls and haptics.
- **Save states**, save data backup and restore, **cheats**, **amiibo**, movie recording.
- **CIA installation** for games, updates and DLC, plus game compression.
- **Online play** through Azahar's multiplayer rooms, and **Artic** streaming from a real 3DS.
- 3GX plugins, the system keyboard and Mii selector, cameras and microphone.
- **External display** support over AirPlay or a cable.

### iOS

- **JIT** through StikDebug or StosDebug (Universal.js), or the built-in JIT helper. Without JIT,
  Neroli falls back to the slower interpreters.
- Works inside **LiveContainer**.
- Liquid Glass interface on iOS 26, with your data in the Files app.

## Requirements

- An iPhone or iPad on **iOS 16 or later** (arm64).
- **iOS 26** for frame generation and the Liquid Glass design.
- A recent device is strongly recommended. Development and testing happen on an iPhone 16 Pro Max.
- **JIT** for full speed. See [Enabling JIT](#enabling-jit).

## Installing

There are no prebuilt releases yet. [Build the IPA](#building) or use one built by CI, then
install it with a sideloading tool such as SideStore, AltStore or Sideloadly, or add it to
LiveContainer.

Updating keeps your data as long as you install over the existing app: the bundle identifier
never changes. Versions from before the rename (Azahar-iOS) update in place too. Their `Azahar`
data folder is moved to `Neroli` on the first launch.

## Enabling JIT

iOS only lets an app generate code while a debugger is attached to it. Neroli needs that for the
CPU recompiler (dynarmic) and the shader JIT. Choose one:

- **StikDebug or StosDebug**: launch Neroli from the debugger app, which attaches and runs the
  Universal.js script.
- **Built-in JIT Helper**: import your device's pairing file in **Settings › General**. Neroli then
  enables JIT itself through a small app extension, using the
  [idevice](https://github.com/jkcoxson/idevice) library.

**Settings › General** shows whether JIT is active.

## Getting started

1. **Dump your games** from your own 3DS. Neroli does not include games, system files or keys, and
   does not help you get them.
2. Import games with the **+** button, or copy them to **Files › On My iPhone › Neroli › ROMs**.
   Supported formats: `.3ds`/`.cci`, `.cxi`, `.3dsx`, `.app`, `.elf`/`.axf`, their compressed
   `.z*` versions, and `.cia` to install.
3. Optionally, copy `sysdata`, `nand` and `sdmc` from a desktop Azahar install to
   **Files › Neroli › Neroli**. The folder layout is the same.

Many options can be set per game: open a game's menu in the library and choose **Game Settings**.

## Building

You need macOS with Xcode 16 or later.

```bash
git submodule update --init --recursive
```

```bash
./.ci/ios.sh configure
```

```bash
./.ci/ios.sh build
```

```bash
./.ci/ios.sh package
```

This writes `artifacts/Neroli-unsigned.ipa`. For the built-in JIT helper, first build the idevice
library with `./.ci/build-idevice.sh build/idevice-out`, which needs Rust. The CircleCI pipeline
in `.circleci/config.yml` runs all of these steps and validates every generated Metal shader with
Apple's compiler.

For signing with a free Apple account, the Metal renderer's architecture and more, see the
[iOS port's README](src/citra_ios/README.md).

## Reporting problems

Turn on **Write Log** in **Settings › About**, reproduce the problem, then use **Share Log** and
attach the file to an [issue](https://github.com/JMillz269/Neroli/issues). Include your device,
iOS version, the game, and whether JIT was on.

## Credits

- [Azahar](https://github.com/azahar-emu/azahar) and [Citra](https://github.com/citra-emu/citra)
  and their contributors, whose emulator Neroli is built on.
- [dynarmic](https://github.com/azahar-emu/dynarmic) and
  [oaknut](https://github.com/merryhime/oaknut) by merryhime, the CPU recompiler.
- [idevice](https://github.com/jkcoxson/idevice) by Jackson Coxson, used by the JIT helper.
- [DolphiniOS](https://github.com/OatmealDome/dolphin-ios) and ARMSX3, whose approach the JIT
  helper follows.
- The authors of every library listed in **Settings › About › Licenses**.

## License

Neroli is free software, licensed under the
[GNU General Public License, version 3](LICENSE.txt) or any later version. Some older files from
Azahar and Citra are under the [GPLv2 or later](misc/licenses/gplv2.txt). Third-party components
keep their own licenses, which the app shows under **Settings › About › Licenses**.

Nintendo 3DS is a trademark of Nintendo. Neroli is not affiliated with Nintendo in any way.
