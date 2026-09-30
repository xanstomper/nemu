add_library(Nemu.Debug STATIC
    crash_dump.hpp
    crash_dump.cpp
    breadcrumbs.hpp
    breadcrumbs.cpp
    gdb_stub.hpp
    gdb_stub.cpp
)

target_include_directories(Nemu.Debug PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(Nemu.Debug PUBLIC
    Nemu.Cpu
    Nemu.Platform
)

# Winsock is required for the GDB RSP stub on Windows builds.
if (WIN32)
    target_link_libraries(Nemu.Debug PUBLIC ws2_32)
endif()

target_compile_features(Nemu.Debug PUBLIC cxx_std_20)
