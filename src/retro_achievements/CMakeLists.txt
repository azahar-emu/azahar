add_library(retro_achievements STATIC
    client.cpp
    client.h
)

target_link_libraries(retro_achievements
    PUBLIC rcheevos
    PRIVATE citra_common httplib
)
