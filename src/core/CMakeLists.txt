add_subdirectory(memory)
add_subdirectory(cpu)

add_library(Nemu.Core INTERFACE)
target_link_libraries(Nemu.Core INTERFACE
    Nemu.Cpu
    Nemu.Memory
    Nemu.Platform
)
