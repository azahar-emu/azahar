add_executable(lime
    lime.rc
    main.cpp
    precompiled_headers.h
)

set_target_properties(lime PROPERTIES OUTPUT_NAME "lime3ds")

if (APPLE)
    set(DIST_DIR "../../dist/apple")
    set(APPLE_RESOURCES
        "${DIST_DIR}/lime.icns"
        "${DIST_DIR}/LaunchScreen.storyboard"
        "${DIST_DIR}/launch_logo.png"
    )
    target_sources(lime PRIVATE ${APPLE_RESOURCES})

    # Define app bundle metadata.
    include(GenerateBuildInfo)
    set_target_properties(lime PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${DIST_DIR}/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Lime3DS"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.lime3ds.Lime3DS"
        MACOSX_BUNDLE_BUNDLE_VERSION "${BUILD_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${BUILD_FULLNAME}"
        MACOSX_BUNDLE_LONG_VERSION_STRING "${BUILD_FULLNAME}"
        MACOSX_BUNDLE_ICON_FILE "lime.icns"
        RESOURCE "${APPLE_RESOURCES}"
    )
endif()

target_link_libraries(lime PRIVATE fmt)

if (ENABLE_SDL2_FRONTEND)
    target_link_libraries(lime PRIVATE lime_sdl)
endif()

if (ENABLE_QT)
    target_link_libraries(lime PRIVATE lime_qt)
    target_link_libraries(lime PRIVATE Boost::boost Qt6::Widgets)
endif()

if (ENABLE_QT AND UNIX AND NOT APPLE)
    target_link_libraries(lime PRIVATE Qt6::DBus gamemode)
endif()

if (ENABLE_QT AND USE_DISCORD_PRESENCE)
    target_link_libraries(lime PRIVATE discord-rpc)
endif()

if(WIN32)
    # compile as a win32 gui application instead of a console application
    if(MSVC)
        set_target_properties(lime PROPERTIES LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS")
    elseif(MINGW)
        set_target_properties(lime PROPERTIES LINK_FLAGS_RELEASE "-mwindows")
    endif()
endif()

if (LIME3DS_USE_PRECOMPILED_HEADERS)
    target_precompile_headers(lime PRIVATE precompiled_headers.h)
endif()

# Bundle in-place on MSVC so dependencies can be resolved by builds.
if (ENABLE_QT AND MSVC)
    include(BundleTarget)
    qt_bundle_target_in_place(lime)
endif()

if(UNIX AND NOT APPLE)
    install(TARGETS lime RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
endif()
