add_executable(citra_room_standalone
    citra_room_standalone.cpp
)

set_target_properties(citra_room_standalone PROPERTIES OUTPUT_NAME "azahar-room")

target_link_libraries(citra_room_standalone PRIVATE citra_room)

if(UNIX AND NOT APPLE)
    install(TARGETS citra_room_standalone RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
endif()
