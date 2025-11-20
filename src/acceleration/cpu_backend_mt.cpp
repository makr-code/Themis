// Multi-Threaded CPU Backend with OpenMP and SIMD optimizations
// Provides high-performance CPU-based acceleration for vector, graph, and geo operations
// Copyright (c) 2024 ThemisDB

#include "acceleration/cpu_backend.h"
#include <cmath>
#include <algorithm>
#include <queue>
#include <limits>
#include <thread>
#include <iostream>

// OpenMP support (if available)
#ifdef _OPENMP
#include <omp.h>
#define THEMIS_HAS_OPENMP 1
#else
#define THEMIS_HAS_OPENMP 0
#endif

// SIMD support detection
#if defined(__AVX2__) || defined(__AVX512F__)
#define THEMIS_HAS_SIMD_X86 1
#include <immintrin.h>
#elif defined(__ARM_NEON) || defined(__aarch64__)
#define THEMIS_HAS_SIMD_ARM 1
#include <arm_neon.h>
#else
#define THEMIS_HAS_SIMD 0
#endif

namespace themis {
namespace acceleration {

// ============================================================================
// Multi-Threaded CPUVectorBackend Implementation
// ============================================================================

class CPUVectorBackendMT : public CPUVectorBackend {
private:
    int numThreads_;
    bool enableSIMD_;
    
public:
    CPUVectorBackendMT() {
#if THEMIS_HAS_OPENMP
        numThreads_ = omp_get_max_threads();
#else
        numThreads_ = std::thread::hardware_concurrency();
        if (numThreads_ == 0) numThreads_ = 1;
#endif
        enableSIMD_ = true;
        
        std::cout << "Multi-threaded CPU backend initialized\n";
        std::cout << "  Threads: " << numThreads_ << "\n";
        std::cout << "  OpenMP: " << (THEMIS_HAS_OPENMP ? "Yes" : "No") << "\n";
#if THEMIS_HAS_SIMD_X86
        std::cout << "  SIMD: AVX2/AVX-512\n";
#elif THEMIS_HAS_SIMD_ARM
        std::cout << "  SIMD: NEON\n";
#else
        std::cout << "  SIMD: No\n";
#endif
    }
    
    void setThreadCount(int threads) {
        numThreads_ = threads;
#if THEMIS_HAS_OPENMP
        omp_set_num_threads(threads);
#endif
    }
    
    void enableSIMD(bool enable) {
        enableSIMD_ = enable;
    }
    
    std::string name() const override {
        return "CPU Multi-Threaded (OpenMP + SIMD)";
    }
    
    // Optimized L2 distance computation with SIMD
    float computeL2Distance(const float* a, const float* b, size_t dim) const override {
#if THEMIS_HAS_SIMD_X86 && defined(__AVX2__)
        if (enableSIMD_ && dim >= 8) {
            __m256 sum_vec = _mm256_setzero_ps();
            size_t i = 0;
            
            // Process 8 floats at a time with AVX2
            for (; i + 7 < dim; i += 8) {
                __m256 a_vec = _mm256_loadu_ps(a + i);
                __m256 b_vec = _mm256_loadu_ps(b + i);
                __m256 diff = _mm256_sub_ps(a_vec, b_vec);
                sum_vec = _mm256_fmadd_ps(diff, diff, sum_vec); // FMA: diff*diff + sum
            }
            
            // Horizontal sum
            __m128 sum_high = _mm256_extractf128_ps(sum_vec, 1);
            __m128 sum_low = _mm256_castps256_ps128(sum_vec);
            __m128 sum = _mm_add_ps(sum_low, sum_high);
            sum = _mm_hadd_ps(sum, sum);
            sum = _mm_hadd_ps(sum, sum);
            
            float result = _mm_cvtss_f32(sum);
            
            // Handle remaining elements
            for (; i < dim; ++i) {
                float diff = a[i] - b[i];
                result += diff * diff;
            }
            
            return std::sqrt(result);
        }
#elif THEMIS_HAS_SIMD_ARM
        if (enableSIMD_ && dim >= 4) {
            float32x4_t sum_vec = vdupq_n_f32(0.0f);
            size_t i = 0;
            
            // Process 4 floats at a time with NEON
            for (; i + 3 < dim; i += 4) {
                float32x4_t a_vec = vld1q_f32(a + i);
                float32x4_t b_vec = vld1q_f32(b + i);
                float32x4_t diff = vsubq_f32(a_vec, b_vec);
                sum_vec = vmlaq_f32(sum_vec, diff, diff); // diff*diff + sum
            }
            
            // Horizontal sum
            float result = vaddvq_f32(sum_vec); // ARM64 only
            
            // Handle remaining elements
            for (; i < dim; ++i) {
                float diff = a[i] - b[i];
                result += diff * diff;
            }
            
            return std::sqrt(result);
        }
#endif
        // Fallback to scalar implementation
        return CPUVectorBackend::computeL2Distance(a, b, dim);
    }
    
    // Optimized cosine distance with SIMD
    float computeCosineDistance(const float* a, const float* b, size_t dim) const override {
#if THEMIS_HAS_SIMD_X86 && defined(__AVX2__)
        if (enableSIMD_ && dim >= 8) {
            __m256 dot_vec = _mm256_setzero_ps();
            __m256 normA_vec = _mm256_setzero_ps();
            __m256 normB_vec = _mm256_setzero_ps();
            size_t i = 0;
            
            for (; i + 7 < dim; i += 8) {
                __m256 a_vec = _mm256_loadu_ps(a + i);
                __m256 b_vec = _mm256_loadu_ps(b + i);
                dot_vec = _mm256_fmadd_ps(a_vec, b_vec, dot_vec);
                normA_vec = _mm256_fmadd_ps(a_vec, a_vec, normA_vec);
                normB_vec = _mm256_fmadd_ps(b_vec, b_vec, normB_vec);
            }
            
            // Horizontal sum
            auto hsum = [](__ m256 v) {
                __m128 sum_high = _mm256_extractf128_ps(v, 1);
                __m128 sum_low = _mm256_castps256_ps128(v);
                __m128 sum = _mm_add_ps(sum_low, sum_high);
                sum = _mm_hadd_ps(sum, sum);
                sum = _mm_hadd_ps(sum, sum);
                return _mm_cvtss_f32(sum);
            };
            
            float dotProduct = hsum(dot_vec);
            float normA = hsum(normA_vec);
            float normB = hsum(normB_vec);
            
            // Handle remaining elements
            for (; i < dim; ++i) {
                dotProduct += a[i] * b[i];
                normA += a[i] * a[i];
                normB += b[i] * b[i];
            }
            
            normA = std::sqrt(normA);
            normB = std::sqrt(normB);
            
            if (normA < 1e-10f || normB < 1e-10f) return 1.0f;
            
            float cosine = dotProduct / (normA * normB);
            return 1.0f - cosine;
        }
#elif THEMIS_HAS_SIMD_ARM
        if (enableSIMD_ && dim >= 4) {
            float32x4_t dot_vec = vdupq_n_f32(0.0f);
            float32x4_t normA_vec = vdupq_n_f32(0.0f);
            float32x4_t normB_vec = vdupq_n_f32(0.0f);
            size_t i = 0;
            
            for (; i + 3 < dim; i += 4) {
                float32x4_t a_vec = vld1q_f32(a + i);
                float32x4_t b_vec = vld1q_f32(b + i);
                dot_vec = vmlaq_f32(dot_vec, a_vec, b_vec);
                normA_vec = vmlaq_f32(normA_vec, a_vec, a_vec);
                normB_vec = vmlaq_f32(normB_vec, b_vec, b_vec);
            }
            
            float dotProduct = vaddvq_f32(dot_vec);
            float normA = vaddvq_f32(normA_vec);
            float normB = vaddvq_f32(normB_vec);
            
            for (; i < dim; ++i) {
                dotProduct += a[i] * b[i];
                normA += a[i] * a[i];
                normB += b[i] * b[i];
            }
            
            normA = std::sqrt(normA);
            normB = std::sqrt(normB);
            
            if (normA < 1e-10f || normB < 1e-10f) return 1.0f;
            
            float cosine = dotProduct / (normA * normB);
            return 1.0f - cosine;
        }
#endif
        return CPUVectorBackend::computeCosineDistance(a, b, dim);
    }
    
    // Multi-threaded batch distance computation
    std::vector<float> computeDistances(
        const float* queries,
        size_t numQueries,
        size_t dim,
        const float* vectors,
        size_t numVectors,
        bool useL2
    ) override {
        std::vector<float> distances(numQueries * numVectors);
        
#if THEMIS_HAS_OPENMP
        // Parallel processing with OpenMP
        #pragma omp parallel for schedule(dynamic, 16)
        for (size_t q = 0; q < numQueries; ++q) {
            const float* query = queries + q * dim;
            for (size_t v = 0; v < numVectors; ++v) {
                const float* vector = vectors + v * dim;
                float dist = useL2 ? computeL2Distance(query, vector, dim)
                                  : computeCosineDistance(query, vector, dim);
                distances[q * numVectors + v] = dist;
            }
        }
#else
        // Fallback to single-threaded
        return CPUVectorBackend::computeDistances(queries, numQueries, dim, vectors, numVectors, useL2);
#endif
        
        return distances;
    }
    
    // Multi-threaded KNN search
    std::vector<std::vector<std::pair<uint32_t, float>>> batchKnnSearch(
        const float* queries,
        size_t numQueries,
        size_t dim,
        const float* vectors,
        size_t numVectors,
        size_t k,
        bool useL2
    ) override {
        std::vector<std::vector<std::pair<uint32_t, float>>> results(numQueries);
        
#if THEMIS_HAS_OPENMP
        // Parallel KNN search
        #pragma omp parallel for schedule(dynamic)
        for (size_t q = 0; q < numQueries; ++q) {
            const float* query = queries + q * dim;
            
            std::vector<std::pair<uint32_t, float>> distances;
            distances.reserve(numVectors);
            
            // Compute distances for this query
            for (size_t v = 0; v < numVectors; ++v) {
                const float* vector = vectors + v * dim;
                float dist = useL2 ? computeL2Distance(query, vector, dim)
                                  : computeCosineDistance(query, vector, dim);
                distances.emplace_back(static_cast<uint32_t>(v), dist);
            }
            
            // Partial sort to get k nearest
            size_t actualK = std::min(k, distances.size());
            std::partial_sort(
                distances.begin(),
                distances.begin() + actualK,
                distances.end(),
                [](const auto& a, const auto& b) { return a.second < b.second; }
            );
            
            results[q].assign(distances.begin(), distances.begin() + actualK);
        }
#else
        return CPUVectorBackend::batchKnnSearch(queries, numQueries, dim, vectors, numVectors, k, useL2);
#endif
        
        return results;
    }
};

// ============================================================================
// Multi-Threaded CPUGeoBackend Implementation
// ============================================================================

class CPUGeoBackendMT : public CPUGeoBackend {
private:
    int numThreads_;
    
public:
    CPUGeoBackendMT() {
#if THEMIS_HAS_OPENMP
        numThreads_ = omp_get_max_threads();
#else
        numThreads_ = std::thread::hardware_concurrency();
#endif
    }
    
    std::string name() const override {
        return "CPU Geo Multi-Threaded (OpenMP)";
    }
    
    std::vector<float> batchDistances(
        const double* latitudes1,
        const double* longitudes1,
        const double* latitudes2,
        const double* longitudes2,
        size_t count,
        bool useHaversine
    ) override {
        std::vector<float> distances(count);
        
#if THEMIS_HAS_OPENMP
        #pragma omp parallel for schedule(static)
        for (size_t i = 0; i < count; ++i) {
            double dist = useHaversine 
                ? haversineDistance(latitudes1[i], longitudes1[i], latitudes2[i], longitudes2[i])
                : vincentyDistance(latitudes1[i], longitudes1[i], latitudes2[i], longitudes2[i]);
            distances[i] = static_cast<float>(dist);
        }
#else
        return CPUGeoBackend::batchDistances(latitudes1, longitudes1, latitudes2, longitudes2, count, useHaversine);
#endif
        
        return distances;
    }
};

// Factory functions
std::unique_ptr<CPUVectorBackend> createMultiThreadedCPUVectorBackend() {
    return std::make_unique<CPUVectorBackendMT>();
}

std::unique_ptr<CPUGeoBackend> createMultiThreadedCPUGeoBackend() {
    return std::make_unique<CPUGeoBackendMT>();
}

} // namespace acceleration
} // namespace themis
