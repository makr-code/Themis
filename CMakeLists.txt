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

# Dependencies
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

# Build options
option(THEMIS_BUILD_TESTS "Build unit tests" ON)
option(THEMIS_BUILD_BENCHMARKS "Build performance benchmarks" ON)
option(THEMIS_ENABLE_GPU "Enable GPU acceleration for vector search" OFF)
option(THEMIS_ENABLE_TRACING "Enable OpenTelemetry distributed tracing" ON)
option(THEMIS_ENABLE_ASAN "Enable AddressSanitizer for debugging" OFF)
option(THEMIS_STRICT_BUILD "Treat warnings as errors" OFF)

# Compiler warnings and optimizations
if(MSVC)
    add_compile_options(/W4 /utf-8 /wd4701)
    # Suppress unreachable code warnings from external headers (e.g., nlohmann/json)
    add_compile_options(/wd4702)
    if(THEMIS_STRICT_BUILD)
        add_compile_options(/WX)
    endif()
    add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS _WIN32_WINNT=0x0A00)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/O2 /GL)
        # Ensure Link-Time Code Generation to avoid link restart messages
        add_link_options(/LTCG)
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

# Optional: OpenTelemetry for distributed tracing
if(THEMIS_ENABLE_TRACING)
    find_package(opentelemetry-cpp CONFIG REQUIRED)
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
    src/index/property_graph.cpp
    src/index/gnn_embeddings.cpp
    src/index/vector_index.cpp
    src/index/adaptive_index.cpp
    
    src/security/mock_key_provider.cpp
    src/security/vault_key_provider.cpp
    src/security/key_cache.cpp
    src/security/field_encryption.cpp
    src/security/encrypted_field.cpp
    
    src/transaction/transaction_manager.cpp
    src/transaction/saga.cpp
    
    src/query/query_engine.cpp
    src/query/query_optimizer.cpp
    src/query/query_parser.cpp
    src/query/aql_parser.cpp
    src/query/aql_translator.cpp
    src/query/semantic_cache.cpp
    
    src/server/http_server.cpp
    src/server/sse_connection_manager.cpp
    
    src/content/content_type.cpp
    src/content/content_manager.cpp
    src/content/text_processor.cpp
    
    src/cache/semantic_cache.cpp
    src/llm/llm_interaction_store.cpp
    src/cdc/changefeed.cpp
    src/timeseries/timeseries.cpp
    src/timeseries/tsstore.cpp
    src/timeseries/gorilla.cpp
    src/timeseries/retention.cpp
    src/timeseries/continuous_agg.cpp
    
    src/utils/serialization.cpp
    src/utils/logger.cpp
    src/utils/cursor.cpp
    src/utils/tracing.cpp
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
        CURL::libcurl
    OpenSSL::SSL
    OpenSSL::Crypto
)

if(THEMIS_ENABLE_GPU)
    target_link_libraries(themis_core PUBLIC faiss)
    target_compile_definitions(themis_core PUBLIC THEMIS_GPU_ENABLED)
elseif(hnswlib_FOUND)
    target_link_libraries(themis_core PUBLIC hnswlib::hnswlib)
    target_compile_definitions(themis_core PUBLIC THEMIS_HNSW_ENABLED)
endif()

if(THEMIS_ENABLE_TRACING)
    target_link_libraries(themis_core PUBLIC
        opentelemetry-cpp::trace
        opentelemetry-cpp::otlp_http_exporter
    )
    target_compile_definitions(themis_core PUBLIC THEMIS_ENABLE_TRACING)
endif()

# Main executable (demo)
add_executable(themis_demo
    src/main.cpp
)

target_link_libraries(themis_demo
    PRIVATE
        themis_core
)

# Encryption demo executable
add_executable(themis_demo_encryption
    src/demo_encryption.cpp
)

target_link_libraries(themis_demo_encryption
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
        tests/test_query_or.cpp
        tests/test_stats_api.cpp
        tests/test_http_range_index.cpp
        tests/test_query_engine_range.cpp
        tests/test_metrics_api.cpp
        tests/test_http_query_range.cpp
        tests/test_http_aql.cpp
    tests/test_http_aql_join.cpp
    tests/test_http_aql_let.cpp
        tests/test_http_aql_collect.cpp
        tests/test_http_config.cpp
        tests/test_http_aql_graph.cpp
        tests/test_http_aql_collect.cpp
        tests/test_http_vector.cpp
        tests/test_http_vector_largescale.cpp
        tests/test_http_content.cpp
        tests/test_http_changefeed.cpp
        tests/test_http_changefeed_sse.cpp
        tests/test_transaction_manager.cpp
        tests/test_mvcc.cpp
        tests/test_aql_parser.cpp
        tests/test_aql_translator.cpp
        tests/test_text_processor.cpp
        tests/test_backup_restore.cpp
        tests/test_cursor.cpp
        tests/test_temporal_graph.cpp
        tests/test_recursive_path_query.cpp
        tests/test_timerange_query.cpp
        tests/test_property_graph.cpp
        tests/test_gnn_embeddings.cpp
        tests/test_semantic_cache.cpp
        tests/test_tsstore.cpp
        tests/test_gorilla.cpp
    tests/test_gorilla_probe.cpp
        tests/test_timeseries_retention.cpp
        tests/test_continuous_agg.cpp
        tests/test_adaptive_index.cpp
        tests/test_http_adaptive_index.cpp
        tests/test_encryption.cpp
        tests/test_vault_key_provider.cpp
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

    # Encryption performance benchmark (standalone)
    add_executable(bench_encryption
        benchmarks/bench_encryption.cpp
    )
    
    target_link_libraries(bench_encryption
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
