add_library(Nemu.Platform STATIC
    logger.hpp
    logger.cpp
    xbox_memory_governor.hpp
    xbox_memory_governor.cpp
    xbox_storage_broker.hpp
    xbox_storage_broker.cpp
    xbox_thread_affinity.hpp
    xbox_thread_affinity.cpp
    xbox_lifecycle.hpp
    xbox_lifecycle.cpp
)

target_include_directories(Nemu.Platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(Nemu.Platform PUBLIC cxx_std_20)
