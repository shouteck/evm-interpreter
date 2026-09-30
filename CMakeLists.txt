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
    src/keccak.cpp
)
target_include_directories(evm PUBLIC include)

add_executable(evm_run src/main.cpp)
target_link_libraries(evm_run PRIVATE evm)

enable_testing()
add_executable(evm_tests
    tests/test_main.cpp
    tests/test_smoke.cpp
    tests/test_uint256.cpp
    tests/test_evm.cpp
    tests/test_keccak.cpp
)
target_link_libraries(evm_tests PRIVATE evm)
add_test(NAME evm_tests COMMAND evm_tests)

# M4: official VMTests corpus runner (separate binary — it's a corpus sweep,
# not a unit test; `evm_vectors` from the build dir)
add_executable(evm_vectors tests/test_vectors.cpp)
target_link_libraries(evm_vectors PRIVATE evm)
target_include_directories(evm_vectors PRIVATE tests)

# demo scenario runner (native build of the same shim the WASM page uses)
add_executable(evm_scenario tools/demo/scenario.cpp)
target_link_libraries(evm_scenario PRIVATE evm)
target_include_directories(evm_scenario PRIVATE tests)
