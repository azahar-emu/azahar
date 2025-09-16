add_executable(citra_meta
    citra.rc
    main.cpp
    precompiled_headers.h
)

set_target_properties(citra_meta PROPERTIES OUTPUT_NAME "azahar")

if (APPLE)
    set(DIST_DIR "../../dist/apple")
    set(APPLE_RESOURCES
        "${DIST_DIR}/azahar.icns"
        "${DIST_DIR}/LaunchScreen.storyboard"
        "${DIST_DIR}/launch_logo.png"
    )
    target_sources(citra_meta PRIVATE ${APPLE_RESOURCES})

    # Define app bundle metadata.
    include(GenerateBuildInfo)
    set_target_properties(citra_meta PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${DIST_DIR}/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Azahar"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.azahar-emu.azahar"
        MACOSX_BUNDLE_BUNDLE_VERSION "${BUILD_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${BUILD_FULLNAME}"
        MACOSX_BUNDLE_LONG_VERSION_STRING "${BUILD_FULLNAME}"
        MACOSX_BUNDLE_ICON_FILE "azahar.icns"
        RESOURCE "${APPLE_RESOURCES}"
    )

    if (IOS)
        set_target_properties(citra_meta PROPERTIES
            # Have Xcode copy and sign MoltenVK into app bundle.
            XCODE_EMBED_FRAMEWORKS "${MOLTENVK_LIBRARY}"
            XCODE_EMBED_FRAMEWORKS_CODE_SIGN_ON_COPY YES
            XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@executable_path/Frameworks"
            # Support iPhone and iPad.
            XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        )
    endif()
endif()

target_link_libraries(citra_meta PRIVATE fmt)

if (ENABLE_SDL2_FRONTEND)
    target_link_libraries(citra_meta PRIVATE citra_sdl)
endif()

if (ENABLE_QT)
    target_link_libraries(citra_meta PRIVATE citra_qt)
    target_link_libraries(citra_meta PRIVATE Boost::boost Qt6::Widgets)
endif()

if (ENABLE_ROOM)
    target_link_libraries(citra_meta PRIVATE citra_room)
endif()

if (ENABLE_QT AND UNIX AND NOT APPLE)
    target_link_libraries(citra_meta PRIVATE Qt6::DBus gamemode)
endif()

if (ENABLE_QT AND APPLE)
    target_link_libraries(citra_meta PRIVATE Qt6::GuiPrivate)
endif()

if (ENABLE_QT AND USE_DISCORD_PRESENCE)
    target_link_libraries(citra_meta PRIVATE discord-rpc)
endif()

if(WIN32)
    # compile as a win32 gui application instead of a console application
    if(MSVC)
        set_target_properties(citra_meta PROPERTIES LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS")
    elseif(MINGW)
        set_target_properties(citra_meta PROPERTIES LINK_FLAGS_RELEASE "-mwindows")
    endif()
endif()

if (CITRA_USE_PRECOMPILED_HEADERS)
    target_precompile_headers(citra_meta PRIVATE precompiled_headers.h)
endif()

if (SSE42_COMPILE_OPTION)
    target_compile_definitions(citra_meta PRIVATE CITRA_HAS_SSE42)
endif()

# Bundle in-place on MSVC so dependencies can be resolved by builds.
if (ENABLE_QT AND MSVC)
    include(BundleTarget)
    qt_bundle_target_in_place(citra_meta)
endif()

if(UNIX AND NOT APPLE)
    install(TARGETS citra_meta RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
endif()
