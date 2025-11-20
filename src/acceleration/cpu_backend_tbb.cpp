// Intel TBB-Based CPU Backend Implementation
// Provides high-performance multi-threaded acceleration using Intel TBB
// Superior to OpenMP for dynamic workloads with work-stealing scheduler
// Copyright (c) 2024 ThemisDB

#include "acceleration/cpu_backend.h"
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <tbb/blocked_range2d.h>
#include <tbb/parallel_reduce.h>
#include <tbb/task_arena.h>
#include <tbb/global_control.h>
#include <cmath>
#include <algorithm>
#include <iostream>

// SIMD support (same as OpenMP version)
#if defined(__AVX2__) || defined(__AVX512F__)
#define THEMIS_HAS_SIMD_X86 1
#include <immintrin.h>
#elif defined(__ARM_NEON) || defined(__aarch64__)
#define THEMIS_HAS_SIMD_ARM 1
#include <arm_neon.h>
#endif

namespace themis {
namespace acceleration {

// ============================================================================
// TBB-Based CPUVectorBackend Implementation
// ============================================================================

class CPUVectorBackendTBB : public CPUVectorBackend {
private:
    std::unique_ptr<tbb::task_arena> arena_;
    std::unique_ptr<tbb::global_control> threadControl_;
    bool enableSIMD_;
    int numThreads_;
    
public:
    CPUVectorBackendTBB() : enableSIMD_(true) {
        numThreads_ = tbb::this_task_arena::max_concurrency();
        
        // Create task arena for controlled parallelism
        arena_ = std::make_unique<tbb::task_arena>(numThreads_);
        
        std::cout << "Intel TBB CPU backend initialized\n";
        std::cout << "  Threads: " << numThreads_ << "\n";
        std::cout << "  TBB Version: " << TBB_VERSION_MAJOR << "." << TBB_VERSION_MINOR << "\n";
#if THEMIS_HAS_SIMD_X86
        std::cout << "  SIMD: AVX2/AVX-512\n";
#elif THEMIS_HAS_SIMD_ARM
        std::cout << "  SIMD: NEON\n";
#else
        std::cout << "  SIMD: Scalar\n";
#endif
    }
    
    void setThreadCount(int threads) {
        numThreads_ = threads;
        threadControl_ = std::make_unique<tbb::global_control>(
            tbb::global_control::max_allowed_parallelism, 
            threads
        );
        arena_ = std::make_unique<tbb::task_arena>(threads);
    }
    
    void enableSIMD(bool enable) {
        enableSIMD_ = enable;
    }
    
    std::string name() const override {
        return "CPU Multi-Threaded (Intel TBB + SIMD)";
    }
    
    // SIMD-optimized L2 distance (same as OpenMP version)
    float computeL2Distance(const float* a, const float* b, size_t dim) const override {
#if THEMIS_HAS_SIMD_X86 && defined(__AVX2__)
        if (enableSIMD_ && dim >= 8) {
            __m256 sum_vec = _mm256_setzero_ps();
            size_t i = 0;
            
            for (; i + 7 < dim; i += 8) {
                __m256 a_vec = _mm256_loadu_ps(a + i);
                __m256 b_vec = _mm256_loadu_ps(b + i);
                __m256 diff = _mm256_sub_ps(a_vec, b_vec);
                sum_vec = _mm256_fmadd_ps(diff, diff, sum_vec);
            }
            
            // Horizontal sum
            __m128 sum_high = _mm256_extractf128_ps(sum_vec, 1);
            __m128 sum_low = _mm256_castps256_ps128(sum_vec);
            __m128 sum = _mm_add_ps(sum_low, sum_high);
            sum = _mm_hadd_ps(sum, sum);
            sum = _mm_hadd_ps(sum, sum);
            
            float result = _mm_cvtss_f32(sum);
            
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
            
            for (; i + 3 < dim; i += 4) {
                float32x4_t a_vec = vld1q_f32(a + i);
                float32x4_t b_vec = vld1q_f32(b + i);
                float32x4_t diff = vsubq_f32(a_vec, b_vec);
                sum_vec = vmlaq_f32(sum_vec, diff, diff);
            }
            
            float result = vaddvq_f32(sum_vec);
            
            for (; i < dim; ++i) {
                float diff = a[i] - b[i];
                result += diff * diff;
            }
            
            return std::sqrt(result);
        }
#endif
        return CPUVectorBackend::computeL2Distance(a, b, dim);
    }
    
    // SIMD-optimized cosine distance (same as OpenMP version)
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
    
    // TBB-parallelized batch distance computation
    std::vector<float> computeDistances(
        const float* queries,
        size_t numQueries,
        size_t dim,
        const float* vectors,
        size_t numVectors,
        bool useL2
    ) override {
        std::vector<float> distances(numQueries * numVectors);
        
        // Use TBB parallel_for with work-stealing
        arena_->execute([&] {
            tbb::parallel_for(
                tbb::blocked_range<size_t>(0, numQueries, 16), // grain_size=16
                [&](const tbb::blocked_range<size_t>& range) {
                    for (size_t q = range.begin(); q != range.end(); ++q) {
                        const float* query = queries + q * dim;
                        for (size_t v = 0; v < numVectors; ++v) {
                            const float* vector = vectors + v * dim;
                            float dist = useL2 
                                ? computeL2Distance(query, vector, dim)
                                : computeCosineDistance(query, vector, dim);
                            distances[q * numVectors + v] = dist;
                        }
                    }
                }
            );
        });
        
        return distances;
    }
    
    // TBB-parallelized KNN search
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
        
        // TBB parallel_for with dynamic load balancing
        arena_->execute([&] {
            tbb::parallel_for(
                tbb::blocked_range<size_t>(0, numQueries),
                [&](const tbb::blocked_range<size_t>& range) {
                    for (size_t q = range.begin(); q != range.end(); ++q) {
                        const float* query = queries + q * dim;
                        
                        std::vector<std::pair<uint32_t, float>> distances;
                        distances.reserve(numVectors);
                        
                        // Compute distances for this query
                        for (size_t v = 0; v < numVectors; ++v) {
                            const float* vector = vectors + v * dim;
                            float dist = useL2 
                                ? computeL2Distance(query, vector, dim)
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
                }
            );
        });
        
        return results;
    }
};

// ============================================================================
// TBB-Based CPUGeoBackend Implementation
// ============================================================================

class CPUGeoBackendTBB : public CPUGeoBackend {
private:
    std::unique_ptr<tbb::task_arena> arena_;
    
public:
    CPUGeoBackendTBB() {
        int numThreads = tbb::this_task_arena::max_concurrency();
        arena_ = std::make_unique<tbb::task_arena>(numThreads);
    }
    
    std::string name() const override {
        return "CPU Geo Multi-Threaded (Intel TBB)";
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
        
        arena_->execute([&] {
            tbb::parallel_for(
                tbb::blocked_range<size_t>(0, count, 256),
                [&](const tbb::blocked_range<size_t>& range) {
                    for (size_t i = range.begin(); i != range.end(); ++i) {
                        double dist = useHaversine 
                            ? haversineDistance(latitudes1[i], longitudes1[i], 
                                              latitudes2[i], longitudes2[i])
                            : vincentyDistance(latitudes1[i], longitudes1[i], 
                                             latitudes2[i], longitudes2[i]);
                        distances[i] = static_cast<float>(dist);
                    }
                }
            );
        });
        
        return distances;
    }
    
    std::vector<bool> batchPointInPolygon(
        const double* pointLats,
        const double* pointLons,
        size_t numPoints,
        const double* polygonCoords,
        size_t numPolygonVertices
    ) override {
        std::vector<bool> results(numPoints);
        
        arena_->execute([&] {
            tbb::parallel_for(
                tbb::blocked_range<size_t>(0, numPoints, 64),
                [&](const tbb::blocked_range<size_t>& range) {
                    for (size_t p = range.begin(); p != range.end(); ++p) {
                        double testLat = pointLats[p];
                        double testLon = pointLons[p];
                        
                        bool inside = false;
                        size_t j = numPolygonVertices - 1;
                        
                        for (size_t i = 0; i < numPolygonVertices; ++i) {
                            double lat_i = polygonCoords[i * 2];
                            double lon_i = polygonCoords[i * 2 + 1];
                            double lat_j = polygonCoords[j * 2];
                            double lon_j = polygonCoords[j * 2 + 1];
                            
                            if (((lon_i > testLon) != (lon_j > testLon)) &&
                                (testLat < (lat_j - lat_i) * (testLon - lon_i) / 
                                          (lon_j - lon_i) + lat_i)) {
                                inside = !inside;
                            }
                            
                            j = i;
                        }
                        
                        results[p] = inside;
                    }
                }
            );
        });
        
        return results;
    }
};

// Factory functions
std::unique_ptr<CPUVectorBackend> createTBBCPUVectorBackend() {
    return std::make_unique<CPUVectorBackendTBB>();
}

std::unique_ptr<CPUGeoBackend> createTBBCPUGeoBackend() {
    return std::make_unique<CPUGeoBackendTBB>();
}

} // namespace acceleration
} // namespace themis
