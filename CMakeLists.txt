cmake_minimum_required(VERSION 3.20)
project(Themis VERSION 1.0.0 LANGUAGES CXX)

# C++20 Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable CUDA if requested
if(THEMIS_ENABLE_CUDA)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
endif()

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# vcpkg integration
if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
endif()

# Dependencies
find_package(OpenSSL REQUIRED)

# Make CURL optional: some environments (CI/dev) may not have dev package installed.
# If found, create CURL::libcurl target; otherwise provide a fallback and continue.
find_package(CURL CONFIG)
if(NOT CURL_FOUND)
    find_package(CURL)
endif()
if(NOT CURL_FOUND)
    message(WARNING "CURL package not found; building without libcurl. Some features may be disabled.")
endif()

# Build options
option(THEMIS_BUILD_TESTS "Build unit tests" ON)
option(THEMIS_BUILD_BENCHMARKS "Build performance benchmarks" ON)
option(THEMIS_ENABLE_GPU "Enable GPU acceleration for vector search" OFF)
option(THEMIS_ENABLE_TRACING "Enable OpenTelemetry distributed tracing" ON)
option(THEMIS_ENABLE_ASAN "Enable AddressSanitizer for debugging" OFF)
option(THEMIS_STRICT_BUILD "Treat warnings as errors" OFF)
option(THEMIS_ENABLE_AVX2 "Enable AVX2/FMA for SIMD kernels in Release builds (MSVC)" ON)
option(THEMIS_ENABLE_HSM_REAL "Enable real PKCS#11 HSM provider (fallback to stub if OFF)" OFF)

# Hardware acceleration backend options
option(THEMIS_ENABLE_CUDA "Enable NVIDIA CUDA acceleration" OFF)
option(THEMIS_ENABLE_HIP "Enable AMD HIP acceleration" OFF)
option(THEMIS_ENABLE_ZLUDA "Enable AMD ZLUDA (CUDA compatibility on AMD)" OFF)
option(THEMIS_ENABLE_ROCM "Enable AMD ROCm acceleration" OFF)
option(THEMIS_ENABLE_DIRECTX "Enable DirectX 12 Compute Shaders (Windows)" OFF)
option(THEMIS_ENABLE_VULKAN "Enable Vulkan Compute acceleration" OFF)
option(THEMIS_ENABLE_OPENGL "Enable OpenGL Compute Shaders" OFF)
option(THEMIS_ENABLE_METAL "Enable Apple Metal acceleration" OFF)
option(THEMIS_ENABLE_ONEAPI "Enable Intel OneAPI/SYCL acceleration" OFF)
option(THEMIS_ENABLE_OPENCL "Enable OpenCL acceleration" OFF)
option(THEMIS_ENABLE_WEBGPU "Enable WebGPU acceleration (experimental)" OFF)

# Lokal aktivierbare Code-Qualität (clang-tidy) ohne CI-Kosten.
option(ENABLE_LOCAL_CLANG_TIDY "Enable local clang-tidy analysis (off by default)" OFF)


# Geo feature tiering options (Core vs. Enterprise add-ins)
option(THEMIS_GEO "Enable Geo core feature scaffolding" ON)
option(THEMIS_GEO_SIMD "Enable SIMD kernels for Geo (enterprise add-in)" OFF)
option(THEMIS_GEO_GPU "Enable GPU backend for Geo (enterprise add-in)" OFF)
option(THEMIS_GEO_H3 "Enable H3 integration (enterprise add-in)" OFF)
option(THEMIS_GEO_GEOS_PLUGIN "Enable GEOS prepared geometries plugin (enterprise add-in)" OFF)
option(THEMIS_ENTERPRISE "Enable enterprise capabilities bundle" OFF)

# Compiler warnings and optimizations
if(MSVC)
    add_compile_options(/W4 /utf-8 /wd4701 /bigobj /FS)
    # /FS: Allow parallel PDB writes to avoid C1041 errors
    # Suppress unreachable code warnings from external headers (e.g., nlohmann/json)
    add_compile_options(/wd4702)
    if(THEMIS_STRICT_BUILD)
        add_compile_options(/WX)
    endif()
    add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS _WIN32_WINNT=0x0A00)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/O2 /GL)
        if(THEMIS_ENABLE_AVX2)
            add_compile_options(/arch:AVX2)
        endif()
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

# Optionale lokale clang-tidy Aktivierung (nicht in CI standardmäßig).
if(ENABLE_LOCAL_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        message(STATUS "clang-tidy aktiviert: ${CLANG_TIDY_EXE}")
        # Keine automatischen Fixes hier; Entwickler nutzen Skript für -fix.
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
    else()
        message(WARNING "ENABLE_LOCAL_CLANG_TIDY=ON aber clang-tidy nicht gefunden.")
    endif()
endif()

# CMake helper function to safely link required targets
# This function checks if a target exists before attempting to link it
# and provides a clear error message if the target is missing.
# Usage: link_required(target_name <PRIVATE|PUBLIC|INTERFACE> dependency_target [additional_targets...])
# Example: link_required(themis_core PRIVATE SomePackage::component)
# Note: This helper is provided for future use. Current code uses explicit target
# checks before target_link_libraries to maintain minimal changes to existing code.
function(link_required target_name visibility dependency_target)
    if(NOT TARGET ${dependency_target})
        message(FATAL_ERROR "Required CMake target '${dependency_target}' not found for '${target_name}'. "
                            "Ensure the corresponding package was found successfully and the target is available.")
    endif()
    # Check additional targets in ARGN if any
    foreach(extra_target IN LISTS ARGN)
        if(NOT TARGET ${extra_target})
            message(FATAL_ERROR "Required CMake target '${extra_target}' not found for '${target_name}'. "
                                "Ensure the corresponding package was found successfully and the target is available.")
        endif()
    endforeach()
    target_link_libraries(${target_name} ${visibility} ${dependency_target} ${ARGN})
endfunction()

# Find required packages
find_package(RocksDB CONFIG)
# If vcpkg-provided CMake config for RocksDB is not available, try to locate system
# library and headers and create an imported target so the rest of the CMake script
# can link against RocksDB::rocksdb transparently.
if(NOT TARGET RocksDB::rocksdb)
    find_library(ROCKSDB_LIB rocksdb)
    find_path(ROCKSDB_INCLUDE_DIR rocksdb/db.h)
    if(ROCKSDB_LIB AND ROCKSDB_INCLUDE_DIR)
        add_library(RocksDB::rocksdb UNKNOWN IMPORTED)
        set_target_properties(RocksDB::rocksdb PROPERTIES
            IMPORTED_LOCATION "${ROCKSDB_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${ROCKSDB_INCLUDE_DIR}")
        message(STATUS "Using system RocksDB library at ${ROCKSDB_LIB} and include ${ROCKSDB_INCLUDE_DIR}")
    else()
        message(FATAL_ERROR "RocksDB not found. Install via vcpkg or system package (librocksdb-dev).")
    endif()
endif()

# Core packages via vcpkg (or system where available)
find_package(simdjson CONFIG REQUIRED)
find_package(TBB CONFIG REQUIRED)
find_package(Arrow CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

# yaml-cpp might be found via PkgConfig or CONFIG
find_package(yaml-cpp CONFIG QUIET)
if(yaml-cpp_FOUND)
    message(STATUS "yaml-cpp found via CONFIG")
    if(NOT TARGET yaml-cpp::yaml-cpp AND TARGET yaml-cpp)
        # Some versions export as 'yaml-cpp' instead of 'yaml-cpp::yaml-cpp'
        add_library(yaml-cpp::yaml-cpp ALIAS yaml-cpp)
    endif()
else()
    # Try pkg-config fallback
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(YAMLCPP QUIET yaml-cpp)
        if(YAMLCPP_FOUND)
            message(STATUS "yaml-cpp found via PkgConfig")
            add_library(yaml-cpp::yaml-cpp INTERFACE IMPORTED)
            set_target_properties(yaml-cpp::yaml-cpp PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${YAMLCPP_INCLUDE_DIRS}"
                INTERFACE_LINK_LIBRARIES "${YAMLCPP_LIBRARIES}")
        endif()
    endif()
endif()

if(NOT TARGET yaml-cpp::yaml-cpp)
    message(FATAL_ERROR "yaml-cpp not found via CONFIG or PkgConfig")
endif()

# Defensive check: Ensure Boost::system target exists after find_package
if(NOT TARGET Boost::system)
    message(FATAL_ERROR "Required CMake target 'Boost::system' not found. "
                        "Ensure Boost was found and the 'system' component is available.")
endif()
# Optional ZSTD for content compression wrappers
# Note: ZSTD packages may expose different target names depending on the build system
# This defensive logic checks for multiple possible target names
find_package(zstd CONFIG)
if(zstd_FOUND)
    set(THEMIS_ZSTD_TARGET zstd::libzstd_shared)
    if(NOT TARGET zstd::libzstd_shared AND TARGET zstd::libzstd)
        set(THEMIS_ZSTD_TARGET zstd::libzstd)
    endif()
elseif(NOT zstd_FOUND)
    find_package(ZSTD CONFIG)
    if(ZSTD_FOUND)
        set(THEMIS_ZSTD_TARGET ZSTD::ZSTD)
    endif()
endif()

# Optional: HNSW (if not using Faiss)
find_package(hnswlib CONFIG)

# Optional: Faiss for GPU support (graceful degradation if not found)
if(THEMIS_ENABLE_GPU)
    find_package(faiss CONFIG)
    if(NOT faiss_FOUND)
        message(WARNING "Faiss nicht gefunden – GPU-Unterstützung wird automatisch deaktiviert.")
        set(THEMIS_ENABLE_GPU OFF CACHE BOOL "Enable GPU acceleration for vector search" FORCE)
    endif()
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

# Copy runtime config files into the build directory so tests and local runs
# can find `config/*` when executed from ${CMAKE_BINARY_DIR} (common when
# running tests via CTest/gtest_discover_tests). This is safe and cheap.
if(EXISTS "${CMAKE_SOURCE_DIR}/config")
    file(COPY "${CMAKE_SOURCE_DIR}/config" DESTINATION "${CMAKE_BINARY_DIR}")
endif()

# Core library sources
set(THEMIS_CORE_SOURCES
    src/storage/rocksdb_wrapper.cpp
    src/storage/base_entity.cpp
    src/storage/key_schema.cpp
    src/storage/backup_manager.cpp
    
    src/index/secondary_index.cpp
    src/index/graph_index.cpp
    src/index/temporal_graph.cpp
    src/index/property_graph.cpp
    src/index/gnn_embeddings.cpp
    src/index/vector_index.cpp
    src/index/adaptive_index.cpp
    
    src/security/mock_key_provider.cpp
    src/security/vault_key_provider.cpp
    src/security/key_cache.cpp
    src/security/keyprovider_signing.cpp
    src/security/vault_signing_provider.cpp
    src/security/field_encryption.cpp
    src/security/encrypted_field.cpp
    
    src/transaction/transaction_manager.cpp
    src/transaction/saga.cpp
    
    src/query/query_engine.cpp
    src/query/query_optimizer.cpp
    src/query/aql_parser.cpp
    src/query/aql_translator.cpp
    src/query/aql_runner.cpp
    src/query/let_evaluator.cpp
    src/query/window_evaluator.cpp
    src/query/cte_subquery.cpp
    src/query/cte_cache.cpp
    src/query/statistical_aggregator.cpp
    src/query/semantic_cache.cpp
    
    src/server/http_server.cpp
    src/server/sse_connection_manager.cpp
    src/server/audit_api_handler.cpp
    src/server/pki_api_handler.cpp
    src/server/saga_api_handler.cpp
    src/server/pii_api_handler.cpp
    src/server/retention_api_handler.cpp
    src/server/keys_api_handler.cpp
    src/server/classification_api_handler.cpp
    src/server/reports_api_handler.cpp
    src/server/ranger_adapter.cpp
    src/server/policy_engine.cpp
    src/server/auth_middleware.cpp
    src/server/rate_limiter.cpp
    src/server/update_api_handler.cpp
    src/server/hot_reload_api_handler.cpp
    
    src/content/content_type.cpp
    src/content/content_manager.cpp
    src/content/text_processor.cpp
    src/content/mock_clip_processor.cpp
    
    src/cache/semantic_cache.cpp
    src/llm/llm_interaction_store.cpp
    src/llm/prompt_manager.cpp
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
    src/utils/zstd_codec.cpp
    src/utils/pki_client.cpp
    src/utils/input_validator.cpp
    src/utils/audit_logger.cpp
    src/utils/pii_detection_engine.cpp
    src/utils/regex_detection_engine.cpp
    src/utils/pii_detector.cpp
    src/utils/retention_manager.cpp
    src/utils/hkdf_helper.cpp
    src/utils/hkdf_cache.cpp
    src/utils/saga_logger.cpp
    src/utils/lek_manager.cpp
    src/utils/stemmer.cpp
    src/utils/stopwords.cpp
    src/utils/pii_pseudonymizer.cpp
    src/utils/normalizer.cpp
    src/utils/simd_distance.cpp
    src/utils/update_checker.cpp
    
    # Updates (hot-reload)
    src/updates/release_manifest.cpp
    src/updates/manifest_database.cpp
    src/updates/hot_reload_engine.cpp
    src/updates/updates_config.cpp
    
    # Geo utilities
    src/utils/geo/ewkb.cpp
    
    # Spatial index
    src/index/spatial_index.cpp
    src/security/pki_key_provider.cpp
    src/security/cms_signing.cpp
    src/security/rbac.cpp
    src/security/hsm_provider.cpp
    src/security/timestamp_authority.cpp
    src/auth/jwt_validator.cpp
    src/governance/policy_engine.cpp
    # Geo scaffolding (headers + stubs; safe no-ops until used)
    src/geo/cpu_backend.cpp
    src/geo/gpu_backend_stub.cpp
    src/geo/boost_cpu_exact_backend.cpp
    src/api/geo_index_hooks.cpp
    
    # Sharding (horizontal scaling)
    src/sharding/urn.cpp
    src/sharding/consistent_hash.cpp
    src/sharding/shard_topology.cpp
    src/sharding/urn_resolver.cpp
    src/sharding/pki_shard_certificate.cpp
    src/sharding/mtls_client.cpp
    src/sharding/signed_request.cpp
    src/sharding/remote_executor.cpp
    src/sharding/shard_router.cpp
    src/sharding/rebalance_operation.cpp
    src/sharding/data_migrator.cpp
    src/sharding/prometheus_metrics.cpp
    src/sharding/health_check.cpp
    src/sharding/admin_api.cpp
    
    # Hardware acceleration (core abstraction layer - always included)
    src/acceleration/backend_registry.cpp
    src/acceleration/cpu_backend.cpp
    src/acceleration/plugin_loader.cpp
    src/acceleration/plugin_security.cpp
)

# Add hardware acceleration backends based on build options
set(THEMIS_ACCELERATION_SOURCES "")

# CUDA backend
if(THEMIS_ENABLE_CUDA)
    find_package(CUDAToolkit REQUIRED)
    
    # Add CUDA source files
    list(APPEND THEMIS_ACCELERATION_SOURCES 
        src/acceleration/cuda_backend.cpp
        src/acceleration/cuda/vector_kernels.cu
    )
    
    target_compile_definitions(themis_core PUBLIC THEMIS_ENABLE_CUDA)
    target_include_directories(themis_core PRIVATE ${CUDAToolkit_INCLUDE_DIRS})
    target_link_libraries(themis_core PRIVATE CUDA::cudart CUDA::cuda_driver)
    
    message(STATUS "CUDA acceleration enabled")
    message(STATUS "  CUDA Version: ${CUDAToolkit_VERSION}")
    message(STATUS "  CUDA Include: ${CUDAToolkit_INCLUDE_DIRS}")
endif()

# Graphics API backends
if(THEMIS_ENABLE_DIRECTX OR THEMIS_ENABLE_VULKAN OR THEMIS_ENABLE_OPENGL)
    list(APPEND THEMIS_ACCELERATION_SOURCES src/acceleration/graphics_backends.cpp)
    
    if(THEMIS_ENABLE_DIRECTX)
        target_compile_definitions(themis_core PUBLIC THEMIS_ENABLE_DIRECTX)
        message(STATUS "DirectX acceleration enabled (stub)")
    endif()
    
    if(THEMIS_ENABLE_VULKAN)
        # Try to find Vulkan SDK
        find_package(Vulkan QUIET)
        if(Vulkan_FOUND)
            target_compile_definitions(themis_core PUBLIC THEMIS_ENABLE_VULKAN)
            target_include_directories(themis_core PRIVATE ${Vulkan_INCLUDE_DIRS})
            message(STATUS "Vulkan acceleration enabled")
            message(STATUS "  Vulkan Version: ${Vulkan_VERSION}")
        else()
            message(WARNING "Vulkan SDK not found, Vulkan support disabled")
        endif()
    endif()
    
    if(THEMIS_ENABLE_OPENGL)
        target_compile_definitions(themis_core PUBLIC THEMIS_ENABLE_OPENGL)
        message(STATUS "OpenGL acceleration enabled (stub)")
    endif()
endif()

# Add acceleration sources to core if any were selected
if(THEMIS_ACCELERATION_SOURCES)
    target_sources(themis_core PRIVATE ${THEMIS_ACCELERATION_SOURCES})
endif()

# Main library
add_library(themis_core STATIC ${THEMIS_CORE_SOURCES})

if(THEMIS_ENABLE_HSM_REAL)
    target_compile_definitions(themis_core PUBLIC THEMIS_ENABLE_HSM_REAL)
    target_sources(themis_core PRIVATE src/security/hsm_provider_pkcs11.cpp)
endif()

# Geo feature flags and Boost.Geometry support
if(THEMIS_GEO)
    target_compile_definitions(themis_core PUBLIC THEMIS_GEO_ENABLED)
    # Try to find Boost for Boost.Geometry backend
    find_package(Boost QUIET)
    if(Boost_FOUND)
        target_compile_definitions(themis_core PRIVATE THEMIS_GEO_BOOST_BACKEND)
        target_include_directories(themis_core PRIVATE ${Boost_INCLUDE_DIRS})
        message(STATUS "Boost.Geometry enabled for exact geometry checks")
    else()
        message(STATUS "Boost not found - Boost.Geometry backend disabled (will use MBR-only)")
    endif()
endif()

# Defensive selection of Arrow linking target: prefer shared, fall back to static.
if(TARGET Arrow::arrow_shared)
    set(THEMIS_ARROW_TARGET Arrow::arrow_shared)
elseif(TARGET Arrow::arrow_static)
    set(THEMIS_ARROW_TARGET Arrow::arrow_static)
else()
    set(THEMIS_ARROW_TARGET "")
endif()

target_link_libraries(themis_core
    PUBLIC
        RocksDB::rocksdb
        simdjson::simdjson
        TBB::tbb
        ${THEMIS_ARROW_TARGET}
        fmt::fmt
        spdlog::spdlog
        Boost::system
        nlohmann_json::nlohmann_json
    $<$<BOOL:${CURL_FOUND}>:CURL::libcurl>
        yaml-cpp::yaml-cpp
    OpenSSL::SSL
    OpenSSL::Crypto
    TBB::tbb
)

# Link ZSTD if available and define compile flag
if(DEFINED THEMIS_ZSTD_TARGET)
    target_link_libraries(themis_core PUBLIC ${THEMIS_ZSTD_TARGET})
    target_compile_definitions(themis_core PUBLIC THEMIS_HAS_ZSTD)
endif()

# Define THEMIS_ENABLE_CURL if CURL is found
if(CURL_FOUND)
    target_compile_definitions(themis_core PUBLIC THEMIS_ENABLE_CURL)
endif()

# Define version string for runtime use
target_compile_definitions(themis_core PUBLIC 
    THEMIS_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    THEMIS_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    THEMIS_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    THEMIS_VERSION_STRING="${PROJECT_VERSION}"
)

if(THEMIS_ENABLE_GPU)
    target_link_libraries(themis_core PUBLIC faiss)
    target_compile_definitions(themis_core PUBLIC THEMIS_GPU_ENABLED)
elseif(hnswlib_FOUND)
    target_link_libraries(themis_core PUBLIC hnswlib::hnswlib)
    target_compile_definitions(themis_core PUBLIC THEMIS_HNSW_ENABLED)
endif()

# Propagate Geo feature tiering compile definitions
if(THEMIS_GEO)
    target_compile_definitions(themis_core PUBLIC THEMIS_GEO_ENABLED)
endif()
if(THEMIS_GEO_SIMD)
    target_compile_definitions(themis_core PUBLIC THEMIS_GEO_SIMD_ENABLED)
endif()
if(THEMIS_GEO_GPU)
    target_compile_definitions(themis_core PUBLIC THEMIS_GEO_GPU_ENABLED)
endif()
if(THEMIS_GEO_H3)
    target_compile_definitions(themis_core PUBLIC THEMIS_GEO_H3_ENABLED)
endif()
if(THEMIS_GEO_GEOS_PLUGIN)
    target_compile_definitions(themis_core PUBLIC THEMIS_GEO_GEOS_PLUGIN_ENABLED)
endif()
if(THEMIS_ENTERPRISE)
    target_compile_definitions(themis_core PUBLIC THEMIS_ENTERPRISE_ENABLED)
endif()

if(THEMIS_ENABLE_TRACING)
    # Defensive checks: Ensure OpenTelemetry targets exist before linking
    if(NOT TARGET opentelemetry-cpp::trace)
        message(FATAL_ERROR "Required CMake target 'opentelemetry-cpp::trace' not found. "
                            "Ensure opentelemetry-cpp was found successfully.")
    endif()
    if(NOT TARGET opentelemetry-cpp::otlp_http_exporter)
        message(FATAL_ERROR "Required CMake target 'opentelemetry-cpp::otlp_http_exporter' not found. "
                            "Ensure opentelemetry-cpp was found successfully with otlp-http feature.")
    endif()
    
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
        tests/test_env.cpp
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
              tests/test_stemming.cpp
              tests/test_stopwords.cpp
              tests/test_normalization.cpp
              tests/test_phrase_search.cpp
              tests/test_aql_fulltext.cpp
              tests/test_aql_or.cpp
              tests/test_aql_fulltext_hybrid.cpp
              tests/test_aql_bm25.cpp
              tests/test_aql_similarity.cpp
              tests/test_aql_similarity_dispatch.cpp
              tests/test_aql_proximity.cpp
              tests/test_aql_proximity_dispatch.cpp
        tests/test_metrics_api.cpp
        tests/test_http_query_range.cpp
        tests/test_http_aql.cpp
        tests/test_http_aql_fulltext_score.cpp
        tests/test_http_aql_fulltext_or.cpp
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
        tests/test_http_timeseries.cpp
    tests/test_stemming.cpp
        tests/test_stopwords.cpp
        tests/test_http_governance.cpp
        
        # Geo tests
        tests/geo/test_geo_ewkb.cpp
        tests/geo/test_spatial_index.cpp
        tests/geo/test_aql_st_functions.cpp
        tests/geo/test_aql_st_queryengine.cpp
        tests/test_geo_index_integration.cpp
        tests/test_aql_let_st.cpp
        tests/test_hybrid_queries.cpp
        tests/test_http_audit.cpp
        tests/test_transaction_manager.cpp
        tests/test_transaction_isolation.cpp
        tests/test_changefeed_ordering.cpp
        tests/test_gorilla_codec_edge_cases.cpp
        tests/test_saga_concurrent_execution.cpp
        tests/test_consistent_hash_distribution.cpp
        tests/test_vulkan_backend.cpp
        tests/test_postgres_importer_datatypes.cpp
        tests/test_plugin_lifecycle.cpp
        tests/test_mvcc.cpp
        tests/test_aql_parser.cpp
        tests/test_aql_with_clause.cpp
        tests/test_aql_subqueries.cpp
        tests/test_cte_cache.cpp
        tests/test_aql_translator.cpp
        tests/test_text_processor.cpp
    tests/test_prompt_manager.cpp
    tests/test_mock_clip.cpp
        tests/test_backup_restore.cpp
        tests/test_cursor.cpp
        tests/test_temporal_graph.cpp
        tests/test_temporal_aggregation.cpp
    tests/test_temporal_aggregation_property.cpp
        tests/test_recursive_path_query.cpp
        tests/test_timerange_query.cpp
        tests/test_property_graph.cpp
        tests/test_graph_type_filtering.cpp
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
    tests/test_hkdf_cache.cpp
    tests/test_field_encryption_batch.cpp
    tests/test_pki_eidas.cpp
    tests/test_pki_api_handler.cpp
    tests/test_wal_archiving.cpp
        tests/test_encryption_e2e.cpp
        tests/test_vault_key_provider.cpp
        tests/test_vault_key_provider_retry.cpp
    tests/test_wal_backup_manager.cpp
        tests/test_audit_logger.cpp
        tests/test_pii_detector.cpp
        tests/test_retention_manager.cpp
        tests/test_saga_logger.cpp
        tests/test_http_retention_api.cpp
        tests/test_policy_yaml.cpp
        tests/test_pki_client.cpp
    tests/test_cms_signing.cpp
        tests/test_keyprovider_signing.cpp
    tests/test_vault_signing_provider.cpp
        tests/test_http_policies_export.cpp
        tests/test_pii_soft_delete.cpp
        tests/test_http_pii_lazy_init.cpp
    tests/test_http_pii_manager_new.cpp
    tests/test_jwt_validator.cpp
    tests/test_jwt_integration.cpp
        tests/test_jwt_rotation_unit.cpp
        tests/test_pki_client_rest.cpp
        tests/test_group_dek.cpp
        tests/test_input_validator.cpp
        tests/test_sharding_core.cpp
        tests/test_pki_shard_certificate.cpp
        tests/test_mtls_client.cpp
        tests/test_signed_request.cpp
        tests/test_shard_communication.cpp
        tests/test_rebalance_migration.cpp
        tests/test_monitoring_ops.cpp
        tests/update_checker_test.cpp
    )
    
    target_link_libraries(themis_tests
        PRIVATE
            themis_core
            GTest::gtest
            GTest::gtest_main
    )

    # Discover individual GoogleTest tests for better CTest integration
    include(GoogleTest)
    # On multi-config generators (MSVC), ensure correct working dir for runtime assets
    if(CMAKE_CONFIGURATION_TYPES)
        set(_gtest_workdir "$<TARGET_FILE_DIR:themis_tests>")
    else()
        set(_gtest_workdir ${CMAKE_CURRENT_BINARY_DIR})
    endif()
    gtest_discover_tests(themis_tests
        DISCOVERY_MODE PRE_TEST
        WORKING_DIRECTORY ${_gtest_workdir}
        EXTRA_ARGS --gtest_color=yes
    )

    # Note: Do not add an aggregate test that runs the entire suite again.
    # gtest_discover_tests already registers each test case with CTest. Adding
    # an extra `add_test(NAME ThemisTests COMMAND themis_tests ...)` would run
    # the full suite a second time and can make CI appear to hang on the last
    # test (previously "Test #619: ThemisTests").
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

    # Hybrid AQL Syntax Sugar benchmark (Phase 2.5)
    add_executable(bench_hybrid_aql_sugar
        benchmarks/bench_hybrid_aql_sugar.cpp
    )
    
    target_link_libraries(bench_hybrid_aql_sugar
        PRIVATE
            themis_core
            benchmark::benchmark
    )

    # HSM Provider performance benchmark (standalone)
    add_executable(bench_hsm_provider
        benchmarks/bench_hsm_provider.cpp
    )
    
    target_link_libraries(bench_hsm_provider
        PRIVATE
            themis_core
            benchmark::benchmark
            benchmark::benchmark_main
    )

    # Phase 1 Benchmarks - Critical Performance Testing

    # Shard Routing benchmark
    add_executable(bench_shard_routing
        benchmarks/bench_shard_routing.cpp
    )
    
    target_link_libraries(bench_shard_routing
        PRIVATE
            themis_core
            benchmark::benchmark
            benchmark::benchmark_main
    )

    # Transaction Throughput benchmark
    add_executable(bench_transaction_throughput
        benchmarks/bench_transaction_throughput.cpp
    )
    
    target_link_libraries(bench_transaction_throughput
        PRIVATE
            themis_core
            benchmark::benchmark
            benchmark::benchmark_main
    )

    # Changefeed Throughput benchmark
    add_executable(bench_changefeed_throughput
        benchmarks/bench_changefeed_throughput.cpp
    )
    
    target_link_libraries(bench_changefeed_throughput
        PRIVATE
            themis_core
            benchmark::benchmark
            benchmark::benchmark_main
    )

    # Timeseries Ingestion benchmark
    add_executable(bench_timeseries_ingestion
        benchmarks/bench_timeseries_ingestion.cpp
    )
    
    target_link_libraries(bench_timeseries_ingestion
        PRIVATE
            themis_core
            benchmark::benchmark
            benchmark::benchmark_main
    )

    # SAGA Compensation benchmark
    add_executable(bench_saga_compensation
        benchmarks/bench_saga_compensation.cpp
    )
    
    target_link_libraries(bench_saga_compensation
        PRIVATE
            themis_core
            benchmark::benchmark
            benchmark::benchmark_main
    )

    # Phase 2 Benchmarks - GPU Backends and Graph Analytics

    # Graph Traversal benchmark
    add_executable(bench_graph_traversal
        benchmarks/bench_graph_traversal.cpp
    )
    
    target_link_libraries(bench_graph_traversal
        PRIVATE
            themis_core
            benchmark::benchmark
            benchmark::benchmark_main
    )

    # PageRank benchmark
    add_executable(bench_pagerank
        benchmarks/bench_pagerank.cpp
    )
    
    target_link_libraries(bench_pagerank
        PRIVATE
            themis_core
            benchmark::benchmark
            benchmark::benchmark_main
    )

    # GPU Backends benchmark
    add_executable(bench_gpu_backends
        benchmarks/bench_gpu_backends.cpp
    )
    
    target_link_libraries(bench_gpu_backends
        PRIVATE
            themis_core
            benchmark::benchmark
            benchmark::benchmark_main
    )

    # GNN Embeddings benchmark
    add_executable(bench_gnn_embeddings
        benchmarks/bench_gnn_embeddings.cpp
    )
    
    target_link_libraries(bench_gnn_embeddings
        PRIVATE
            themis_core
            benchmark::benchmark
            benchmark::benchmark_main
    )
    
    # Phase 3: Content Management & Governance Benchmarks
    add_executable(bench_content_versioning
        benchmarks/bench_content_versioning.cpp
    )
    
    target_link_libraries(bench_content_versioning
        PRIVATE
            themis_core
            benchmark::benchmark
            benchmark::benchmark_main
    )
    
    add_executable(bench_text_extraction
        benchmarks/bench_text_extraction.cpp
    )
    
    target_link_libraries(bench_text_extraction
        PRIVATE
            themis_core
            benchmark::benchmark
            benchmark::benchmark_main
    )
    
    add_executable(bench_policy_evaluation
        benchmarks/bench_policy_evaluation.cpp
    )
    
    target_link_libraries(bench_policy_evaluation
        PRIVATE
            themis_core
            benchmark::benchmark
            benchmark::benchmark_main
    )
endif()

# Admin Tools (.NET)
option(THEMIS_BUILD_ADMIN_TOOLS "Build .NET admin tools (requires .NET SDK 8.0+)" OFF)
if(THEMIS_BUILD_ADMIN_TOOLS)
    add_subdirectory(tools)
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
