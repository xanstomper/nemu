add_library(Nemu.Frontend STATIC
    xbox_frontend.hpp
    xbox_frontend.cpp
)

target_include_directories(Nemu.Frontend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(Nemu.Frontend PUBLIC
    Nemu.Core
    Nemu.Gpu
    Nemu.Hid
    Nemu.FileSystem
    Nemu.Config
    Nemu.Platform
)

target_compile_features(Nemu.Frontend PUBLIC cxx_std_20)
