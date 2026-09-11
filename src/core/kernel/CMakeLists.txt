add_library(Nemu.Kernel STATIC
    k_auto_object.hpp
    k_handle_table.hpp
    k_handle_table.cpp
    k_event.hpp
    k_event.cpp
    k_process.hpp
    k_process.cpp
    k_thread.hpp
    k_thread.cpp
    svc.hpp
    svc.cpp
)

target_include_directories(Nemu.Kernel PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(Nemu.Kernel PUBLIC
    Nemu.Cpu
    Nemu.Memory
    Nemu.Platform
)

target_compile_features(Nemu.Kernel PUBLIC cxx_std_20)
