# Kid Icarus: Uprising single-screen QOL layer (emulator-independent core).
add_library(kiu_qol STATIC
    kiu_config.cpp
    kiu_config.h
    kiu_controller.cpp
    kiu_controller.h
    kiu_default_config.cpp
    kiu_hud.cpp
    kiu_hud.h
    kiu_stylus.cpp
    kiu_stylus.h
    kiu_types.h
)
target_include_directories(kiu_qol PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_features(kiu_qol PUBLIC cxx_std_17)

if (ENABLE_TESTS)
    add_executable(kiu_qol_tests tests/kiu_qol_tests.cpp)
    target_link_libraries(kiu_qol_tests PRIVATE kiu_qol)
    add_test(NAME kiu_qol_tests COMMAND kiu_qol_tests)
endif()
