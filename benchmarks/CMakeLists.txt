# Nemu Benchmarks Suite
add_executable(bench_jit_vs_interpreter
    bench_jit_vs_interpreter.cpp
)
target_include_directories(bench_jit_vs_interpreter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(bench_jit_vs_interpreter PRIVATE
    Nemu.Core
)
target_compile_features(bench_jit_vs_interpreter PRIVATE cxx_std_20)

add_executable(bench_memory
    bench_memory.cpp
)
target_include_directories(bench_memory PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(bench_memory PRIVATE
    Nemu.Core
)
target_compile_features(bench_memory PRIVATE cxx_std_20)

add_executable(bench_deswizzle
    bench_deswizzle.cpp
)
target_include_directories(bench_deswizzle PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(bench_deswizzle PRIVATE
    Nemu.Core
)
target_compile_features(bench_deswizzle PRIVATE cxx_std_20)

add_executable(bench_ipc
    bench_ipc.cpp
)
target_include_directories(bench_ipc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(bench_ipc PRIVATE
    Nemu.Core
)
target_compile_features(bench_ipc PRIVATE cxx_std_20)
