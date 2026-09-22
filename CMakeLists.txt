cmake_minimum_required(VERSION 3.20)
project(evm CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(evm
    src/uint256.cpp
    src/evm.cpp
    src/host.cpp
    src/opcode.cpp
    src/hex.cpp
)
target_include_directories(evm PUBLIC include)

add_executable(evm_run src/main.cpp)
target_link_libraries(evm_run PRIVATE evm)

enable_testing()
add_executable(evm_tests
    tests/test_main.cpp
    tests/test_smoke.cpp
    tests/test_uint256.cpp
)
target_link_libraries(evm_tests PRIVATE evm)
add_test(NAME evm_tests COMMAND evm_tests)
