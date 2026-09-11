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

