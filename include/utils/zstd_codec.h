#pragma once

#include <vector>
#include <cstdint>
#include <string>

// Thin wrapper around Zstandard (ZSTD) compression library
// Functions are available only if compiled with THEMIS_HAS_ZSTD. If not,
// the functions will return empty vectors to signal unsupported operation.

namespace themis {
namespace utils {

// Compress a buffer with ZSTD. Returns compressed bytes on success; empty on failure/unsupported.
std::vector<uint8_t> zstd_compress(const uint8_t* data, size_t size, int level = 3);

// Compress a string
inline std::vector<uint8_t> zstd_compress(const std::string& s, int level = 3) {
    return zstd_compress(reinterpret_cast<const uint8_t*>(s.data()), s.size(), level);
}

// Decompress a buffer that contains ZSTD frame. Empty on failure/unsupported.
std::vector<uint8_t> zstd_decompress(const std::vector<uint8_t>& compressed);

} // namespace utils
} // namespace themis
