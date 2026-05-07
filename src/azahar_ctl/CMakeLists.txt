add_executable(azahar-ctl
    azahar_ctl.cpp
)

target_link_libraries(azahar-ctl PRIVATE Qt6::Core Qt6::Network)

if (AZAHAR_IPC_SOCKET_NAME)
    target_compile_definitions(azahar-ctl PRIVATE AZAHAR_IPC_SOCKET_NAME="${AZAHAR_IPC_SOCKET_NAME}")
endif()

if (UNIX AND NOT APPLE)
    install(TARGETS azahar-ctl RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
elseif(WIN32)
    install(TARGETS azahar-ctl RUNTIME DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
endif()
