# Network Streaming (bottom screen to another device)

This fork adds an in-game menu option, **"Stream Bottom Screen to Device…"**,
that streams the emulated 3DS bottom screen live to another Android phone on
the same Wi-Fi network, with touch input forwarded back so the second phone
works as a real second touchscreen.

Companion receiver app (install on the second phone):
https://github.com/Ufoex/azahar-viewer

## Usage

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

Two small upstream bugs were fixed along the way (separate commits, useful
independently of this feature):

- `Config::Reload()` validated its omitted-settings-keys list by pointer
  identity instead of string content, which made it assert on the very
  first settings reload on any fresh Android install.
- `UpdateCurrentFramebufferLayout()` clamped a secondary Android window's
  size to the *primary* layout's minimum, inflating it and breaking
  touch-to-framebuffer coordinate mapping for any secondary window smaller
  than that minimum.
