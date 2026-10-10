add_library(citra_cli STATIC EXCLUDE_FROM_ALL
    citra_cli.h
    citra_cli.cpp
    compression_cli.h
    compression_cli.cpp
)

target_link_libraries(citra_cli PRIVATE citra_common citra_core)

if (MSVC)
    target_link_libraries(citra_cli PRIVATE getopt)
endif()
