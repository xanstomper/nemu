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
    nvhost/nvmap.hpp
    nvhost/nvmap.cpp
    nvhost/nvhost_ctrl.hpp
    nvhost/nvhost_ctrl.cpp
    nvhost/nvhost_as_gpu.hpp
    nvhost/nvhost_as_gpu.cpp
    nvhost/nvhost_channel.hpp
    nvhost/nvhost_channel.cpp
    nvhost/nvdevice.hpp
    nvhost/nvdevice.cpp
    presentation/buffer_queue.hpp
    presentation/buffer_queue.cpp
    presentation/nvnflinger.hpp
    presentation/nvnflinger.cpp
    texture/astc_decoder.hpp
    texture/astc_decoder.cpp
    texture/texture_types.hpp
    texture/texture_types.cpp
    texture/texture_cache.hpp
    texture/texture_cache.cpp
    shader/maxwell_shader_decoder.hpp
    shader/maxwell_shader_decoder.cpp
    shader/shader_translator.hpp
    shader/shader_translator.cpp
    shader/hlsl_validator.hpp
    shader/hlsl_validator.cpp
    pipeline/pipeline_cache.hpp
    pipeline/pipeline_cache.cpp
    pipeline/pipeline_bridge.hpp
    pipeline/pipeline_bridge.cpp
    pipeline/graphics_optimizer.hpp
    pipeline/graphics_optimizer.cpp
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
    Nemu.Memory
    Nemu.Platform
)

if(WIN32)
    target_link_libraries(Nemu.Gpu PUBLIC
        d3d12
        dxgi
        d3dcompiler
    )
endif()

target_compile_features(Nemu.Gpu PUBLIC cxx_std_20)
