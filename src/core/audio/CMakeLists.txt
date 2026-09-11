set(AUDIO_SOURCES
    audio_types.hpp
    audio_ring_buffer.hpp
    audio_interface.hpp
    null_audio_backend.hpp
    null_audio_backend.cpp
    audio_factory.hpp
    audio_factory.cpp
)

if(WIN32)
    list(APPEND AUDIO_SOURCES
        xaudio2/xaudio2_backend.hpp
        xaudio2/xaudio2_backend.cpp
    )
endif()

add_library(Nemu.Audio STATIC ${AUDIO_SOURCES})

target_include_directories(Nemu.Audio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(Nemu.Audio PUBLIC
    Nemu.Platform
)

if(WIN32)
    target_link_libraries(Nemu.Audio PUBLIC
        xaudio2_8
        ole32
    )
endif()

target_compile_features(Nemu.Audio PUBLIC cxx_std_20)
