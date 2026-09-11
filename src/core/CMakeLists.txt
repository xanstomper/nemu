add_subdirectory(memory)
add_subdirectory(cpu)
add_subdirectory(timing)
add_subdirectory(common)
add_subdirectory(kernel)
add_subdirectory(crypto)
add_subdirectory(loader)
add_subdirectory(filesystem)
add_subdirectory(gpu)
add_subdirectory(audio)
add_subdirectory(hid)
add_subdirectory(debug)
add_subdirectory(save)
add_subdirectory(config)

add_library(Nemu.Core INTERFACE)
target_link_libraries(Nemu.Core INTERFACE
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




