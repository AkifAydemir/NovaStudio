cmake_minimum_required(VERSION 3.20)
project(Nova LANGUAGES C)

# Keep all Windows runtime artifacts in the root build directory for deterministic
# DLL/executable discovery under both single-config and multi-config generators.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
foreach(NOVA_CONFIG Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${NOVA_CONFIG}" NOVA_CONFIG_UPPER)
    set("CMAKE_RUNTIME_OUTPUT_DIRECTORY_${NOVA_CONFIG_UPPER}" "${CMAKE_BINARY_DIR}")
endforeach()

include(CTest)
add_subdirectory(native)
add_subdirectory(compiler)
