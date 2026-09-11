add_executable(Nemu
    main.cpp
)

target_include_directories(Nemu PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(Nemu PRIVATE
    Nemu.Core
    Nemu.Frontend
)

target_compile_features(Nemu PRIVATE cxx_std_20)
