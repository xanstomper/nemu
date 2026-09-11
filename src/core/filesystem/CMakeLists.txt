add_library(Nemu.FileSystem STATIC
    vfs.hpp
    vfs.cpp
)

target_include_directories(Nemu.FileSystem PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(Nemu.FileSystem PUBLIC
    Nemu.Platform
)

target_compile_features(Nemu.FileSystem PUBLIC cxx_std_20)
