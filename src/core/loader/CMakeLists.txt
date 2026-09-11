add_library(Nemu.Loader STATIC
    nro.hpp
    nro.cpp
)

target_include_directories(Nemu.Loader PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(Nemu.Loader PUBLIC
    Nemu.Memory
    Nemu.Cpu
    Nemu.Platform
)

target_compile_features(Nemu.Loader PUBLIC cxx_std_20)
