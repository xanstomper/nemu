add_library(Nemu.Crypto STATIC
    aes.hpp
    aes.cpp
    key_store.hpp
    key_store.cpp
)

target_include_directories(Nemu.Crypto PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(Nemu.Crypto PUBLIC
    Nemu.Platform
)

target_compile_features(Nemu.Crypto PUBLIC cxx_std_20)
