add_subdirectory(memory)
add_subdirectory(cpu)
add_subdirectory(kernel)
add_subdirectory(loader)
add_subdirectory(filesystem)

add_library(Nemu.Core INTERFACE)
target_link_libraries(Nemu.Core INTERFACE
    Nemu.Cpu
    Nemu.Memory
    Nemu.Kernel
    Nemu.Loader
    Nemu.FileSystem
    Nemu.Platform
)

