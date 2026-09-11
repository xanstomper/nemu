cmake_minimum_required(VERSION 3.22)
project(Nemu VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Platform detection
if(WIN32)
    message(STATUS "Targeting Windows / Xbox Platform")
    add_compile_definitions(NEMU_PLATFORM_WINDOWS=1)
    if(NEMU_BUILD_XBOX)
        message(STATUS "Building specifically for Xbox Developer Mode (UWP / Win32 Full Trust)")
        add_compile_definitions(NEMU_PLATFORM_XBOX=1)
    endif()
else()
    message(STATUS "Targeting Linux / Host Platform")
    add_compile_definitions(NEMU_PLATFORM_LINUX=1)
endif()

# Strict compiler warning options
if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8)
else()
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wformat=2
    )
endif()

# Build options
option(NEMU_ENABLE_TESTS "Build automated unit and integration tests" ON)
option(NEMU_ENABLE_BENCHMARKS "Build micro-benchmarks" ON)
option(NEMU_ENABLE_LOGGING "Enable structured logging and tracing" ON)
option(NEMU_BUILD_XBOX "Configure build for Xbox Series S/X Dev Mode" OFF)

if(NEMU_ENABLE_LOGGING)
    add_compile_definitions(NEMU_LOGGING_ENABLED=1)
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Source directories
add_subdirectory(src)

if(NEMU_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(NEMU_ENABLE_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
