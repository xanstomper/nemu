add_library(Nemu.Loader STATIC
    nro.hpp
    nro.cpp
    nso.hpp
    nso.cpp
    pfs0.hpp
    pfs0.cpp
    xci.hpp
    xci.cpp
    nca.hpp
    nca.cpp
    ncz.hpp
    ncz.cpp
    romfs.hpp
    romfs.cpp
    title_loader.hpp
    title_loader.cpp
    patch_manager.hpp
    patch_manager.cpp
)

target_include_directories(Nemu.Loader PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

find_library(ZSTD_LIB NAMES zstd)

target_link_libraries(Nemu.Loader PUBLIC
    Nemu.Memory
    Nemu.Cpu
    Nemu.Crypto
    Nemu.FileSystem
    Nemu.Platform
)

if(ZSTD_LIB)
    target_link_libraries(Nemu.Loader PUBLIC ${ZSTD_LIB})
endif()

target_compile_features(Nemu.Loader PUBLIC cxx_std_20)
