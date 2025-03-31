set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR}/CMakeModules)

add_library(citra_room STATIC EXCLUDE_FROM_ALL
    precompiled_headers.h
    citra_room.cpp
    citra_room.h
    citra_room.rc
)

create_target_directory_groups(citra_room)

target_link_libraries(citra_room PRIVATE citra_common network)
if (ENABLE_WEB_SERVICE)
    target_link_libraries(citra_room PRIVATE web_service)
endif()

target_link_libraries(citra_room PRIVATE cryptopp)
if (MSVC)
    target_link_libraries(citra_room PRIVATE getopt)
endif()
target_link_libraries(citra_room PRIVATE ${PLATFORM_LIBRARIES} Threads::Threads)

if(UNIX AND NOT APPLE)
    install(TARGETS citra_room RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
endif()

if (CITRA_USE_PRECOMPILED_HEADERS)
    target_precompile_headers(citra_room PRIVATE precompiled_headers.h)
endif()

# Bundle in-place on MSVC so dependencies can be resolved by builds.
if (MSVC)
    include(BundleTarget)
    bundle_target_in_place(citra_room)
endif()
