// ARM-Specific Performance Benchmarks
// Tests SIMD performance on ARM architectures with NEON optimizations
// Compares ARM NEON vs scalar performance across different vector dimensions

#include <benchmark/benchmark.h>
#include <random>
#include <vector>
#include <cmath>
#include <string>
#include <cstring>

#include "utils/simd_distance.h"

// Detect architecture at compile time
#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#define THEMIS_ARM_NEON 1
#endif

namespace {

// Random vector generator
std::vector<float> generateRandomVector(size_t dim, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::vector<float> v(dim);
    for (size_t i = 0; i < dim; ++i) {
        v[i] = dist(rng);
    }
    return v;
}

// Scalar implementation for comparison
float scalar_l2_distance_sq(const float* a, const float* b, size_t dim) {
    float acc = 0.0f;
    for (size_t i = 0; i < dim; ++i) {
        float d = a[i] - b[i];
        acc += d * d;
    }
    return acc;
}

float scalar_l2_distance(const float* a, const float* b, size_t dim) {
    return std::sqrt(scalar_l2_distance_sq(a, b, dim));
}

// Dot product for cosine similarity
float scalar_dot_product(const float* a, const float* b, size_t dim) {
    float acc = 0.0f;
    for (size_t i = 0; i < dim; ++i) {
        acc += a[i] * b[i];
    }
    return acc;
}

#ifdef THEMIS_ARM_NEON
// ARM NEON dot product implementation
float neon_dot_product(const float* a, const float* b, size_t dim) {
    size_t i = 0;
    float32x4_t acc0 = vdupq_n_f32(0.0f);
    float32x4_t acc1 = vdupq_n_f32(0.0f);
    const size_t step = 8;
    
    // Process 8 floats at a time
    for (; i + step <= dim; i += step) {
        float32x4_t va0 = vld1q_f32(a + i);
        float32x4_t vb0 = vld1q_f32(b + i);
        #if defined(__ARM_FEATURE_FMA)
        acc0 = vfmaq_f32(acc0, va0, vb0);
        #else
        acc0 = vmlaq_f32(acc0, va0, vb0);
        #endif
        
        float32x4_t va1 = vld1q_f32(a + i + 4);
        float32x4_t vb1 = vld1q_f32(b + i + 4);
        #if defined(__ARM_FEATURE_FMA)
        acc1 = vfmaq_f32(acc1, va1, vb1);
        #else
        acc1 = vmlaq_f32(acc1, va1, vb1);
        #endif
    }
    
    // Combine accumulators
    float32x4_t acc = vaddq_f32(acc0, acc1);
    
    // Horizontal sum
    float32x2_t sum2 = vadd_f32(vget_low_f32(acc), vget_high_f32(acc));
    float32x2_t sum1 = vpadd_f32(sum2, sum2);
    float res = vget_lane_f32(sum1, 0);
    
    // Handle remaining elements
    for (; i < dim; ++i) {
        res += a[i] * b[i];
    }
    
    return res;
}
#endif

} // namespace

// ============================================================================
// Benchmarks: L2 Distance (using library SIMD implementation)
// ============================================================================

static void BM_ARM_L2_Distance_SIMD(benchmark::State& state) {
    const size_t dim = static_cast<size_t>(state.range(0));
    auto vec_a = generateRandomVector(dim, 12345);
    auto vec_b = generateRandomVector(dim, 67890);
    
    for (auto _ : state) {
        float dist = themis::simd::l2_distance(vec_a.data(), vec_b.data(), dim);
        benchmark::DoNotOptimize(dist);
    }
    
    state.counters["dimension"] = static_cast<double>(dim);
    state.counters["ops_per_sec"] = benchmark::Counter(
        static_cast<double>(state.iterations()),
        benchmark::Counter::kIsRate
    );
    
#ifdef THEMIS_ARM_NEON
    state.SetLabel("ARM_NEON");
#else
    state.SetLabel("x86_SIMD_or_Scalar");
#endif
}

static void BM_ARM_L2_Distance_Scalar(benchmark::State& state) {
    const size_t dim = static_cast<size_t>(state.range(0));
    auto vec_a = generateRandomVector(dim, 12345);
    auto vec_b = generateRandomVector(dim, 67890);
    
    for (auto _ : state) {
        float dist = scalar_l2_distance(vec_a.data(), vec_b.data(), dim);
        benchmark::DoNotOptimize(dist);
    }
    
    state.counters["dimension"] = static_cast<double>(dim);
    state.counters["ops_per_sec"] = benchmark::Counter(
        static_cast<double>(state.iterations()),
        benchmark::Counter::kIsRate
    );
    state.SetLabel("Scalar_Reference");
}

// ============================================================================
// Benchmarks: Squared L2 Distance (avoids sqrt)
// ============================================================================

static void BM_ARM_L2_Distance_Squared_SIMD(benchmark::State& state) {
    const size_t dim = static_cast<size_t>(state.range(0));
    auto vec_a = generateRandomVector(dim, 11111);
    auto vec_b = generateRandomVector(dim, 22222);
    
    for (auto _ : state) {
        float dist_sq = themis::simd::l2_distance_sq(vec_a.data(), vec_b.data(), dim);
        benchmark::DoNotOptimize(dist_sq);
    }
    
    state.counters["dimension"] = static_cast<double>(dim);
#ifdef THEMIS_ARM_NEON
    state.SetLabel("ARM_NEON_squared");
#else
    state.SetLabel("SIMD_squared");
#endif
}

static void BM_ARM_L2_Distance_Squared_Scalar(benchmark::State& state) {
    const size_t dim = static_cast<size_t>(state.range(0));
    auto vec_a = generateRandomVector(dim, 11111);
    auto vec_b = generateRandomVector(dim, 22222);
    
    for (auto _ : state) {
        float dist_sq = scalar_l2_distance_sq(vec_a.data(), vec_b.data(), dim);
        benchmark::DoNotOptimize(dist_sq);
    }
    
    state.counters["dimension"] = static_cast<double>(dim);
    state.SetLabel("Scalar_squared");
}

// ============================================================================
// Benchmarks: Dot Product (for cosine similarity)
// ============================================================================

#ifdef THEMIS_ARM_NEON
static void BM_ARM_DotProduct_NEON(benchmark::State& state) {
    const size_t dim = static_cast<size_t>(state.range(0));
    auto vec_a = generateRandomVector(dim, 33333);
    auto vec_b = generateRandomVector(dim, 44444);
    
    for (auto _ : state) {
        float dot = neon_dot_product(vec_a.data(), vec_b.data(), dim);
        benchmark::DoNotOptimize(dot);
    }
    
    state.counters["dimension"] = static_cast<double>(dim);
    state.SetLabel("ARM_NEON_dotprod");
}
#endif

static void BM_ARM_DotProduct_Scalar(benchmark::State& state) {
    const size_t dim = static_cast<size_t>(state.range(0));
    auto vec_a = generateRandomVector(dim, 33333);
    auto vec_b = generateRandomVector(dim, 44444);
    
    for (auto _ : state) {
        float dot = scalar_dot_product(vec_a.data(), vec_b.data(), dim);
        benchmark::DoNotOptimize(dot);
    }
    
    state.counters["dimension"] = static_cast<double>(dim);
    state.SetLabel("Scalar_dotprod");
}

// ============================================================================
// Benchmarks: Batch Distance Calculations (common in vector search)
// ============================================================================

static void BM_ARM_Batch_L2_SIMD(benchmark::State& state) {
    const size_t dim = static_cast<size_t>(state.range(0));
    const size_t batch_size = 100;
    
    auto query = generateRandomVector(dim, 55555);
    std::vector<std::vector<float>> dataset;
    dataset.reserve(batch_size);
    for (size_t i = 0; i < batch_size; ++i) {
        dataset.push_back(generateRandomVector(dim, 66666 + i));
    }
    
    for (auto _ : state) {
        for (const auto& vec : dataset) {
            float dist = themis::simd::l2_distance(query.data(), vec.data(), dim);
            benchmark::DoNotOptimize(dist);
        }
    }
    
    state.counters["dimension"] = static_cast<double>(dim);
    state.counters["batch_size"] = static_cast<double>(batch_size);
    state.counters["vectors_per_sec"] = benchmark::Counter(
        static_cast<double>(state.iterations() * batch_size),
        benchmark::Counter::kIsRate
    );
#ifdef THEMIS_ARM_NEON
    state.SetLabel("ARM_NEON_batch");
#else
    state.SetLabel("SIMD_batch");
#endif
}

static void BM_ARM_Batch_L2_Scalar(benchmark::State& state) {
    const size_t dim = static_cast<size_t>(state.range(0));
    const size_t batch_size = 100;
    
    auto query = generateRandomVector(dim, 55555);
    std::vector<std::vector<float>> dataset;
    dataset.reserve(batch_size);
    for (size_t i = 0; i < batch_size; ++i) {
        dataset.push_back(generateRandomVector(dim, 66666 + i));
    }
    
    for (auto _ : state) {
        for (const auto& vec : dataset) {
            float dist = scalar_l2_distance(query.data(), vec.data(), dim);
            benchmark::DoNotOptimize(dist);
        }
    }
    
    state.counters["dimension"] = static_cast<double>(dim);
    state.counters["batch_size"] = static_cast<double>(batch_size);
    state.counters["vectors_per_sec"] = benchmark::Counter(
        static_cast<double>(state.iterations() * batch_size),
        benchmark::Counter::kIsRate
    );
    state.SetLabel("Scalar_batch");
}

// ============================================================================
// Register Benchmarks with Various Dimensions
// ============================================================================

// Common embedding dimensions
const std::vector<int64_t> DIMS = {64, 128, 256, 384, 512, 768, 1024, 1536};

// L2 Distance benchmarks
BENCHMARK(BM_ARM_L2_Distance_SIMD)->DenseRange(64, 1536, 128)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_ARM_L2_Distance_Scalar)->DenseRange(64, 1536, 128)->Unit(benchmark::kMicrosecond);

// Squared L2 Distance (faster, no sqrt)
BENCHMARK(BM_ARM_L2_Distance_Squared_SIMD)->DenseRange(64, 1536, 128)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_ARM_L2_Distance_Squared_Scalar)->DenseRange(64, 1536, 128)->Unit(benchmark::kMicrosecond);

// Dot product benchmarks (ARM NEON specific)
#ifdef THEMIS_ARM_NEON
BENCHMARK(BM_ARM_DotProduct_NEON)->DenseRange(64, 1536, 128)->Unit(benchmark::kMicrosecond);
#endif
BENCHMARK(BM_ARM_DotProduct_Scalar)->DenseRange(64, 1536, 128)->Unit(benchmark::kMicrosecond);

// Batch processing benchmarks
BENCHMARK(BM_ARM_Batch_L2_SIMD)->DenseRange(128, 768, 128)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_ARM_Batch_L2_Scalar)->DenseRange(128, 768, 128)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
