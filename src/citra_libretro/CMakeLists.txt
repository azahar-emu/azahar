set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/$<CONFIG>)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR}/CMakeModules)

# Object library for libretro code (can be linked into both shared lib and tests)
add_library(azahar_libretro_common OBJECT
        emu_window/libretro_window.cpp
        emu_window/libretro_window.h
        input/input_factory.cpp
        input/input_factory.h
        input/mouse_tracker.cpp
        input/mouse_tracker.h
        $<$<BOOL:${ENABLE_VULKAN}>: libretro_vk.cpp libretro_vk.h>
        environment.cpp
        environment.h
        core_settings.cpp
        core_settings.h)

target_compile_definitions(azahar_libretro_common PRIVATE HAVE_LIBRETRO)
target_link_libraries(azahar_libretro_common PRIVATE citra_common citra_core video_core libretro robin_map)
if(ENABLE_OPENGL)
    target_link_libraries(azahar_libretro_common PRIVATE glad)
endif()
if(ENABLE_VULKAN)
    target_link_libraries(azahar_libretro_common PRIVATE sirit vulkan-headers vma)
endif()

add_library(azahar_libretro SHARED
        citra_libretro.cpp
        citra_libretro.h
        $<TARGET_OBJECTS:azahar_libretro_common>)

create_target_directory_groups(azahar_libretro)

target_link_libraries(citra_common PRIVATE libretro)
target_link_libraries(citra_core PRIVATE libretro)
target_link_libraries(video_core PRIVATE libretro)
target_link_libraries(audio_core PRIVATE libretro)
target_link_libraries(input_common PRIVATE libretro)
target_compile_definitions(citra_common PRIVATE HAVE_LIBRETRO)
target_compile_definitions(citra_core PRIVATE HAVE_LIBRETRO)
target_compile_definitions(video_core PRIVATE HAVE_LIBRETRO)
target_compile_definitions(audio_core PRIVATE HAVE_LIBRETRO)
target_compile_definitions(input_common PRIVATE HAVE_LIBRETRO)

target_link_libraries(azahar_libretro PRIVATE citra_common citra_core)
target_link_libraries(azahar_libretro PRIVATE boost dds-ktx libretro robin_map)
if(ENABLE_VULKAN)
  target_link_libraries(azahar_libretro PRIVATE sirit vulkan-headers vma)
endif()
if(ENABLE_OPENGL)
  target_link_libraries(azahar_libretro PRIVATE glad)
endif()
target_link_libraries(azahar_libretro PRIVATE ${PLATFORM_LIBRARIES} Threads::Threads)
if(DEFINED LIBRETRO_STATIC)
  target_link_libraries(azahar_libretro PRIVATE -static-libstdc++)
endif()

set_target_properties(azahar_libretro PROPERTIES PREFIX "")
target_compile_definitions(azahar_libretro PRIVATE HAVE_LIBRETRO)

if(ANDROID)
  target_compile_definitions(citra_common PRIVATE HAVE_LIBRETRO_VFS)
  target_compile_definitions(citra_core PRIVATE HAVE_LIBRETRO_VFS)
  target_compile_definitions(video_core PRIVATE HAVE_LIBRETRO_VFS)
  target_compile_definitions(azahar_libretro_common PRIVATE USING_GLES HAVE_LIBRETRO_VFS)
  target_compile_definitions(azahar_libretro PRIVATE USING_GLES HAVE_LIBRETRO_VFS)
  target_link_libraries(citra_common PRIVATE libretro_common)
  target_link_libraries(citra_core PRIVATE libretro_common)
  target_link_libraries(video_core PRIVATE libretro_common)
  target_link_libraries(azahar_libretro_common PRIVATE libretro_common)
  target_link_libraries(azahar_libretro PRIVATE libretro_common)
  # Link Android log library for __android_log_print
  target_link_libraries(azahar_libretro PRIVATE log)
  set_target_properties(azahar_libretro PROPERTIES SUFFIX "_android.so")
endif()

if(MINGW)
  target_link_libraries(azahar_libretro PRIVATE crypt32)
endif()

if(IOS)
  target_compile_definitions(azahar_libretro_common PRIVATE IOS)
  target_compile_definitions(azahar_libretro PRIVATE IOS)
  target_link_libraries(azahar_libretro PRIVATE "-framework CoreFoundation" "-framework Foundation")
endif()

if (SSE42_COMPILE_OPTION)
  target_compile_definitions(azahar_libretro PRIVATE CITRA_HAS_SSE42)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin" OR
    CMAKE_SYSTEM_NAME STREQUAL "iOS" OR
    CMAKE_SYSTEM_NAME STREQUAL "tvOS")
  target_link_libraries(azahar_libretro PRIVATE "-Wl,-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/libretro.osx.def")
else()
  target_link_libraries(azahar_libretro PRIVATE "-Wl,-Bsymbolic")
endif()
