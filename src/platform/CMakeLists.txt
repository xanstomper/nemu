add_library(Nemu.Platform STATIC
    logger.hpp
    logger.cpp
)

target_include_directories(Nemu.Platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(Nemu.Platform PUBLIC cxx_std_20)
