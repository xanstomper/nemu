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
    ipc/ipc_types.hpp
    ipc/ipc_service.hpp
    ipc/ipc_dispatcher.hpp
    ipc/ipc_dispatcher.cpp
    ipc/service_registry.hpp
    ipc/service_registry.cpp
    ipc/k_shared_memory.hpp
    ipc/hid_shared.hpp
    ipc/sm_service.hpp
    ipc/sm_service.cpp
    ipc/time_service.hpp
    ipc/time_service.cpp
    ipc/set_sys_service.hpp
    ipc/set_sys_service.cpp
    ipc/hid_service.hpp
    ipc/hid_service.cpp
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
