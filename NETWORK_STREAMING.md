# Network Streaming (bottom screen to another device)

This fork adds an in-game menu option, **"Stream Bottom Screen to Device…"**,
that streams the emulated 3DS bottom screen live to another Android phone on
the same Wi-Fi network, with touch input forwarded back so the second phone
works as a real second touchscreen. Available on **Android** and, as of this
release, on **Linux desktop** too - both talk the exact same wire protocol,
so a single Android Viewer install works as the receiver for either.

Companion receiver app (install on the second phone):
https://github.com/Ufoex/azahar-viewer

## Usage (Android sender)

1. Build and install this fork on the phone running the emulator.
2. Install [Azahar Viewer](https://github.com/Ufoex/azahar-viewer) on a
   second phone, same Wi-Fi network.
3. Start a game, open the in-game menu, tap **"Stream Bottom Screen to
   Device…"**.
4. Open Azahar Viewer on the second phone — it lists any streaming Azahar
   instance on the network automatically (via mDNS/NSD, no IP typing) — tap
   it to connect.
5. Touching the second phone's screen sends touch input back to the
   emulator, same as touching the 3DS's own bottom screen.

Toggle the same menu entry again ("Stop Streaming Bottom Screen") to turn it
off.

## Usage (Linux desktop sender)

The Linux Qt build has the same **Tools → Stream Bottom Screen to Device**
menu entry. Steps are identical to the Android flow above: start a game,
toggle the menu entry, open Azahar Viewer on a phone on the same Wi-Fi, tap
the discovered device.

### Requirements

- A recent Debian/Ubuntu-family desktop (built and tested on Ubuntu 26.04).
- Qt6 runtime (Widgets, Multimedia, Concurrent, Gui, DBus, Core, Network) -
  already present on any system with a Qt6 desktop environment or Qt6 apps
  installed.
- `libavahi-client3` / `libavahi-common3` (Avahi mDNS/DNS-SD), used for the
  same auto-discovery Android's NSD gives it. Install with
  `sudo apt install libavahi-client3` if missing.
- **FFmpeg 8.0.x runtime libraries** specifically (`libavutil.so.60`,
  `libavcodec.so.62`, `libavformat.so.62`, `libavfilter.so.11`,
  `libswresample.so.6`). Azahar loads FFmpeg dynamically at runtime and
  checks the major version of each library strictly - a prebuilt binary
  only works against the exact FFmpeg major version it was built against.
  Check yours with `ffmpeg -version`. If your distro ships an older FFmpeg
  (7.x or earlier), build from source instead (see note below).
- A firewall allowing inbound TCP port **27500** and UDP port **5353**
  (mDNS) on your LAN interface, if you have one enabled:
  `sudo ufw allow 27500/tcp && sudo ufw allow 5353/udp`.

### Wayland note

Video dumping (and this streaming feature) shares Azahar's OpenGL context
with a second thread for GPU→CPU frame readback. Under native Wayland this
has been observed to crash on at least Mesa/Intel (an `EGL_BAD_ACCESS`
inside `FrameDumperOpenGL::PresentLoop`) - a pre-existing issue in Azahar's
own GL context sharing, not specific to this feature. To work around it,
this build automatically re-execs itself through XWayland (X11) whenever it
detects a Wayland session (`WAYLAND_DISPLAY` set) and you haven't already
forced a platform via `QT_QPA_PLATFORM` yourself. Requires Xwayland, which
every mainstream Wayland desktop (GNOME, KDE, etc.) ships by default. Native
X11 sessions are entirely unaffected by this. Normal gameplay performance
under XWayland vs. native Wayland is not meaningfully different for this
kind of low-native-resolution workload.

### Building from source on a different FFmpeg version

The `externals/library-headers` submodule pins the FFmpeg header versions
this build was compiled against. If your system's FFmpeg runtime major
versions differ from the ones listed above, either install a matching
FFmpeg, or update that submodule to a header set matching your installed
FFmpeg before building (`cd externals/library-headers && git fetch && git
checkout <matching commit>`).

## How it works

- The core already renders a chosen screen layout into any `Surface` handed
  to it via the secondary-window path (the same one used to pop the bottom
  screen out to a real second monitor). `NetworkStreamer` hands it the input
  `Surface` of a hardware H.264 encoder (`MediaCodec`, Surface input) instead
  of a `SurfaceView`, so the GPU renders straight into the encoder with no
  CPU round-trip.
- The encoded stream is served over a small length-prefixed TCP protocol
  (each chunk: 4-byte size, 4-byte `MediaCodec` buffer flags, payload),
  advertised via NSD/mDNS as `_azahar._tcp.` so viewers discover it by name.
- Touch events come back over the same socket (1 byte action + 2
  floats normalized to `[0,1]`) and feed the existing
  `onSecondaryTouchEvent`/`onSecondaryTouchMoved` JNI hooks.

Relevant files: `src/android/app/src/main/java/org/citra/citra_emu/display/NetworkStreamer.kt`.

## Status / limitations

Personal project, not hardened for anything beyond home use on a trusted
network:

- Single viewer at a time (a new connection replaces the previous one).
- No authentication or encryption - anyone on the same LAN can connect and
  see the stream / send touch input while streaming is enabled.
- Fixed port (27500), fixed resolution (320x240, matching the 3DS bottom
  screen's native resolution).

A few small upstream bugs were fixed along the way (separate commits, useful
independently of this feature):

- `Config::Reload()` validated its omitted-settings-keys list by pointer
  identity instead of string content, which made it assert on the very
  first settings reload on any fresh Android install.
- `UpdateCurrentFramebufferLayout()` clamped a secondary Android window's
  size to the *primary* layout's minimum, inflating it and breaking
  touch-to-framebuffer coordinate mapping for any secondary window smaller
  than that minimum.
- `AV_OPT_TYPE_CHANNEL_LAYOUT` (used by the video-dumping option dialogs)
  was removed from FFmpeg some versions ago; the dumping code didn't
  compile against current FFmpeg headers without guarding/removing it.

Linux-specific notes:

- Registering a `VideoDumper::Backend` alone does not start GPU→CPU frame
  capture - `RendererBase::PrepareVideoDumping()`/`CleanupVideoDumping()`
  must be called explicitly, same as the stock "Dump Video" feature does.
- The `use_skip_duplicate_frames` setting (on by default) starves the video
  dumping mailbox of frames, because the main window's own render call
  consumes the "frame updated" flag before the dumper's call gets to check
  it. The streaming feature disables this setting for its own duration and
  restores it afterward.
