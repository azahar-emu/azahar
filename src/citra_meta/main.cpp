// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <iostream>

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
#if defined(_WIN32)
#include <windows.h>
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif // _MSC_VER
#else
#include <cpuid.h>
#endif // _WIN32

static bool CpuSupportsSSE42() {
    uint32_t ecx;

#if defined(_MSC_VER)
    int cpu_info[4];
    __cpuid(cpu_info, 1);
    ecx = static_cast<uint32_t>(cpu_info[2]);
#elif defined(__GNUC__) || defined(__clang__)
    uint32_t eax, ebx, edx;
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        return false;
    }
#else
#error "Unsupported compiler"
#endif

    // Bit 20 of ECX indicates SSE4.2
    return (ecx & (1 << 20)) != 0;
}

static bool CheckAndReportSSE42() {
    if (!CpuSupportsSSE42()) {
        const std::string error_msg =
            "This application requires a CPU with SSE4.2 support or higher.\nTo run on unsupported "
            "systems, recompile the application with the ENABLE_SSE42 option disabled.";
#if defined(_WIN32)
        MessageBoxA(nullptr, error_msg.c_str(), "Incompatible CPU", MB_OK | MB_ICONERROR);
#endif
        std::cerr << "Error: " << error_msg << std::endl;
        return false;
    }
    return true;
}
#endif

int main(int argc, char* argv[]) {
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
        LaunchRoom(argc, argv, true);
        return 0;
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
        LaunchQtFrontend(argc, argv);
        return 0;
    }
#endif

#if ENABLE_SDL2_FRONTEND
    LaunchSdlFrontend(argc, argv);
#else
    std::cout << "Cannot use SDL frontend as it was disabled at compile time. Exiting."
              << std::endl;
    return -1;
#endif

    return 0;
}
