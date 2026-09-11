add_library(Nemu.Debug STATIC
    crash_dump.hpp
    crash_dump.cpp
)

target_include_directories(Nemu.Debug PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(Nemu.Debug PUBLIC
    Nemu.Cpu
    Nemu.Platform
)

target_compile_features(Nemu.Debug PUBLIC cxx_std_20)
