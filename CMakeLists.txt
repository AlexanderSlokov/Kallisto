cmake_minimum_required(VERSION 3.20)
project(kallisto CXX)

# For Clangd. Allow CMake introduces compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Performance flags (required for simdjson AVX2/AVX-512)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-march=native -pthread)

option(ENABLE_COVERAGE "Enable coverage reporting" OFF)

include_directories(include)

# Create a library for core logic to share between main app and tests
add_library(kallisto_lib
    src/siphash.cpp
    src/cuckoo_table.cpp
    src/btree_index.cpp
    src/tls_btree_manager.cpp
    src/rocksdb_storage.cpp
    src/kallisto_core.cpp
    # Threading infrastructure (Phase 1.1)
    src/event/dispatcher_impl.cpp
    src/event/worker_impl.cpp
    src/thread_local/thread_local_impl.cpp
    # Sharded storage (Phase 1.2)
    src/sharded_cuckoo_table.cpp
    # Network infrastructure (Phase 2.2)
    src/net/listener.cpp
)

# Link pthread for threading support
target_link_libraries(kallisto_lib PRIVATE pthread)

if(ENABLE_COVERAGE)
    target_compile_options(kallisto_lib PUBLIC --coverage -O0 -g)
    target_link_options(kallisto_lib PUBLIC --coverage)
endif()

# RocksDB (persistence layer)
find_package(RocksDB CONFIG)
if(RocksDB_FOUND)
    target_link_libraries(kallisto_lib PUBLIC RocksDB::rocksdb)
    target_compile_definitions(kallisto_lib PUBLIC KALLISTO_HAS_ROCKSDB=1)
    message(STATUS "RocksDB found - persistence layer enabled")
else()
    message(STATUS "RocksDB not found - persistence layer disabled")
endif()

# Main Executable
add_executable(kallisto src/cli.cpp)
target_link_libraries(kallisto kallisto_lib)

# ==============================================================================
# Testing Infrastructure (CTest & GTest)
# ==============================================================================
enable_testing()
find_package(GTest CONFIG REQUIRED)

# CLI Tests
add_executable(kallisto_test src/test_main.cpp)
target_link_libraries(kallisto_test PRIVATE kallisto_lib GTest::gtest GTest::gtest_main pthread)
add_test(NAME CliTest COMMAND kallisto_test)

# CuckooTable Tests
add_executable(test_cuckoo_table src/test_cuckoo_table.cpp)
target_link_libraries(test_cuckoo_table PRIVATE kallisto_lib GTest::gtest GTest::gtest_main pthread)
add_test(NAME CuckooTableTest COMMAND test_cuckoo_table)

add_executable(test_rocksdb src/test_rocksdb_storage.cpp)
target_link_libraries(test_rocksdb PRIVATE kallisto_lib GTest::gtest GTest::gtest_main pthread)
add_test(NAME RocksDBTest COMMAND test_rocksdb)

add_executable(test_threading src/event/test_threading.cpp)
target_link_libraries(test_threading kallisto_lib)
add_test(NAME ThreadingTest COMMAND test_threading)

add_executable(test_listener src/net/test_listener.cpp)
# (Target link libraries for test_listener are in Phase 2 below)
add_test(NAME ListenerTest COMMAND test_listener)

add_executable(test_thread_local src/thread_local/test_thread_local.cpp)
target_link_libraries(test_thread_local PRIVATE kallisto_lib GTest::gtest GTest::gtest_main pthread)
add_test(NAME ThreadLocalTest COMMAND test_thread_local)

add_executable(test_dispatcher src/event/test_dispatcher.cpp)
target_link_libraries(test_dispatcher PRIVATE kallisto_lib GTest::gtest GTest::gtest_main pthread)
add_test(NAME DispatcherTest COMMAND test_dispatcher)

add_executable(test_worker src/event/test_worker.cpp)
target_link_libraries(test_worker PRIVATE kallisto_lib GTest::gtest GTest::gtest_main pthread)
add_test(NAME WorkerTest COMMAND test_worker)

# Modern GTest Suites
add_executable(test_kallisto_core src/test_kallisto_core.cpp)
target_link_libraries(test_kallisto_core PRIVATE kallisto_lib GTest::gtest GTest::gtest_main pthread)
add_test(NAME KallistoCoreTest COMMAND test_kallisto_core)

# Kallisto UDS Admin tests
add_executable(test_uds_admin src/server/test_uds_admin_handler.cpp)
target_link_libraries(test_uds_admin PRIVATE kallisto_server_lib kallisto_lib GTest::gtest GTest::gtest_main pthread)
add_test(NAME UdsAdminTest COMMAND test_uds_admin)

add_executable(test_btree_rcu src/test_btree_index.cpp)
target_link_libraries(test_btree_rcu PRIVATE kallisto_lib GTest::gtest GTest::gtest_main pthread)
add_test(NAME BTreeRCUTest COMMAND test_btree_rcu)

add_executable(test_sharded_cuckoo src/test_sharded_cuckoo_table.cpp)
target_link_libraries(test_sharded_cuckoo PRIVATE kallisto_lib GTest::gtest GTest::gtest_main pthread)
add_test(NAME ShardedCuckooTest COMMAND test_sharded_cuckoo)

add_executable(test_siphash src/test_siphash.cpp)
target_link_libraries(test_siphash PRIVATE kallisto_lib GTest::gtest GTest::gtest_main pthread)
add_test(NAME SipHashTest COMMAND test_siphash)

add_executable(test_http_handler src/server/test_http_handler.cpp)
target_link_libraries(test_http_handler PRIVATE kallisto_server_lib kallisto_lib GTest::gtest GTest::gtest_main GTest::gmock pthread)
add_test(NAME HttpHandlerTest COMMAND test_http_handler)

add_executable(test_kallisto_server src/test_kallisto_server.cpp)
# Note: we link against kallisto_server_lib so the HttpHandler/UdsAdmin implementations are met
target_link_libraries(test_kallisto_server PRIVATE kallisto_server_lib kallisto_lib GTest::gtest GTest::gtest_main pthread)
add_test(NAME KallistoServerTest COMMAND test_kallisto_server)

# ===========================================================================
# Benchmark Tools & Utils
# ===========================================================================
add_executable(bench_p99 benchmarks/bench_p99.cpp)
target_link_libraries(bench_p99 kallisto_lib)

add_executable(bench_dos benchmarks/bench_dos.cpp)
target_link_libraries(bench_dos kallisto_lib)

add_executable(repro_crash benchmarks/repro_crash.cpp)
target_link_libraries(repro_crash kallisto_lib)

add_executable(bench_multithread benchmarks/bench_multithread.cpp)
target_link_libraries(bench_multithread kallisto_lib)

# ===========================================================================
# Phase 2: Envoy-Style Server (HTTP + SO_REUSEPORT)
# ===========================================================================
find_package(simdjson CONFIG)

# Server library
add_library(kallisto_server_lib
    src/net/listener.cpp
    src/server/http_handler.cpp
    src/server/uds_admin_handler.cpp
)
target_link_libraries(kallisto_server_lib PUBLIC kallisto_lib)

if(ENABLE_COVERAGE)
    target_compile_options(kallisto_server_lib PUBLIC --coverage -O0 -g)
    target_link_options(kallisto_server_lib PUBLIC --coverage)
endif()

if(simdjson_FOUND)
    target_link_libraries(kallisto_server_lib PUBLIC simdjson::simdjson)
    target_compile_definitions(kallisto_server_lib PUBLIC KALLISTO_HAS_SIMDJSON=1)
endif()

# Server executable
add_executable(kallisto_server src/kallisto_server.cpp)
target_link_libraries(kallisto_server kallisto_server_lib)
# Server tests link
target_link_libraries(test_listener kallisto_server_lib)

# ===========================================================================
# Optional: Future Upgrades
# ===========================================================================
find_package(spdlog CONFIG)
if(spdlog_FOUND)
   message(STATUS "spdlog found - ready for Logger upgrade")
endif()