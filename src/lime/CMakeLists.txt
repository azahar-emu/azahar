add_executable(lime
    lime.rc
    main.cpp
)

set_target_properties(lime PROPERTIES OUTPUT_NAME "lime3ds")

if (ENABLE_SDL2_FRONTEND)
    target_link_libraries(lime PRIVATE lime_sdl)
endif()

if (ENABLE_QT)
    target_link_libraries(lime PRIVATE lime_qt)
    target_link_libraries(lime PRIVATE Boost::boost fmt Qt6::Widgets)
endif()

if (ENABLE_QT AND UNIX AND NOT APPLE)
    target_link_libraries(lime PRIVATE Qt6::DBus gamemode)
endif()

# Bundle in-place on MSVC so dependencies can be resolved by builds.
if (ENABLE_QT AND MSVC)
    include(BundleTarget)
    qt_bundle_target_in_place(lime)
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