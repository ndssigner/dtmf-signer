cmake_minimum_required(VERSION 3.16)
project(dtmf_signer C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# libsecp256k1: static, without its tests, benchmarks or examples.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_EXHAUSTIVE_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/secp256k1)

# ur-tones: its own target, built without our warning flags.
add_library(ur_tones STATIC third_party/ur-tones/c/ur_tones.c)
target_include_directories(ur_tones PUBLIC third_party/ur-tones/c)

# Warning flags apply to our own targets only.
add_library(ds_warnings INTERFACE)
target_compile_options(ds_warnings INTERFACE -Wall -Wextra -Werror)

enable_testing()
add_subdirectory(core)
add_subdirectory(ports/host)
add_subdirectory(tests/c)
