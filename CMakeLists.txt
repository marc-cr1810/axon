cmake_minimum_required(VERSION 3.21)
project(axon VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Options ───────────────────────────────────────────────────────────────────
option(AXON_BUILD_TESTS    "Build unit tests"        ON)
option(AXON_BUILD_EXAMPLES "Build examples"          ON)
option(AXON_USE_HUGEPAGES  "Enable huge page hints"  ON)

# ── Compiler flags ────────────────────────────────────────────────────────────
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wno-interference-size
)

# Release flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -march=native)
endif()

# Debug flags
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-O0 -g3 -fsanitize=address,undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

# ── Library target ────────────────────────────────────────────────────────────
add_library(axon STATIC
    src/shm/shm_segment.cpp
    src/transport/registry.cpp
    src/node.cpp
    src/platform/cpu.cpp
)

target_include_directories(axon PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(axon PUBLIC rt pthread)

if(AXON_USE_HUGEPAGES)
    target_compile_definitions(axon PUBLIC AXON_USE_HUGEPAGES)
endif()

add_library(axon::axon ALIAS axon)

# ── Sub-directories ───────────────────────────────────────────────────────────
if(AXON_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(AXON_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
