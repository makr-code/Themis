// ARM Memory Access Pattern Benchmarks
// Tests cache-friendly vs cache-unfriendly access patterns on ARM
// Useful for Raspberry Pi performance tuning

#include <benchmark/benchmark.h>
#include <random>
#include <vector>
#include <algorithm>
#include <cstring>

namespace {

// Generate random indices for scatter/gather patterns
std::vector<size_t> generateRandomIndices(size_t count, size_t max_val, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<size_t> dist(0, max_val - 1);
    std::vector<size_t> indices(count);
    for (size_t i = 0; i < count; ++i) {
        indices[i] = dist(rng);
    }
    return indices;
}

} // namespace

// ============================================================================
// Sequential Access (Cache-Friendly)
// ============================================================================

static void BM_ARM_Sequential_Read(benchmark::State& state) {
    const size_t size = static_cast<size_t>(state.range(0));
    std::vector<float> data(size, 1.0f);
    
    for (auto _ : state) {
        float sum = 0.0f;
        for (size_t i = 0; i < size; ++i) {
            sum += data[i];
        }
        benchmark::DoNotOptimize(sum);
    }
    
    state.counters["size_kb"] = static_cast<double>(size * sizeof(float)) / 1024.0;
    state.counters["bandwidth_mb_s"] = benchmark::Counter(
        static_cast<double>(state.iterations() * size * sizeof(float)),
        benchmark::Counter::kIsRate,
        benchmark::Counter::kIs1024
    );
    state.SetLabel("sequential_read");
}

static void BM_ARM_Sequential_Write(benchmark::State& state) {
    const size_t size = static_cast<size_t>(state.range(0));
    std::vector<float> data(size);
    
    for (auto _ : state) {
        for (size_t i = 0; i < size; ++i) {
            data[i] = static_cast<float>(i);
        }
        benchmark::ClobberMemory();
    }
    
    state.counters["size_kb"] = static_cast<double>(size * sizeof(float)) / 1024.0;
    state.counters["bandwidth_mb_s"] = benchmark::Counter(
        static_cast<double>(state.iterations() * size * sizeof(float)),
        benchmark::Counter::kIsRate,
        benchmark::Counter::kIs1024
    );
    state.SetLabel("sequential_write");
}

// ============================================================================
// Random Access (Cache-Unfriendly)
// ============================================================================

static void BM_ARM_Random_Read(benchmark::State& state) {
    const size_t size = static_cast<size_t>(state.range(0));
    std::vector<float> data(size, 1.0f);
    auto indices = generateRandomIndices(size / 4, size, 12345);
    
    for (auto _ : state) {
        float sum = 0.0f;
        for (size_t idx : indices) {
            sum += data[idx];
        }
        benchmark::DoNotOptimize(sum);
    }
    
    state.counters["size_kb"] = static_cast<double>(size * sizeof(float)) / 1024.0;
    state.counters["accesses"] = static_cast<double>(indices.size());
    state.SetLabel("random_read");
}

static void BM_ARM_Random_Write(benchmark::State& state) {
    const size_t size = static_cast<size_t>(state.range(0));
    std::vector<float> data(size);
    auto indices = generateRandomIndices(size / 4, size, 67890);
    
    for (auto _ : state) {
        for (size_t idx : indices) {
            data[idx] = static_cast<float>(idx);
        }
        benchmark::ClobberMemory();
    }
    
    state.counters["size_kb"] = static_cast<double>(size * sizeof(float)) / 1024.0;
    state.counters["accesses"] = static_cast<double>(indices.size());
    state.SetLabel("random_write");
}

// ============================================================================
// Strided Access (Moderate Cache Efficiency)
// ============================================================================

static void BM_ARM_Strided_Read(benchmark::State& state) {
    const size_t size = static_cast<size_t>(state.range(0));
    const size_t stride = static_cast<size_t>(state.range(1));
    std::vector<float> data(size, 1.0f);
    
    for (auto _ : state) {
        float sum = 0.0f;
        for (size_t i = 0; i < size; i += stride) {
            sum += data[i];
        }
        benchmark::DoNotOptimize(sum);
    }
    
    state.counters["size_kb"] = static_cast<double>(size * sizeof(float)) / 1024.0;
    state.counters["stride"] = static_cast<double>(stride);
    state.SetLabel("strided_read");
}

// ============================================================================
// Memory Copy (Tests ARM cache line size optimization)
// ============================================================================

static void BM_ARM_MemCopy_Builtin(benchmark::State& state) {
    const size_t size = static_cast<size_t>(state.range(0));
    std::vector<float> src(size, 1.0f);
    std::vector<float> dst(size);
    
    for (auto _ : state) {
        std::memcpy(dst.data(), src.data(), size * sizeof(float));
        benchmark::ClobberMemory();
    }
    
    state.counters["size_kb"] = static_cast<double>(size * sizeof(float)) / 1024.0;
    state.counters["bandwidth_mb_s"] = benchmark::Counter(
        static_cast<double>(state.iterations() * size * sizeof(float)),
        benchmark::Counter::kIsRate,
        benchmark::Counter::kIs1024
    );
    state.SetLabel("memcpy");
}

static void BM_ARM_MemCopy_Loop(benchmark::State& state) {
    const size_t size = static_cast<size_t>(state.range(0));
    std::vector<float> src(size, 1.0f);
    std::vector<float> dst(size);
    
    for (auto _ : state) {
        for (size_t i = 0; i < size; ++i) {
            dst[i] = src[i];
        }
        benchmark::ClobberMemory();
    }
    
    state.counters["size_kb"] = static_cast<double>(size * sizeof(float)) / 1024.0;
    state.counters["bandwidth_mb_s"] = benchmark::Counter(
        static_cast<double>(state.iterations() * size * sizeof(float)),
        benchmark::Counter::kIsRate,
        benchmark::Counter::kIs1024
    );
    state.SetLabel("loop_copy");
}

// ============================================================================
// Cache Line Effects (Typical ARM: 64 bytes)
// ============================================================================

static void BM_ARM_CacheLine_Aligned(benchmark::State& state) {
    const size_t size = 16384; // 64 KB
    const size_t stride = 16; // 64 bytes / 4 bytes per float
    alignas(64) std::vector<float> data(size, 1.0f);
    
    for (auto _ : state) {
        float sum = 0.0f;
        for (size_t i = 0; i < size; i += stride) {
            sum += data[i];
        }
        benchmark::DoNotOptimize(sum);
    }
    
    state.SetLabel("cacheline_aligned_64B");
}

static void BM_ARM_CacheLine_Unaligned(benchmark::State& state) {
    const size_t size = 16384; // 64 KB
    const size_t stride = 16; // 64 bytes / 4 bytes per float
    std::vector<float> data(size + 1, 1.0f); // Intentionally unaligned
    
    for (auto _ : state) {
        float sum = 0.0f;
        for (size_t i = 1; i < size; i += stride) { // Start at offset 1
            sum += data[i];
        }
        benchmark::DoNotOptimize(sum);
    }
    
    state.SetLabel("cacheline_unaligned");
}

// ============================================================================
// Register Benchmarks
// ============================================================================

// Test different memory sizes (L1, L2, L3, RAM)
// Raspberry Pi 4: L1 32KB, L2 1MB (per core)
// Adjust ranges based on target ARM platform

// Sequential access - various sizes
BENCHMARK(BM_ARM_Sequential_Read)
    ->Arg(4 * 1024)      // 16 KB (fits in L1)
    ->Arg(32 * 1024)     // 128 KB (fits in L2)
    ->Arg(256 * 1024)    // 1 MB (L2 boundary)
    ->Arg(1024 * 1024)   // 4 MB (L3/RAM)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK(BM_ARM_Sequential_Write)
    ->Arg(4 * 1024)
    ->Arg(32 * 1024)
    ->Arg(256 * 1024)
    ->Arg(1024 * 1024)
    ->Unit(benchmark::kMicrosecond);

// Random access - stress test for cache
BENCHMARK(BM_ARM_Random_Read)
    ->Arg(4 * 1024)
    ->Arg(32 * 1024)
    ->Arg(256 * 1024)
    ->Arg(1024 * 1024)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK(BM_ARM_Random_Write)
    ->Arg(4 * 1024)
    ->Arg(32 * 1024)
    ->Arg(256 * 1024)
    ->Unit(benchmark::kMicrosecond);

// Strided access with different strides
BENCHMARK(BM_ARM_Strided_Read)
    ->Args({32 * 1024, 1})   // Sequential
    ->Args({32 * 1024, 2})   // Every other
    ->Args({32 * 1024, 4})   // Every 4th
    ->Args({32 * 1024, 8})   // Every 8th
    ->Args({32 * 1024, 16})  // Every cache line
    ->Unit(benchmark::kMicrosecond);

// Memory copy benchmarks
BENCHMARK(BM_ARM_MemCopy_Builtin)
    ->Arg(4 * 1024)
    ->Arg(32 * 1024)
    ->Arg(256 * 1024)
    ->Arg(1024 * 1024)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK(BM_ARM_MemCopy_Loop)
    ->Arg(4 * 1024)
    ->Arg(32 * 1024)
    ->Arg(256 * 1024)
    ->Unit(benchmark::kMicrosecond);

// Cache line alignment effects
BENCHMARK(BM_ARM_CacheLine_Aligned)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_ARM_CacheLine_Unaligned)->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
