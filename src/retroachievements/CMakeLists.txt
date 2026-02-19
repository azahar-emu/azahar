add_library(retroachievements STATIC
    client.cpp
    client.h
)

target_link_libraries(retroachievements PRIVATE citra_common httplib rcheevos)
