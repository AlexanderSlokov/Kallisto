cmake_minimum_required(VERSION 3.20)
project(kallisto CXX)

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
    src/kallisto.cpp
    helpers/logger.cpp
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
add_executable(kallisto src/main.cpp)
target_link_libraries(kallisto kallisto_lib)

# ===========================================================================
# Testing Infrastructure (CTest & GTest)
# ===========================================================================
enable_testing()
find_package(GTest CONFIG REQUIRED)

# Legacy / Custom Tests
add_executable(kallisto_test tests/test_main.cpp)
target_link_libraries(kallisto_test PRIVATE kallisto_lib GTest::gtest GTest::gtest_main pthread)
add_test(NAME CoreTest COMMAND kallisto_test)

add_executable(test_rocksdb tests/test_rocksdb.cpp)
target_link_libraries(test_rocksdb PRIVATE kallisto_lib GTest::gtest GTest::gtest_main pthread)
add_test(NAME RocksDBTest COMMAND test_rocksdb)

add_executable(test_threading tests/test_threading.cpp)
target_link_libraries(test_threading kallisto_lib)
add_test(NAME ThreadingTest COMMAND test_threading)

add_executable(test_listener tests/test_listener.cpp)
# (Target link libraries for test_listener are in Phase 2 below)
add_test(NAME ListenerTest COMMAND test_listener)

# Modern GTest Suites
add_executable(test_btree_rcu tests/test_btree_rcu.cpp)
target_link_libraries(test_btree_rcu PRIVATE kallisto_lib GTest::gtest GTest::gtest_main pthread)
add_test(NAME BTreeRCUTest COMMAND test_btree_rcu)

add_executable(test_sharded_cuckoo tests/test_sharded_cuckoo.cpp)
target_link_libraries(test_sharded_cuckoo PRIVATE kallisto_lib GTest::gtest GTest::gtest_main pthread)
add_test(NAME ShardedCuckooTest COMMAND test_sharded_cuckoo)

# ===========================================================================
# Benchmark Tools & Utils
# ===========================================================================
add_executable(bench_p99 tests/bench_p99.cpp)
target_link_libraries(bench_p99 kallisto_lib)

add_executable(bench_dos tests/bench_dos.cpp)
target_link_libraries(bench_dos kallisto_lib)

add_executable(repro_crash tests/repro_crash.cpp)
target_link_libraries(repro_crash kallisto_lib)

add_executable(bench_multithread tests/bench_multithread.cpp)
target_link_libraries(bench_multithread kallisto_lib)

# ===========================================================================
# Phase 2: Envoy-Style Server (gRPC + HTTP + SO_REUSEPORT)
# ===========================================================================
find_package(Protobuf CONFIG)
find_package(gRPC CONFIG)
find_package(simdjson CONFIG)

if(Protobuf_FOUND AND gRPC_FOUND)
    message(STATUS "gRPC and Protobuf found - building server components")

    # Proto Generation
    set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto")
    set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    file(MAKE_DIRECTORY ${GEN_DIR})

    find_program(PROTOC_EXECUTABLE protoc)
    find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin)

    add_custom_command(
        OUTPUT "${GEN_DIR}/kallisto.pb.cc" "${GEN_DIR}/kallisto.pb.h"
            "${GEN_DIR}/kallisto.grpc.pb.cc" "${GEN_DIR}/kallisto.grpc.pb.h"
        COMMAND ${PROTOC_EXECUTABLE}
            --proto_path=${PROTO_DIR}
            --cpp_out=${GEN_DIR}
            --grpc_out=${GEN_DIR}
            --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
            ${PROTO_DIR}/kallisto.proto
        DEPENDS ${PROTO_DIR}/kallisto.proto
        COMMENT "Generating gRPC/Protobuf sources from kallisto.proto"
    )

   # Proto library
   add_library(kallisto_proto_lib STATIC
       "${GEN_DIR}/kallisto.pb.cc"
       "${GEN_DIR}/kallisto.grpc.pb.cc"
   )
   target_include_directories(kallisto_proto_lib PUBLIC ${GEN_DIR})
   target_link_libraries(kallisto_proto_lib PUBLIC protobuf::libprotobuf gRPC::grpc++)

   # Server library
   add_library(kallisto_server_lib
       src/net/listener.cpp
       src/server/grpc_handler.cpp
       src/server/http_handler.cpp
   )
   target_link_libraries(kallisto_server_lib PUBLIC kallisto_lib kallisto_proto_lib gRPC::grpc++_reflection)

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
else()
   message(STATUS "gRPC/Protobuf not found - skipping server components")
endif()

# ===========================================================================
# Optional: Future Upgrades
# ===========================================================================
find_package(spdlog CONFIG)
if(spdlog_FOUND)
   message(STATUS "spdlog found - ready for Logger upgrade")
endif()