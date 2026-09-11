add_library(Nemu.Cpu STATIC
    cpu_state.hpp
    cpu_state.cpp
    instruction.hpp
    decoder.hpp
    decoder.cpp
    interpreter.hpp
    interpreter.cpp
)

target_include_directories(Nemu.Cpu PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(Nemu.Cpu PUBLIC
    Nemu.Platform
    Nemu.Memory
)

target_compile_features(Nemu.Cpu PUBLIC cxx_std_20)
