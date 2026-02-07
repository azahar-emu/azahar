add_library(libretro_common STATIC
    libretro-common/compat/compat_posix_string.c
    libretro-common/compat/fopen_utf8.c
    libretro-common/encodings/encoding_utf.c
    libretro-common/compat/compat_strl.c
    libretro-common/file/file_path.c
    libretro-common/streams/file_stream.c
    libretro-common/streams/file_stream_transforms.c
    libretro-common/string/stdstring.c
    libretro-common/time/rtime.c
    libretro-common/vfs/vfs_implementation.c
)
target_include_directories(libretro_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/libretro-common
    ${CMAKE_CURRENT_SOURCE_DIR}/libretro-common/include
)
