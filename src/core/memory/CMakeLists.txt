add_library(Nemu.Memory STATIC
    memory_interface.hpp
    virtual_memory.hpp
    virtual_memory.cpp
    fastmem.hpp
    fastmem.cpp
    address_space.hpp
    address_space.cpp
)

target_include_directories(Nemu.Memory PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(Nemu.Memory PUBLIC
    Nemu.Platform
)

target_compile_features(Nemu.Memory PUBLIC cxx_std_20)
