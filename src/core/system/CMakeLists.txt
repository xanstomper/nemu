add_library(Nemu.System STATIC
    emulator.hpp
    emulator.cpp
)

target_include_directories(Nemu.System PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(Nemu.System PUBLIC
    Nemu.Cpu
    Nemu.Timing
    Nemu.Common
    Nemu.Memory
    Nemu.Kernel
    Nemu.Crypto
    Nemu.Loader
    Nemu.FileSystem
    Nemu.Gpu
    Nemu.Audio
    Nemu.Hid
    Nemu.Debug
    Nemu.Save
    Nemu.Config
    Nemu.Platform
)

target_compile_features(Nemu.System PUBLIC cxx_std_20)
