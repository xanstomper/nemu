add_library(Nemu.Network STATIC
    ldn_network.hpp
    ldn_network.cpp
)

target_include_directories(Nemu.Network PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(Nemu.Network PUBLIC
    Nemu.Platform
)

# Winsock2 for socket/IPC (LDN LAN) on Windows/Xbox targets.
if(WIN32)
    target_link_libraries(Nemu.Network PUBLIC ws2_32)
endif()

target_compile_features(Nemu.Network PUBLIC cxx_std_20)
