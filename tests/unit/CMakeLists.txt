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
