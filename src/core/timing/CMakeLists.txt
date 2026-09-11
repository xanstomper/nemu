add_library(Nemu.Timing STATIC
    core_timing.hpp
    core_timing.cpp
)

target_include_directories(Nemu.Timing PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_compile_features(Nemu.Timing PRIVATE cxx_std_20)
