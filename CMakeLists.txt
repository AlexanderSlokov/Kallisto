cmake_minimum_required(VERSION 3.10)
project(kallisto)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

add_executable(kallisto 
    src/main.cpp
    src/siphash.cpp
    src/cuckoo_table.cpp
    src/btree_index.cpp
    src/btree_index.cpp
    src/storage_engine.cpp
    src/kallisto.cpp
    helpers/logger.cpp
)

# Future: Add tests
enable_testing()
