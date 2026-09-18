cmake_minimum_required(VERSION 3.20)
project(NovaVM C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
add_library(nova_vm SHARED
src/nova_vm.c
)
target_include_directories(nova_vm PUBLIC
${CMAKE_CURRENT_SOURCE_DIR}/include
)
set_target_properties(nova_vm PROPERTIES
RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
)
if(MSVC)
target_compile_options(nova_vm PRIVATE /W4 /WX)
else()
target_compile_options(nova_vm PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
include(CTest)
if(BUILD_TESTING)
add_executable(nova_vm_smoke tests/test_vm.c)
target_link_libraries(nova_vm_smoke PRIVATE nova_vm)
target_include_directories(nova_vm_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_executable(nova_vm_debugger_test tests/test_debugger.c)
target_link_libraries(nova_vm_debugger_test PRIVATE nova_vm)
target_include_directories(nova_vm_debugger_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_executable(nova_vm_compare_test tests/test_compare.c)
target_link_libraries(nova_vm_compare_test PRIVATE nova_vm)
target_include_directories(nova_vm_compare_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_executable(nova_vm_bitwise_test tests/test_bitwise.c)
target_link_libraries(nova_vm_bitwise_test PRIVATE nova_vm)
target_include_directories(nova_vm_bitwise_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_executable(nova_vm_allocator_test tests/test_allocator.c)
target_link_libraries(nova_vm_allocator_test PRIVATE nova_vm)
target_include_directories(nova_vm_allocator_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME nova_vm_smoke COMMAND nova_vm_smoke)
add_test(NAME nova_vm_debugger COMMAND nova_vm_debugger_test)
add_test(NAME nova_vm_compare COMMAND nova_vm_compare_test)
add_test(NAME nova_vm_bitwise COMMAND nova_vm_bitwise_test)
add_test(NAME nova_vm_allocator COMMAND nova_vm_allocator_test)
endif()
