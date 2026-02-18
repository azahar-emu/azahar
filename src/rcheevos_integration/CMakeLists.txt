add_library(rcheevos_integration STATIC
    rcheevos_integration.cpp
    rcheevos_integration.h
)

target_link_libraries(rcheevos_integration PRIVATE citra_common httplib rcheevos)
