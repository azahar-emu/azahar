// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>

namespace Common {

constexpr char help_string[] =
    "Usage: {} [options] <file path>\n"
    "-d, --dump-video [path]     Dump video recording of emulator playback to the given file path\n"
    "-f, --fullscreen            Start in fullscreen mode\n"
    "-g, --gdbport [port]        Enable gdb stub on the given port\n"
    "-h, --help                  Display this help and exit\n"
    "-i, --install [path]        Install a CIA file at the given path\n"
    "-p, --movie-play [path]     Play a TAS movie located at the given path\n"
    "-r, --movie-record [path]   Record a TAS movie to the given file path\n"
    "-a, --movie-record-author [author]   Set the author for the recorded TAS movie (to be used "
    "alongside --movie-record)\n"
#ifdef ENABLE_SDL2_FRONTEND
    "-n, --no-gui                Use the lightweight SDL frontend instead of the usual Qt "
    "frontend\n"
    // TODO: Move -m outside of this check when it is implemented in Qt frontend
    "-m, --multiplayer [nick:password@address:port]   Nickname, password, address and port for "
    "multiplayer (currently only usable with SDL frontend)\n"
#endif
#ifdef ENABLE_ROOM
    "    --room                  Utilize dedicated multiplayer room functionality (equivalent to "
    "the old citra-room executable)\n"
#endif
    "-v, --version               Output version information and exit\n"
    "-w, --windowed              Start in windowed mode";

} // namespace Common
