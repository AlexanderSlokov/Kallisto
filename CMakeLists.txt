cmake_minimum_required(VERSION 3.10)
project(kallisto)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

# Create a library for core logic to share between main app and tests
add_library(kallisto_lib
    src/siphash.cpp
    src/cuckoo_table.cpp
    src/btree_index.cpp
    src/storage_engine.cpp
    src/kallisto.cpp
    helpers/logger.cpp
)

# Main Executable
add_executable(kallisto src/main.cpp)
target_link_libraries(kallisto kallisto_lib)

# Tests
enable_testing()
add_executable(kallisto_test tests/test_main.cpp)
target_link_libraries(kallisto_test kallisto_lib)

# Add test command

# Benchmark Tools
add_executable(bench_p99 tests/bench_p99.cpp)
target_link_libraries(bench_p99 kallisto_lib)

add_executable(bench_dos tests/bench_dos.cpp)
target_link_libraries(bench_dos kallisto_lib)

