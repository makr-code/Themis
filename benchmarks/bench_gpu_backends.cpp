// Benchmark: GPU Backend Performance Comparison
// Compares CUDA, HIP, Metal, Vulkan, DirectX, and other acceleration backends

#include "acceleration/compute_backend.h"
#include "acceleration/backend_registry.h"
#include "acceleration/cpu_backend.h"
#ifdef THEMIS_ENABLE_CUDA
#include "acceleration/cuda_backend.h"
#endif
#ifdef THEMIS_ENABLE_HIP
#include "acceleration/hip_backend.h"
#endif
#ifdef THEMIS_ENABLE_VULKAN
#include "acceleration/vulkan_backend.h"
#endif
#ifdef THEMIS_ENABLE_METAL
#include "acceleration/metal_backend.h"
#endif
#ifdef THEMIS_ENABLE_OPENCL
#include "acceleration/opencl_backend.h"
#endif

#include <benchmark/benchmark.h>
#include <random>
#include <memory>
#include <vector>

using namespace themis::acceleration;

// ============================================================================
// Test Data Generation
// ============================================================================

struct BenchmarkData {
    std::vector<float> queries;
    std::vector<float> vectors;
    size_t num_queries;
    size_t num_vectors;
    size_t dim;
    
    BenchmarkData(size_t nq, size_t nv, size_t d) 
        : num_queries(nq), num_vectors(nv), dim(d) {
        
        std::mt19937 rng(42);
        std::normal_distribution<float> dist(0.0f, 1.0f);
        
        queries.resize(num_queries * dim);
        vectors.resize(num_vectors * dim);
        
        for (auto& v : queries) v = dist(rng);
        for (auto& v : vectors) v = dist(rng);
        
        // Normalize vectors
        for (size_t i = 0; i < num_queries; i++) {
            float norm = 0.0f;
            for (size_t j = 0; j < dim; j++) {
                float val = queries[i * dim + j];
                norm += val * val;
            }
            norm = std::sqrt(norm);
            if (norm > 0) {
                for (size_t j = 0; j < dim; j++) {
                    queries[i * dim + j] /= norm;
                }
            }
        }
        
        for (size_t i = 0; i < num_vectors; i++) {
            float norm = 0.0f;
            for (size_t j = 0; j < dim; j++) {
                float val = vectors[i * dim + j];
                norm += val * val;
            }
            norm = std::sqrt(norm);
            if (norm > 0) {
                for (size_t j = 0; j < dim; j++) {
                    vectors[i * dim + j] /= norm;
                }
            }
        }
    }
};

// ============================================================================
// CPU Backend Benchmark
// ============================================================================

static void BM_CPUBackend_DistanceComputation(benchmark::State& state) {
    const size_t num_queries = state.range(0);
    const size_t num_vectors = state.range(1);
    const size_t dim = 128;
    
    BenchmarkData data(num_queries, num_vectors, dim);
    
    auto backend = std::make_unique<CPUBackend>();
    if (!backend->initialize() || !backend->isAvailable()) {
        state.SkipWithError("CPU backend not available");
        return;
    }
    
    for (auto _ : state) {
        auto distances = backend->computeDistances(
            data.queries.data(),
            data.num_queries,
            data.dim,
            data.vectors.data(),
            data.num_vectors,
            true  // L2 distance
        );
        
        benchmark::DoNotOptimize(distances);
    }
    
    backend->shutdown();
    
    state.SetItemsProcessed(state.iterations() * num_queries * num_vectors);
    state.counters["backend"] = static_cast<int>(BackendType::CPU);
    state.counters["distances_per_sec"] = benchmark::Counter(
        state.iterations() * num_queries * num_vectors,
        benchmark::Counter::kIsRate
    );
}

BENCHMARK(BM_CPUBackend_DistanceComputation)
    ->Args({10, 1000})      // 10 queries, 1K vectors
    ->Args({100, 10000})    // 100 queries, 10K vectors
    ->Args({1000, 100000})  // 1K queries, 100K vectors
    ->Unit(benchmark::kMillisecond);

// ============================================================================
// CUDA Backend Benchmark (if available)
// ============================================================================

#ifdef THEMIS_ENABLE_CUDA
static void BM_CUDABackend_DistanceComputation(benchmark::State& state) {
    const size_t num_queries = state.range(0);
    const size_t num_vectors = state.range(1);
    const size_t dim = 128;
    
    BenchmarkData data(num_queries, num_vectors, dim);
    
    auto backend = std::make_unique<CUDABackend>();
    if (!backend->initialize() || !backend->isAvailable()) {
        state.SkipWithError("CUDA backend not available");
        return;
    }
    
    auto caps = backend->getCapabilities();
    
    for (auto _ : state) {
        auto distances = backend->computeDistances(
            data.queries.data(),
            data.num_queries,
            data.dim,
            data.vectors.data(),
            data.num_vectors,
            true
        );
        
        benchmark::DoNotOptimize(distances);
    }
    
    backend->shutdown();
    
    state.SetItemsProcessed(state.iterations() * num_queries * num_vectors);
    state.counters["backend"] = static_cast<int>(BackendType::CUDA);
    state.counters["compute_units"] = caps.computeUnits;
    state.counters["distances_per_sec"] = benchmark::Counter(
        state.iterations() * num_queries * num_vectors,
        benchmark::Counter::kIsRate
    );
}

BENCHMARK(BM_CUDABackend_DistanceComputation)
    ->Args({10, 1000})
    ->Args({100, 10000})
    ->Args({1000, 100000})
    ->Args({10000, 1000000})  // Large scale
    ->Unit(benchmark::kMillisecond);
#endif

// ============================================================================
// HIP Backend Benchmark (if available)
// ============================================================================

#ifdef THEMIS_ENABLE_HIP
static void BM_HIPBackend_DistanceComputation(benchmark::State& state) {
    const size_t num_queries = state.range(0);
    const size_t num_vectors = state.range(1);
    const size_t dim = 128;
    
    BenchmarkData data(num_queries, num_vectors, dim);
    
    auto backend = std::make_unique<HIPBackend>();
    if (!backend->initialize() || !backend->isAvailable()) {
        state.SkipWithError("HIP backend not available");
        return;
    }
    
    for (auto _ : state) {
        auto distances = backend->computeDistances(
            data.queries.data(),
            data.num_queries,
            data.dim,
            data.vectors.data(),
            data.num_vectors,
            true
        );
        
        benchmark::DoNotOptimize(distances);
    }
    
    backend->shutdown();
    
    state.SetItemsProcessed(state.iterations() * num_queries * num_vectors);
    state.counters["backend"] = static_cast<int>(BackendType::HIP);
}

BENCHMARK(BM_HIPBackend_DistanceComputation)
    ->Args({10, 1000})
    ->Args({100, 10000})
    ->Args({1000, 100000})
    ->Unit(benchmark::kMillisecond);
#endif

// ============================================================================
// Vulkan Backend Benchmark (if available)
// ============================================================================

#ifdef THEMIS_ENABLE_VULKAN
static void BM_VulkanBackend_DistanceComputation(benchmark::State& state) {
    const size_t num_queries = state.range(0);
    const size_t num_vectors = state.range(1);
    const size_t dim = 128;
    
    BenchmarkData data(num_queries, num_vectors, dim);
    
    auto backend = std::make_unique<VulkanBackend>();
    if (!backend->initialize() || !backend->isAvailable()) {
        state.SkipWithError("Vulkan backend not available");
        return;
    }
    
    for (auto _ : state) {
        auto distances = backend->computeDistances(
            data.queries.data(),
            data.num_queries,
            data.dim,
            data.vectors.data(),
            data.num_vectors,
            true
        );
        
        benchmark::DoNotOptimize(distances);
    }
    
    backend->shutdown();
    
    state.SetItemsProcessed(state.iterations() * num_queries * num_vectors);
    state.counters["backend"] = static_cast<int>(BackendType::VULKAN);
}

BENCHMARK(BM_VulkanBackend_DistanceComputation)
    ->Args({10, 1000})
    ->Args({100, 10000})
    ->Args({1000, 100000})
    ->Unit(benchmark::kMillisecond);
#endif

// ============================================================================
// Metal Backend Benchmark (if available)
// ============================================================================

#ifdef THEMIS_ENABLE_METAL
static void BM_MetalBackend_DistanceComputation(benchmark::State& state) {
    const size_t num_queries = state.range(0);
    const size_t num_vectors = state.range(1);
    const size_t dim = 128;
    
    BenchmarkData data(num_queries, num_vectors, dim);
    
    auto backend = std::make_unique<MetalBackend>();
    if (!backend->initialize() || !backend->isAvailable()) {
        state.SkipWithError("Metal backend not available");
        return;
    }
    
    for (auto _ : state) {
        auto distances = backend->computeDistances(
            data.queries.data(),
            data.num_queries,
            data.dim,
            data.vectors.data(),
            data.num_vectors,
            true
        );
        
        benchmark::DoNotOptimize(distances);
    }
    
    backend->shutdown();
    
    state.SetItemsProcessed(state.iterations() * num_queries * num_vectors);
    state.counters["backend"] = static_cast<int>(BackendType::METAL);
}

BENCHMARK(BM_MetalBackend_DistanceComputation)
    ->Args({10, 1000})
    ->Args({100, 10000})
    ->Args({1000, 100000})
    ->Unit(benchmark::kMillisecond);
#endif

// ============================================================================
// OpenCL Backend Benchmark (if available)
// ============================================================================

#ifdef THEMIS_ENABLE_OPENCL
static void BM_OpenCLBackend_DistanceComputation(benchmark::State& state) {
    const size_t num_queries = state.range(0);
    const size_t num_vectors = state.range(1);
    const size_t dim = 128;
    
    BenchmarkData data(num_queries, num_vectors, dim);
    
    auto backend = std::make_unique<OpenCLBackend>();
    if (!backend->initialize() || !backend->isAvailable()) {
        state.SkipWithError("OpenCL backend not available");
        return;
    }
    
    for (auto _ : state) {
        auto distances = backend->computeDistances(
            data.queries.data(),
            data.num_queries,
            data.dim,
            data.vectors.data(),
            data.num_vectors,
            true
        );
        
        benchmark::DoNotOptimize(distances);
    }
    
    backend->shutdown();
    
    state.SetItemsProcessed(state.iterations() * num_queries * num_vectors);
    state.counters["backend"] = static_cast<int>(BackendType::OPENCL);
}

BENCHMARK(BM_OpenCLBackend_DistanceComputation)
    ->Args({10, 1000})
    ->Args({100, 10000})
    ->Args({1000, 100000})
    ->Unit(benchmark::kMillisecond);
#endif

// ============================================================================
// Backend Comparison - Different Vector Dimensions
// ============================================================================

static void BM_BackendComparison_VaryingDimensions(benchmark::State& state) {
    const size_t num_queries = 100;
    const size_t num_vectors = 10000;
    const size_t dim = state.range(0);
    
    BenchmarkData data(num_queries, num_vectors, dim);
    
    // Use CPU backend for comparison
    auto backend = std::make_unique<CPUBackend>();
    if (!backend->initialize() || !backend->isAvailable()) {
        state.SkipWithError("Backend not available");
        return;
    }
    
    for (auto _ : state) {
        auto distances = backend->computeDistances(
            data.queries.data(),
            data.num_queries,
            data.dim,
            data.vectors.data(),
            data.num_vectors,
            true
        );
        
        benchmark::DoNotOptimize(distances);
    }
    
    backend->shutdown();
    
    state.SetItemsProcessed(state.iterations() * num_queries * num_vectors);
    state.counters["dimension"] = dim;
}

BENCHMARK(BM_BackendComparison_VaryingDimensions)
    ->Arg(64)
    ->Arg(128)
    ->Arg(256)
    ->Arg(512)
    ->Arg(1024)
    ->Arg(2048)
    ->Unit(benchmark::kMillisecond);

// ============================================================================
// Backend Initialization Overhead
// ============================================================================

static void BM_BackendInitializationOverhead(benchmark::State& state) {
    const int backend_type = state.range(0);
    
    for (auto _ : state) {
        std::unique_ptr<IComputeBackend> backend;
        
        switch (static_cast<BackendType>(backend_type)) {
            case BackendType::CPU:
                backend = std::make_unique<CPUBackend>();
                break;
#ifdef THEMIS_ENABLE_CUDA
            case BackendType::CUDA:
                backend = std::make_unique<CUDABackend>();
                break;
#endif
            default:
                state.SkipWithError("Backend not compiled");
                return;
        }
        
        auto start = std::chrono::high_resolution_clock::now();
        bool init_success = backend->initialize();
        auto end = std::chrono::high_resolution_clock::now();
        
        if (!init_success) {
            state.SkipWithError("Backend initialization failed");
        }
        
        auto init_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        benchmark::DoNotOptimize(init_time);
        
        backend->shutdown();
    }
    
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_BackendInitializationOverhead)
    ->Arg(static_cast<int>(BackendType::CPU))
#ifdef THEMIS_ENABLE_CUDA
    ->Arg(static_cast<int>(BackendType::CUDA))
#endif
    ->Unit(benchmark::kMicrosecond);

// ============================================================================
// Throughput Comparison
// ============================================================================

static void BM_ThroughputComparison(benchmark::State& state) {
    const size_t num_queries = 1000;
    const size_t num_vectors = 100000;
    const size_t dim = 128;
    
    BenchmarkData data(num_queries, num_vectors, dim);
    
    auto backend = std::make_unique<CPUBackend>();
    if (!backend->initialize() || !backend->isAvailable()) {
        state.SkipWithError("Backend not available");
        return;
    }
    
    int64_t total_distances = 0;
    
    for (auto _ : state) {
        auto distances = backend->computeDistances(
            data.queries.data(),
            data.num_queries,
            data.dim,
            data.vectors.data(),
            data.num_vectors,
            true
        );
        
        total_distances += distances.size();
        benchmark::DoNotOptimize(distances);
    }
    
    backend->shutdown();
    
    state.SetItemsProcessed(total_distances);
    state.counters["distances_per_sec"] = benchmark::Counter(
        total_distances, benchmark::Counter::kIsRate);
    state.counters["GB_per_sec"] = benchmark::Counter(
        total_distances * sizeof(float), benchmark::Counter::kIsRate);
}

BENCHMARK(BM_ThroughputComparison)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(5);

BENCHMARK_MAIN();
