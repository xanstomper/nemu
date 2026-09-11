add_library(Nemu.Loader STATIC
    nro.hpp
    nro.cpp
    nso.hpp
    nso.cpp
    pfs0.hpp
    pfs0.cpp
    nca.hpp
    nca.cpp
    romfs.hpp
    romfs.cpp
    title_loader.hpp
    title_loader.cpp
)

target_include_directories(Nemu.Loader PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(Nemu.Loader PUBLIC
    Nemu.Memory
    Nemu.Cpu
    Nemu.Crypto
    Nemu.FileSystem
    Nemu.Platform
)

target_compile_features(Nemu.Loader PUBLIC cxx_std_20)
