# Native iOS app: a SwiftUI frontend on top of an Objective-C++ bridge to the emulator core.
# Requires the Xcode generator (cmake -G Xcode -DCMAKE_SYSTEM_NAME=iOS ...).

if (NOT CMAKE_GENERATOR STREQUAL "Xcode")
    message(FATAL_ERROR "The iOS app must be configured with the Xcode generator (-G Xcode)")
endif()

enable_language(Swift)

set(AZAHAR_IOS_DEPLOYMENT_TARGET "16.0")
set(AZAHAR_IOS_BUNDLE_ID "org.jmillz269.neroli" CACHE STRING "Bundle identifier of the iOS app")
# The increased memory limit entitlement needs a paid developer account (or a sideloading tool
# that preserves it). Disable it to sign with a free account from Xcode.
option(AZAHAR_IOS_ENTITLEMENTS "Request the increased memory limit entitlements" ON)

# ---------------------------------------------------------------------------------------------
# Objective-C++ bridge to the core, exposed to Swift as the "AzaharCore" clang module.
# ---------------------------------------------------------------------------------------------
set(BRIDGE_OBJCXX_SOURCES
    Bridge/AZConfiguration.mm
    Bridge/AZEmulator.mm
    Bridge/AZFeatures.mm
    Bridge/AZGameInfo.mm
    Bridge/AZJITHelper.mm
    Bridge/AZMetalFXProbe.mm
    Bridge/AZMultiplayer.mm
    Bridge/camera_ios.mm
    Bridge/mic_ios.mm
)

add_library(azahar_ios_bridge STATIC
    ${BRIDGE_OBJCXX_SOURCES}
    Bridge/camera_ios.h
    Bridge/mic_ios.h
    Bridge/emu_window_ios.cpp
    Bridge/emu_window_ios.h
    Bridge/input_ios.cpp
    Bridge/input_ios.h
    Bridge/jit_warmup.cpp
    Bridge/jit_warmup.h
    Bridge/include/AzaharCore/AzaharCore.h
    Bridge/include/AzaharCore/AZConfiguration.h
    Bridge/include/AzaharCore/AZEmulator.h
    Bridge/include/AzaharCore/AZGameInfo.h
    Bridge/include/AzaharCore/AZJITHelper.h
    Bridge/include/AzaharCore/AZMetalFXProbe.h
    Bridge/include/AzaharCore/AZMultiplayer.h
    Bridge/include/AzaharCore/AZFrameGenerator.h
    Bridge/include/AzaharCore/module.modulemap
)

set_source_files_properties(${BRIDGE_OBJCXX_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-fobjc-arc"
    SKIP_PRECOMPILE_HEADERS ON
)

target_include_directories(azahar_ios_bridge
    PUBLIC Bridge/include
    PRIVATE ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(azahar_ios_bridge PRIVATE
    citra_common citra_core video_core audio_core input_common network fmt
    "-framework Foundation" "-framework Metal" "-framework QuartzCore" "-weak_framework MetalFX"
    "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo"
)
# The JIT warm-up (Bridge/jit_warmup.cpp) calls into dynarmic through ARM_Dynarmic, whose header
# includes dynarmic's. citra_core links it privately, so its include path does not reach here.
if (TARGET dynarmic)
    target_link_libraries(azahar_ios_bridge PRIVATE dynarmic)
endif()

set_target_properties(azahar_ios_bridge PROPERTIES
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET ${AZAHAR_IOS_DEPLOYMENT_TARGET}
)

# ---------------------------------------------------------------------------------------------
# SwiftUI application
# ---------------------------------------------------------------------------------------------
set(APP_SWIFT_SOURCES
    App/AzaharApp.swift
    App/ContentView.swift
    App/Core/AppSettings.swift
    App/Core/ConsoleInput.swift
    App/Core/ControllerNavigator.swift
    App/Core/FrameGenerator.swift
    App/Core/JITLauncher.swift
    App/Core/CountryNames.swift
    App/Core/ControllerLayout.swift
    App/Core/CustomStorage.swift
    App/Core/DocumentImporter.swift
    App/Core/ExternalDisplay.swift
    App/Core/EmulationSession.swift
    App/Core/FileLocations.swift
    App/Core/GameCompressor.swift
    App/Core/BundleMaker.swift
    App/Core/GameSettings.swift
    App/Core/GameLibrary.swift
    App/Core/GameStats.swift
    App/Core/LayoutPresets.swift
    App/Core/LogExporter.swift
    App/Core/SaveManager.swift
    App/Core/MultiplayerSession.swift
    App/Views/AmiiboView.swift
    App/Views/ArticView.swift
    App/Views/CheatsView.swift
    App/Views/ControllerMappingView.swift
    App/Views/ControllerOverlay.swift
    App/Views/EmulationView.swift
    App/Views/GameRowView.swift
    App/Views/GameHUD.swift
    App/Views/LiquidGlassDesignSystem.swift
    App/Views/GameSettingsView.swift
    App/Views/GamesListView.swift
    App/Views/KeyboardPromptView.swift
    App/Views/MetalView.swift
    App/Views/MiiSelectorView.swift
    App/Views/MoviesView.swift
    App/Views/MultiplayerView.swift
    App/Views/SaveStatesView.swift
    App/Views/ScreenLayoutEditor.swift
    App/Views/SettingsView.swift
)

add_executable(azahar_ios MACOSX_BUNDLE ${APP_SWIFT_SOURCES})

# The project's compatibility list, shown as a rating in the game library.
set(AZAHAR_COMPAT_LIST "${PROJECT_SOURCE_DIR}/dist/compatibility_list/compatibility_list.json")
target_sources(azahar_ios PRIVATE ${AZAHAR_COMPAT_LIST})
set_source_files_properties(${AZAHAR_COMPAT_LIST} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

# The app icon (the Azahar flower), as plain PNGs named for CFBundleIcons.
file(GLOB AZAHAR_ICON_FILES "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Icons/*.png")
target_sources(azahar_ios PRIVATE ${AZAHAR_ICON_FILES})
set_source_files_properties(${AZAHAR_ICON_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

# The same icon as an asset catalog (compiled to Assets.car, the way iOS 26 expects app icons).
set(AZAHAR_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Assets.xcassets")
target_sources(azahar_ios PRIVATE ${AZAHAR_ASSETS})
set_source_files_properties(${AZAHAR_ASSETS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

include(GenerateBuildInfo)
set(SRC_DIR "${PROJECT_SOURCE_DIR}")
generate_build_info()

# The app's version as iOS wants it: numbers separated by periods. A release tag such as v0.1.0
# becomes 0.1.0 for both version keys; a build without a tag keeps the commit as its displayed
# version and 0 as its build number, as before.
set(AZAHAR_IOS_SHORT_VERSION "${BUILD_FULLNAME}")
set(AZAHAR_IOS_BUNDLE_VERSION "${BUILD_VERSION}")
if (BUILD_VERSION STREQUAL BUILD_FULLNAME AND BUILD_VERSION MATCHES "^[vV]?([0-9]+(\\.[0-9]+)*)")
    set(AZAHAR_IOS_SHORT_VERSION "${CMAKE_MATCH_1}")
    set(AZAHAR_IOS_BUNDLE_VERSION "${CMAKE_MATCH_1}")
endif()
message(STATUS "iOS app version ${AZAHAR_IOS_SHORT_VERSION} (build ${AZAHAR_IOS_BUNDLE_VERSION})")

set_target_properties(azahar_ios PROPERTIES
    OUTPUT_NAME "Neroli"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME "Neroli"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${AZAHAR_IOS_BUNDLE_ID}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${AZAHAR_IOS_BUNDLE_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${AZAHAR_IOS_SHORT_VERSION}"
    MACOSX_BUNDLE_LONG_VERSION_STRING "${BUILD_FULLNAME}"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${AZAHAR_IOS_BUNDLE_ID}"
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET ${AZAHAR_IOS_DEPLOYMENT_TARGET}
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
    XCODE_ATTRIBUTE_SWIFT_VERSION "5.0"
    XCODE_ATTRIBUTE_SWIFT_INCLUDE_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/Bridge/include"
    XCODE_ATTRIBUTE_SWIFT_OPTIMIZATION_LEVEL "$<IF:$<CONFIG:Debug>,-Onone,-O>"
    XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@executable_path/Frameworks"
    XCODE_ATTRIBUTE_ENABLE_BITCODE NO
    XCODE_ATTRIBUTE_DEAD_CODE_STRIPPING YES
)

if (AZAHAR_IOS_ENTITLEMENTS)
    set_target_properties(azahar_ios PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/Azahar.entitlements")
endif()

# The core is compiled with flags and definitions that only make sense for C/C++ (warnings,
# fortification, -stdlib=libc++, NAME=VALUE definitions). Keep them away from swiftc.
set_property(TARGET azahar_ios PROPERTY COMPILE_OPTIONS "")
set_property(TARGET azahar_ios PROPERTY COMPILE_DEFINITIONS "")
# src/ is a global include directory. On the case-insensitive macOS file system its
# network/network.h shadows the SDK's <Network/Network.h>, which breaks importing AVFoundation.
set_property(TARGET azahar_ios PROPERTY INCLUDE_DIRECTORIES "")

# ---------------------------------------------------------------------------------------------
# JIT helper: an app extension that enables JIT for the app from outside (an app cannot attach a
# debugger to itself). Its debugging part is the idevice library, built by .ci/build-idevice.sh
# into build/idevice-out; without it the extension is still built and says so.
# ---------------------------------------------------------------------------------------------
option(AZAHAR_IOS_JIT_HELPER "Embed the JIT helper app extension" ON)
set(AZAHAR_IDEVICE_DIR "${PROJECT_SOURCE_DIR}/build/idevice-out" CACHE PATH
    "Where .ci/build-idevice.sh put the idevice library")

if (AZAHAR_IOS_JIT_HELPER)
    add_executable(azahar_jit_helper MACOSX_BUNDLE JITHelper/main.m)
    set_target_properties(azahar_jit_helper PROPERTIES
        OUTPUT_NAME "AzaharJITHelper"
        XCODE_PRODUCT_TYPE "com.apple.product-type.app-extension"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/JITHelper/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Neroli JIT Helper"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${AZAHAR_IOS_BUNDLE_ID}.jit-helper"
        MACOSX_BUNDLE_BUNDLE_VERSION "${AZAHAR_IOS_BUNDLE_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${AZAHAR_IOS_SHORT_VERSION}"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${AZAHAR_IOS_BUNDLE_ID}.jit-helper"
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET ${AZAHAR_IOS_DEPLOYMENT_TARGET}
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_SKIP_INSTALL YES
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS
            "${CMAKE_CURRENT_SOURCE_DIR}/JITHelper/JITHelper.entitlements"
        XCODE_ATTRIBUTE_APPLICATION_EXTENSION_API_ONLY YES
        XCODE_ATTRIBUTE_ENABLE_BITCODE NO
    )
    # Keep the core's C++ flags and include paths away from it (see azahar_ios below).
    set_property(TARGET azahar_jit_helper PROPERTY COMPILE_OPTIONS "")
    set_property(TARGET azahar_jit_helper PROPERTY COMPILE_DEFINITIONS "")
    set_property(TARGET azahar_jit_helper PROPERTY INCLUDE_DIRECTORIES "")
    # An extension starts at NSExtensionMain, not main.
    target_link_options(azahar_jit_helper PRIVATE -e _NSExtensionMain -fapplication-extension)
    target_link_libraries(azahar_jit_helper PRIVATE "-framework Foundation")

    if (EXISTS "${AZAHAR_IDEVICE_DIR}/lib/libidevice_ffi.a")
        message(STATUS "JIT helper: linking ${AZAHAR_IDEVICE_DIR}/lib/libidevice_ffi.a")
        target_link_libraries(azahar_jit_helper PRIVATE
            "-Wl,-force_load,${AZAHAR_IDEVICE_DIR}/lib/libidevice_ffi.a")
        target_include_directories(azahar_jit_helper PRIVATE "${AZAHAR_IDEVICE_DIR}/include")
        target_compile_definitions(azahar_jit_helper PRIVATE AZ_HAVE_IDEVICE=1)
        set_property(TARGET azahar_jit_helper APPEND PROPERTY COMPILE_DEFINITIONS AZ_HAVE_IDEVICE=1)
    else()
        message(STATUS "JIT helper: no idevice library in ${AZAHAR_IDEVICE_DIR}, built without it")
    endif()

    # Embedded in the app's PlugIns folder.
    set_property(TARGET azahar_ios PROPERTY XCODE_EMBED_APP_EXTENSIONS azahar_jit_helper)
    add_dependencies(azahar_ios azahar_jit_helper)
endif()

target_link_libraries(azahar_ios PRIVATE azahar_ios_bridge "-lc++")

# ---------------------------------------------------------------------------------------------
# Licenses.txt: the license of Azahar and of every third-party component built into the app,
# shown in Settings › About › Licenses. Each entry is a line "@@@ name", a line saying what the
# license is and where the code comes from, then the full text. The texts are read from the
# components' own license files at configure time, so they always match the code that is built.
# ---------------------------------------------------------------------------------------------
set(AZAHAR_LICENSES_FILE "${CMAKE_CURRENT_BINARY_DIR}/Licenses.txt")
file(WRITE "${AZAHAR_LICENSES_FILE}" "")

function(azahar_license name summary path)
    set(full "${PROJECT_SOURCE_DIR}/${path}")
    if (NOT EXISTS "${full}")
        message(WARNING "Licenses.txt: ${path} not found, ${name} left out")
        return()
    endif()
    file(READ "${full}" text)
    string(STRIP "${text}" text)
    file(APPEND "${AZAHAR_LICENSES_FILE}" "@@@ ${name}\n${summary}\n${text}\n")
endfunction()

# For code whose license is only named in its source files (an SPDX tag or "MIT License" with a
# copyright line), the standard MIT text with that copyright line.
set(AZAHAR_MIT_TEXT [=[Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.]=])
function(azahar_license_mit name summary copyright)
    file(APPEND "${AZAHAR_LICENSES_FILE}"
        "@@@ ${name}\n${summary}\nCopyright (c) ${copyright}\n\n${AZAHAR_MIT_TEXT}\n")
endfunction()

azahar_license("Neroli, based on Azahar" "GPL-3.0-or-later. The source code of this app: https://github.com/JMillz269/Neroli"
    "LICENSE.txt")
azahar_license("Azahar (parts under GPLv2 or later)"
    "GPL-2.0-or-later. Older files of Azahar and Citra are licensed under the GPLv2 or any later version"
    "misc/licenses/gplv2.txt")
azahar_license("Boost" "BSL-1.0, https://www.boost.org" "externals/boost/LICENSE_1_0.txt")
azahar_license("Crypto++" "BSL-1.0, https://cryptopp.com" "externals/cryptopp/LICENSE")
azahar_license("dynarmic" "0BSD, https://github.com/azahar-emu/dynarmic" "externals/dynarmic/LICENSE.txt")
azahar_license("mcl" "MIT, https://github.com/azahar-emu/mcl (part of dynarmic)"
    "externals/dynarmic/externals/mcl/LICENSE")
azahar_license("robin-map" "MIT, https://github.com/Tessil/robin-map (part of dynarmic)"
    "externals/dynarmic/externals/robin-map/LICENSE")
azahar_license("oaknut" "MIT, https://github.com/merryhime/oaknut" "externals/oaknut/LICENSE")
azahar_license("ENet" "MIT, http://enet.bespin.org" "externals/enet/LICENSE")
azahar_license("FAAD2" "GPL-2.0-or-later, https://github.com/knik0/faad2" "externals/faad2/faad2/COPYING")
azahar_license("{fmt}" "MIT, https://github.com/fmtlib/fmt" "externals/fmt/LICENSE")
azahar_license("inih" "BSD-3-Clause, https://github.com/benhoyt/inih" "externals/inih/inih/LICENSE.txt")
azahar_license("LibreSSL" "OpenSSL, SSLeay, ISC and other licenses, https://www.libressl.org"
    "externals/libressl/COPYING")
azahar_license("LodePNG" "Zlib, https://github.com/lvandeve/lodepng" "externals/lodepng/lodepng/LICENSE")
azahar_license("microtar" "MIT, https://github.com/rxi/microtar" "externals/microtar/LICENSE")
azahar_license("nihstro" "BSD-3-Clause, https://github.com/neobrain/nihstro" "externals/nihstro/license.txt")
azahar_license("OpenAL Soft" "LGPL-2.0-or-later, https://openal-soft.org" "externals/openal-soft/COPYING")
azahar_license("SoundTouch" "LGPL-2.1, https://www.surina.net/soundtouch" "externals/soundtouch/COPYING.TXT")
azahar_license("Teakra" "MIT, https://github.com/wwylele/teakra" "externals/teakra/LICENSE")
azahar_license("xxHash" "BSD-2-Clause, https://github.com/Cyan4973/xxHash" "externals/xxHash/LICENSE")
azahar_license("Zstandard" "BSD-3-Clause, https://github.com/facebook/zstd" "externals/zstd/LICENSE")
azahar_license("dds-ktx" "BSD-2-Clause, https://github.com/septag/dds-ktx" "externals/dds-ktx/LICENSE")
azahar_license_mit("JSON for Modern C++" "MIT, https://github.com/nlohmann/json"
    "2013-2022 Niels Lohmann")
azahar_license_mit("cpp-httplib" "MIT, https://github.com/yhirose/cpp-httplib" "2023 Yuji Hirose")
azahar_license_mit("Anime4K (texture filter)" "MIT, https://github.com/bloc97/Anime4K" "2019 bloc97")
azahar_license_mit("ScaleForce (texture filter)"
    "MIT, https://github.com/BreadFish64/ScaleFish/tree/master/scale_force" "2020 BreadFish64")
azahar_license_mit("FidelityFX CAS (sharpening)" "MIT, https://github.com/GPUOpen-Effects/FidelityFX-CAS"
    "2017-2019 Advanced Micro Devices, Inc. All rights reserved.")

# idevice and the Rust crates linked into the JIT helper, collected by .ci/build-idevice.sh.
if (AZAHAR_IOS_JIT_HELPER AND EXISTS "${AZAHAR_IDEVICE_DIR}/lib/libidevice_ffi.a")
    if (EXISTS "${AZAHAR_IDEVICE_DIR}/licenses.txt")
        file(READ "${AZAHAR_IDEVICE_DIR}/licenses.txt" idevice_licenses)
        file(APPEND "${AZAHAR_LICENSES_FILE}" "${idevice_licenses}")
    else()
        message(WARNING "Licenses.txt: ${AZAHAR_IDEVICE_DIR}/licenses.txt not found, the licenses "
                        "of idevice and its crates are left out")
    endif()
endif()

target_sources(azahar_ios PRIVATE "${AZAHAR_LICENSES_FILE}")
set_source_files_properties("${AZAHAR_LICENSES_FILE}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
