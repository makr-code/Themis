#pragma once

#include <cstddef>

namespace themis {
namespace simd {

// Compute L2 distance (Euclidean) between two float vectors of length dim.
// Returns sqrt(sum((a[i]-b[i])^2)). Uses SIMD when available:
// - x86_64: AVX512 or AVX2
// - ARM64/AArch64: NEON
// Falls back to portable scalar implementation if no SIMD available.
float l2_distance(const float* a, const float* b, std::size_t dim);

// Compute squared L2 distance (without sqrt). Useful for ranking when only
// relative ordering is required. Also SIMD-optimized when available.
float l2_distance_sq(const float* a, const float* b, std::size_t dim);

} // namespace simd
} // namespace themis
