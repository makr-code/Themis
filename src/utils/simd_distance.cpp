#include "utils/simd_distance.h"
#include <cstddef>
#include <cmath>

#if defined(__AVX512F__)
  #include <immintrin.h>
#elif defined(__AVX2__)
  #include <immintrin.h>
#endif

namespace themis {
namespace simd {

static inline float scalar_l2_sq(const float* a, const float* b, std::size_t dim) {
    float acc = 0.0f;
    for (std::size_t i = 0; i < dim; ++i) {
        float d = a[i] - b[i];
        acc += d * d;
    }
    return acc;
}

#if defined(__AVX512F__)
static inline float avx512_l2_sq(const float* a, const float* b, std::size_t dim) {
    std::size_t i = 0;
    __m512 acc = _mm512_setzero_ps();
    const std::size_t step = 16;
    for (; i + step <= dim; i += step) {
        __m512 va = _mm512_loadu_ps(a + i);
        __m512 vb = _mm512_loadu_ps(b + i);
        __m512 diff = _mm512_sub_ps(va, vb);
        acc = _mm512_fmadd_ps(diff, diff, acc);
    }
    alignas(64) float tmp[16];
    _mm512_store_ps(tmp, acc);
    float res = 0.0f;
    for (int k = 0; k < 16; ++k) res += tmp[k];
    if (i < dim) res += scalar_l2_sq(a + i, b + i, dim - i);
    return res;
}
#elif defined(__AVX2__)
static inline float avx2_l2_sq(const float* a, const float* b, std::size_t dim) {
    std::size_t i = 0;
    __m256 acc0 = _mm256_setzero_ps();
    __m256 acc1 = _mm256_setzero_ps();
    const std::size_t step = 16;
    for (; i + step <= dim; i += step) {
        __m256 va0 = _mm256_loadu_ps(a + i);
        __m256 vb0 = _mm256_loadu_ps(b + i);
        __m256 d0 = _mm256_sub_ps(va0, vb0);
        acc0 = _mm256_fmadd_ps(d0, d0, acc0);

        __m256 va1 = _mm256_loadu_ps(a + i + 8);
        __m256 vb1 = _mm256_loadu_ps(b + i + 8);
        __m256 d1 = _mm256_sub_ps(va1, vb1);
        acc1 = _mm256_fmadd_ps(d1, d1, acc1);
    }
    __m256 acc = _mm256_add_ps(acc0, acc1);
    alignas(32) float tmp[8];
    _mm256_store_ps(tmp, acc);
    float res = 0.0f;
    for (int k = 0; k < 8; ++k) res += tmp[k];
    if (i < dim) res += scalar_l2_sq(a + i, b + i, dim - i);
    return res;
}
#endif

float l2_distance_sq(const float* a, const float* b, std::size_t dim) {
#if defined(__AVX512F__)
    return avx512_l2_sq(a, b, dim);
#elif defined(__AVX2__)
    return avx2_l2_sq(a, b, dim);
#else
    return scalar_l2_sq(a, b, dim);
#endif
}

float l2_distance(const float* a, const float* b, std::size_t dim) {
    float d2 = l2_distance_sq(a, b, dim);
    return std::sqrt(d2);
}

} // namespace simd
} // namespace themis
