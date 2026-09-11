add_executable(test_cpu
    cpu/test_cpu.cpp
)

target_include_directories(test_cpu PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(test_cpu PRIVATE
    Nemu.Core
)

target_compile_features(test_cpu PRIVATE cxx_std_20)
add_test(NAME test_cpu COMMAND test_cpu)

add_executable(test_memory
    memory/test_memory.cpp
)

target_include_directories(test_memory PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(test_memory PRIVATE
    Nemu.Core
)

target_compile_features(test_memory PRIVATE cxx_std_20)
add_test(NAME test_memory COMMAND test_memory)

add_executable(test_kernel
    kernel/test_kernel.cpp
)

target_include_directories(test_kernel PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(test_kernel PRIVATE
    Nemu.Core
)

target_compile_features(test_kernel PRIVATE cxx_std_20)
add_test(NAME test_kernel COMMAND test_kernel)

add_executable(test_loader
    loader/test_loader.cpp
)

target_include_directories(test_loader PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(test_loader PRIVATE
    Nemu.Core
)

target_compile_features(test_loader PRIVATE cxx_std_20)
add_test(NAME test_loader COMMAND test_loader)

add_executable(test_vfs
    filesystem/test_vfs.cpp
)

target_include_directories(test_vfs PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(test_vfs PRIVATE
    Nemu.Core
)

target_compile_features(test_vfs PRIVATE cxx_std_20)
add_test(NAME test_vfs COMMAND test_vfs)

add_executable(test_gpu
    gpu/test_gpu.cpp
)

target_include_directories(test_gpu PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(test_gpu PRIVATE
    Nemu.Core
)

target_compile_features(test_gpu PRIVATE cxx_std_20)
add_test(NAME test_gpu COMMAND test_gpu)

add_executable(test_audio
    audio/test_audio.cpp
)

target_include_directories(test_audio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(test_audio PRIVATE
    Nemu.Core
)

target_compile_features(test_audio PRIVATE cxx_std_20)
add_test(NAME test_audio COMMAND test_audio)

add_executable(test_hid
    hid/test_hid.cpp
)

target_include_directories(test_hid PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(test_hid PRIVATE
    Nemu.Core
)

target_compile_features(test_hid PRIVATE cxx_std_20)
add_test(NAME test_hid COMMAND test_hid)

add_executable(test_jit
    jit/test_jit.cpp
)

target_include_directories(test_jit PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(test_jit PRIVATE
    Nemu.Core
)

target_compile_features(test_jit PRIVATE cxx_std_20)
add_test(NAME test_jit COMMAND test_jit)

add_executable(test_save
    save/test_save.cpp
)

target_include_directories(test_save PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(test_save PRIVATE
    Nemu.Core
)

target_compile_features(test_save PRIVATE cxx_std_20)
add_test(NAME test_save COMMAND test_save)

add_executable(test_config
    config/test_config.cpp
)

target_include_directories(test_config PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(test_config PRIVATE
    Nemu.Core
)

target_compile_features(test_config PRIVATE cxx_std_20)
add_test(NAME test_config COMMAND test_config)

add_executable(test_debug
    debug/test_debug.cpp
)

target_include_directories(test_debug PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(test_debug PRIVATE
    Nemu.Core
)

target_compile_features(test_debug PRIVATE cxx_std_20)
add_test(NAME test_debug COMMAND test_debug)

add_executable(test_frontend
    frontend/test_frontend.cpp
)

target_include_directories(test_frontend PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(test_frontend PRIVATE
    Nemu.Core
    Nemu.Frontend
)

target_compile_features(test_frontend PRIVATE cxx_std_20)
add_test(NAME test_frontend COMMAND test_frontend)

add_executable(test_ipc
    ipc/test_ipc.cpp
)

target_include_directories(test_ipc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(test_ipc PRIVATE
    Nemu.Core
)

target_compile_features(test_ipc PRIVATE cxx_std_20)
add_test(NAME test_ipc COMMAND test_ipc)






add_executable(test_timing
    timing/test_timing.cpp
)

target_include_directories(test_timing PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(test_timing PRIVATE
    Nemu.Core
)

target_compile_features(test_timing PRIVATE cxx_std_20)
add_test(NAME test_timing COMMAND test_timing)

add_executable(test_address_space
    memory/test_address_space.cpp
)

target_include_directories(test_address_space PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(test_address_space PRIVATE
    Nemu.Core
)

target_compile_features(test_address_space PRIVATE cxx_std_20)
add_test(NAME test_address_space COMMAND test_address_space)

add_executable(test_timed_wake
    timing/test_timed_wake.cpp
)

target_include_directories(test_timed_wake PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(test_timed_wake PRIVATE
    Nemu.Core
)

target_compile_features(test_timed_wake PRIVATE cxx_std_20)
add_test(NAME test_timed_wake COMMAND test_timed_wake)

add_executable(test_bounded_queue
    common/test_bounded_queue.cpp
)

target_include_directories(test_bounded_queue PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(test_bounded_queue PRIVATE
    Nemu.Core
)

target_compile_features(test_bounded_queue PRIVATE cxx_std_20)
add_test(NAME test_bounded_queue COMMAND test_bounded_queue)

add_executable(test_scratch_buffer
    common/test_scratch_buffer.cpp
)

target_include_directories(test_scratch_buffer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(test_scratch_buffer PRIVATE
    Nemu.Core
)

target_compile_features(test_scratch_buffer PRIVATE cxx_std_20)
add_test(NAME test_scratch_buffer COMMAND test_scratch_buffer)
