// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <iostream>

#include "common/detached_tasks.h"
#include "common/scope_exit.h"

#ifdef ENABLE_QT
#include "citra_qt/citra_qt.h"
#endif
#ifdef ENABLE_ROOM
#include "citra_room/citra_room.h"
#endif
#ifdef ENABLE_SDL2_FRONTEND
#include "citra_sdl/citra_sdl.h"
#endif

#ifdef _WIN32
extern "C" {
// tells Nvidia drivers to use the dedicated GPU by default on laptops with switchable graphics
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
}
#endif

#if CITRA_HAS_SSE42
#include "common/x64/cpu_detect.h"
#ifdef _WIN32
#include <windows.h>
#endif

static bool CheckAndReportSSE42() {
    const auto& caps = Common::GetCPUCaps();
    if (!caps.sse4_2) {
        const std::string error_msg =
            "This application requires a CPU with SSE4.2 support or higher.\nTo run on unsupported "
            "systems, recompile the application with the ENABLE_SSE42 option disabled.";
#ifdef _WIN32
        MessageBoxA(nullptr, error_msg.c_str(), "Incompatible CPU", MB_OK | MB_ICONERROR);
#endif
        std::cerr << "Error: " << error_msg << std::endl;
        return false;
    }
    return true;
}
#endif

int main(int argc, char* argv[]) {
    Common::DetachedTasks detached_tasks;
    SCOPE_EXIT({ detached_tasks.WaitForAllTasks(); });

#if CITRA_HAS_SSE42
    if (!CheckAndReportSSE42()) {
        return 1;
    }
#endif

#if ENABLE_ROOM
    bool launch_room = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--room") == 0) {
            launch_room = true;
        }
    }

    if (launch_room) {
        return LaunchRoom(argc, argv, true);
    }
#endif

#if ENABLE_QT
    bool no_gui = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-gui") == 0 || strcmp(argv[i], "-n") == 0) {
            no_gui = true;
        }
    }

    if (!no_gui) {
        return LaunchQtFrontend(argc, argv);
    }
#endif

#if ENABLE_SDL2_FRONTEND
    return LaunchSdlFrontend(argc, argv);
#else
    std::cout << "Cannot use SDL frontend as it was disabled at compile time. Exiting."
              << std::endl;
    return -1;
#endif

    return 0;
}
