# Neroli

Neroli (formerly Azahar-iOS) is a native iOS port of Azahar, not affiliated with the Azahar
developers. It has:

- **A native Metal renderer** (`src/video_core/renderer_metal`). It talks to Metal directly. There is
  no Vulkan or MoltenVK layer.
- **A SwiftUI frontend** (`src/citra_ios/App`) with a game library, settings, an on-screen 3DS
  controller, game controller support and motion controls. Its layout follows MeloCafe/MeloNX.
- **An Objective-C++ bridge** (`src/citra_ios/Bridge`) that Swift imports as the `AzaharCore`
  module.

```
SwiftUI app (App/)                 ──import AzaharCore──▶  Bridge/ (Objective-C++)
  GamesListView / SettingsView                                AZEmulator, AZGameInfo, AZConfiguration
  EmulationView + ControllerOverlay                           EmuWindow_iOS, IOSInput factories
  MetalView (CAMetalLayer) ───────────── render surface ──────▶ Core::System
                                                                 └─ VideoCore ─▶ Metal::RendererMetal
```

## Building

You need macOS with Xcode 16 or later. The app targets iOS 16 or later on arm64.

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

The last step writes `artifacts/Neroli-unsigned.ipa`. A sideloading tool (SideStore, AltStore,
Sideloadly, …) can sign it, or you can open `build/ios/citra.xcodeproj`, select the `azahar_ios`
target and sign it with your own team.

If you sign with a free Apple developer account, configure with
`-DAZAHAR_IOS_ENTITLEMENTS=OFF`. The increased memory limit entitlement requires a paid account.

GitHub Actions (`.github/workflows/ios.yml`) builds an unsigned IPA on every push. Before that, it
compiles every shader the Metal backend can generate with Apple's offline Metal compiler.

## Using the app

- Import decrypted games with the **+** button in the Games tab, or copy them into
  **Files › On My iPhone › Neroli › ROMs**.
- System files, saves and the virtual SD card are in **Files › Neroli › Neroli**. This is the
  regular Azahar user directory (a folder named Azahar from earlier versions is moved there
  the first time Neroli starts), so you can copy `sysdata`, `nand` and `sdmc` over from a desktop
  install.
- Supported formats: `.3ds/.cci`, `.cxi`, `.3dsx`, `.app`, `.elf/.axf` and their compressed
  `.z*` variants. CIA installation is not exposed in the UI yet.

### JIT

iOS only allows executable memory while a debugger is attached to the app. When the app is started
with JIT enabled (for example with StikDebug, or from Xcode), Neroli uses the dynarmic ARM
recompiler and the PICA shader JIT. Otherwise it automatically falls back to the interpreters,
which are much slower. The JIT status is shown in **Settings › General**.

## Metal renderer

The backend follows the structure of the Vulkan backend, so it reuses the shared rasterizer cache
and the PICA state handling.

| File | Purpose |
| --- | --- |
| `mtl_instance` | Device, command queue, format table and shader generator profile |
| `mtl_scheduler` | Command buffers and encoders, tick-based GPU synchronization |
| `mtl_stream_buffer` | Ring buffers in unified memory protected by command buffer ticks |
| `mtl_texture_runtime` | Surfaces, framebuffers, samplers, uploads/downloads, copies, blits, clears |
| `mtl_pipeline_cache` | MSL generation and compilation, pipeline and depth/stencil state caches |
| `mtl_rasterizer` | PICA draw state to Metal: textures, LUTs, uniforms, draws |
| `renderer_metal` | Presentation to the `CAMetalLayer`, screen layouts and screenshots |
| `shader/generator/msl_shader_gen` | MSL fragment shader generator (TEV, lighting, fog, proctex, shadows) |

Implementation notes:

- **Conventions.** The backend uses the Vulkan backend's conventions: top-left origin, [0, 1]
  depth, and the same surface memory layout. The only difference is that the vertex shader negates
  Y, because Metal's NDC +Y points up. Culling and scissor logic are therefore identical to Vulkan's.
- **Formats.** All color surfaces are stored as `RGBA8Unorm`, which makes color format
  reinterpretation a copy. D24 is stored as `Depth32Float`. D24S8 is stored as
  `Depth32Float_Stencil8`, with on-the-fly de-interleaving for uploads and downloads.
- **Blending and logic ops.** Every Apple GPU supports framebuffer fetch (`[[color(0)]]`). The
  renderer uses it to emulate all 16 PICA logic ops exactly, and min/max blending with blend
  factors.
- **Shadow rendering** uses a raster-ordered R32Uint view of the shadow map in place of
  fragment shader interlock. Shadow textures are sampled through R32Uint texture views.
- **Custom border colors** are emulated in the fragment shader, because Metal samplers only
  support a fixed set of border colors.

### Status and roadmap

Working in this first version:

- [x] Software vertex shading plus hardware rasterization, covering all fragment pipeline features
- [x] Resolution scaling and custom textures (RGBA8, ASTC, and BCn where the GPU supports it)
- [x] Presentation with every screen layout, including mono and side-by-side 3D modes

Next:

- [ ] Hardware vertex shaders: an MSL backend for the PICA shader decompiler, and quaternion
      fix-up without geometry shaders
- [ ] Persistent pipeline cache (`MTLBinaryArchive`) and asynchronous shader compilation
- [ ] Texture filters (Anime4K, xBRZ, …) as Metal compute passes
- [ ] Anaglyph/interlaced 3D modes, the cursor overlay, and secondary display (AirPlay) output
- [ ] Save states, cheats, CIA installation, amiibo and the system keyboard applet in the UI
- [ ] Customizable on-screen controller layout

## License

Neroli, like Azahar, is licensed under the GNU General Public License, version 3
or any later version (see [LICENSE.txt](../../LICENSE.txt)). Older files of Azahar and Citra are
licensed under the GPLv2 or any later version (see
[misc/licenses/gplv2.txt](../../misc/licenses/gplv2.txt)); each file's header says which applies.

The app also contains third-party code under its own licenses: the libraries in `externals`, the
Rust crates of the idevice library linked into the JIT helper, and the texture filters and the
sharpening of the Metal renderer (Anime4K, ScaleForce and FidelityFX CAS, MIT). Their license
texts are bundled into the app as `Licenses.txt` and shown in Settings › About › Licenses. The
list is made by `src/citra_ios/CMakeLists.txt` from the components' own license files, and by
`.ci/collect-rust-licenses.py` for the crates.

Anyone who is given a build of the app is entitled to its complete corresponding source code
under the GPL; the source of this port is at https://github.com/JMillz269/Azahar-iOS.
