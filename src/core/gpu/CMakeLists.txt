set(GPU_SOURCES
    deswizzle.hpp
    deswizzle.cpp
    gpu_interface.hpp
    null_backend.hpp
    null_backend.cpp
    maxwell_3d.hpp
    maxwell_3d.cpp
    gpu_factory.hpp
    gpu_factory.cpp
)

if(WIN32)
    list(APPEND GPU_SOURCES
        d3d12/d3d12_backend.hpp
        d3d12/d3d12_backend.cpp
    )
endif()

add_library(Nemu.Gpu STATIC ${GPU_SOURCES})

target_include_directories(Nemu.Gpu PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(Nemu.Gpu PUBLIC
    Nemu.Platform
)

if(WIN32)
    target_link_libraries(Nemu.Gpu PUBLIC
        d3d12
        dxgi
    )
endif()

target_compile_features(Nemu.Gpu PUBLIC cxx_std_20)
