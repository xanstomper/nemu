add_library(Nemu.Save STATIC
    save_manager.hpp
    save_manager.cpp
)

target_include_directories(Nemu.Save PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(Nemu.Save PUBLIC
    Nemu.FileSystem
    Nemu.Platform
)

target_compile_features(Nemu.Save PUBLIC cxx_std_20)
