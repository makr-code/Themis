cmake_minimum_required(VERSION 3.20)
project(Themis VERSION 1.0.0 LANGUAGES CXX)

# C++20 Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# vcpkg integration
if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
endif()

# Build options
option(THEMIS_BUILD_TESTS "Build unit tests" ON)
option(THEMIS_BUILD_BENCHMARKS "Build performance benchmarks" ON)
option(THEMIS_ENABLE_GPU "Enable GPU acceleration for vector search" OFF)
option(THEMIS_ENABLE_ASAN "Enable AddressSanitizer for debugging" OFF)
option(THEMIS_STRICT_BUILD "Treat warnings as errors" OFF)

# Compiler warnings and optimizations
if(MSVC)
    add_compile_options(/W4 /utf-8 /wd4701)
    if(THEMIS_STRICT_BUILD)
        add_compile_options(/WX)
    endif()
    add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/O2 /GL)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(THEMIS_STRICT_BUILD)
        add_compile_options(-Werror)
    endif()
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -march=native)
    endif()
endif()

# AddressSanitizer support
if(THEMIS_ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

# Find required packages
find_package(RocksDB CONFIG REQUIRED)
find_package(simdjson CONFIG REQUIRED)
find_package(TBB CONFIG REQUIRED)
find_package(Arrow CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

# Optional: HNSW (if not using Faiss)
find_package(hnswlib CONFIG)

# Optional: Faiss for GPU support
if(THEMIS_ENABLE_GPU)
    find_package(faiss CONFIG REQUIRED)
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Core library sources
set(THEMIS_CORE_SOURCES
    src/storage/rocksdb_wrapper.cpp
    src/storage/base_entity.cpp
    src/storage/key_schema.cpp
    
    src/index/secondary_index.cpp
    src/index/graph_index.cpp
    src/index/vector_index.cpp
    
    src/transaction/transaction_manager.cpp
    src/transaction/saga.cpp
    
    src/query/query_engine.cpp
    src/query/query_optimizer.cpp
    src/query/query_parser.cpp
    src/query/aql_parser.cpp
    src/query/aql_translator.cpp
    
    src/server/http_server.cpp
    
    src/content/content_type.cpp
    src/content/content_manager.cpp
    src/content/text_processor.cpp
    
    src/utils/serialization.cpp
    src/utils/logger.cpp
)

# Main library
add_library(themis_core STATIC ${THEMIS_CORE_SOURCES})

target_link_libraries(themis_core
    PUBLIC
        RocksDB::rocksdb
        simdjson::simdjson
        TBB::tbb
        Arrow::arrow_shared
        fmt::fmt
        spdlog::spdlog
        Boost::system
        nlohmann_json::nlohmann_json
)

if(THEMIS_ENABLE_GPU)
    target_link_libraries(themis_core PUBLIC faiss)
    target_compile_definitions(themis_core PUBLIC THEMIS_GPU_ENABLED)
elseif(hnswlib_FOUND)
    target_link_libraries(themis_core PUBLIC hnswlib::hnswlib)
    target_compile_definitions(themis_core PUBLIC THEMIS_HNSW_ENABLED)
endif()

# Main executable (demo)
add_executable(themis_demo
    src/main.cpp
)

target_link_libraries(themis_demo
    PRIVATE
        themis_core
)

# API Server executable
add_executable(themis_server
    src/main_server.cpp
)

target_link_libraries(themis_server
    PRIVATE
        themis_core
)

# Tests
if(THEMIS_BUILD_TESTS)
    enable_testing()
    find_package(GTest CONFIG REQUIRED)
    
    add_executable(themis_tests
        tests/test_base_entity.cpp
        tests/test_secondary_index.cpp
        tests/test_composite_index.cpp
        tests/test_unique_index.cpp
        tests/test_range_index.cpp
        tests/test_sparse_geo_index.cpp
        tests/test_ttl_fulltext_index.cpp
    tests/test_index_stats.cpp
        tests/test_graph_index.cpp
        tests/test_vector_index.cpp
        tests/test_query_engine.cpp
        tests/test_stats_api.cpp
        tests/test_http_range_index.cpp
        tests/test_query_engine_range.cpp
        tests/test_metrics_api.cpp
        tests/test_http_query_range.cpp
        tests/test_http_aql.cpp
        tests/test_http_aql_graph.cpp
        tests/test_http_vector.cpp
        tests/test_transaction_manager.cpp
        tests/test_mvcc.cpp
        tests/test_aql_parser.cpp
        tests/test_aql_translator.cpp
        tests/test_text_processor.cpp
    )
    
    target_link_libraries(themis_tests
        PRIVATE
            themis_core
            GTest::gtest
            GTest::gtest_main
    )
    
    add_test(NAME ThemisTests COMMAND themis_tests)
endif()

# Benchmarks
if(THEMIS_BUILD_BENCHMARKS)
    find_package(benchmark CONFIG REQUIRED)
    
    add_executable(themis_benchmarks
        benchmarks/bench_crud.cpp
        benchmarks/bench_query.cpp
        benchmarks/bench_vector_search.cpp
    )
    
    target_link_libraries(themis_benchmarks
        PRIVATE
            themis_core
            benchmark::benchmark
            benchmark::benchmark_main
    )
    
    # Compression benchmark (standalone)
    add_executable(bench_compression
        benchmarks/bench_compression.cpp
    )
    
    target_link_libraries(bench_compression
        PRIVATE
            themis_core
            benchmark::benchmark
    )
    
    # Index rebuild benchmark (standalone)
    add_executable(bench_index_rebuild
        benchmarks/bench_index_rebuild.cpp
    )
    
    target_link_libraries(bench_index_rebuild
        PRIVATE
            themis_core
            benchmark::benchmark
    )
    
    # MVCC performance benchmark (standalone)
    add_executable(bench_mvcc
        benchmarks/bench_mvcc.cpp
    )
    
    target_link_libraries(bench_mvcc
        PRIVATE
            themis_core
            benchmark::benchmark
            benchmark::benchmark_main
    )
endif()

# Installation
install(TARGETS themis_server themis_core
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/
    DESTINATION include
)
