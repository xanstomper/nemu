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

if (WIN32)
    set_target_properties(Nemu PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_options(Nemu PRIVATE
        "-Wl,--subsystem,windows:10.0"
        "-Wl,--major-os-version,10"
        "-Wl,--minor-os-version,0"
        "-Wl,--major-subsystem-version,10"
        "-Wl,--minor-subsystem-version,0"
        "-s"
    )
endif()

