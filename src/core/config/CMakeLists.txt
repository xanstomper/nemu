add_library(Nemu.Config STATIC
    config_manager.hpp
    config_manager.cpp
)

target_include_directories(Nemu.Config PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(Nemu.Config PUBLIC
    Nemu.FileSystem
    Nemu.Hid
    Nemu.Platform
)

target_compile_features(Nemu.Config PUBLIC cxx_std_20)
