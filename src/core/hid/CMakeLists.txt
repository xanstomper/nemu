add_library(Nemu.Hid STATIC
    hid_types.hpp
    deadzone.hpp
    deadzone.cpp
    controller_mapping.hpp
    controller_mapping.cpp
    hid_manager.hpp
    hid_manager.cpp
    vibration.hpp
    vibration.cpp
    sixaxis.hpp
    sixaxis.cpp
    xbox_controller_driver.hpp
    xbox_controller_driver.cpp
)

target_include_directories(Nemu.Hid PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(Nemu.Hid PUBLIC
    Nemu.Platform
)

target_compile_features(Nemu.Hid PUBLIC cxx_std_20)
