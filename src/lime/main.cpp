// Copyright Citra Emulator Project / Lime3DS Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <iostream>

#ifdef ENABLE_QT
#include "lime_qt/lime_qt.h"
#endif
#ifdef ENABLE_SDL2_FRONTEND
#include "lime_sdl/lime_sdl.h"
#endif

#ifdef _WIN32
extern "C" {
// tells Nvidia drivers to use the dedicated GPU by default on laptops with switchable graphics
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
}
#endif

int main(int argc, char* argv[]) {
#if ENABLE_QT
    bool no_gui = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-gui") == 0 || strcmp(argv[i], "-n") == 0) {
            no_gui = true;
        }
    }

    if (!no_gui) {
        LaunchQtFrontend(argc, argv);
        return 0;
    }
#endif

#if ENABLE_SDL2_FRONTEND
    LaunchSdlFrontend(argc, argv);
#else
    std::cout << "Cannot use SDL frontend as it was excluded at compile time. Exiting."
              << std::endl;
    return -1;
#endif

    return 0;
}