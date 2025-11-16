// Thread-local HKDF LRU cache (single-definition header)
#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace themis {
namespace utils {

class HKDFCache {
public:
    // Return a thread-local instance (preferred by callers in code/tests)
    static HKDFCache& threadLocal();

    // Derive with cache: ikm, salt, info, output_length
    // NOTE: function name matches existing callers: derive_cached
    std::vector<uint8_t> derive_cached(const std::vector<uint8_t>& ikm,
                                       const std::vector<uint8_t>& salt,
                                       const std::string& info,
                                       size_t output_length);

    // Clear cache (useful for tests or explicit invalidation)
    void clear();

    // Configure capacity (default ~1024)
    void setCapacity(size_t cap);

private:
    HKDFCache();
    ~HKDFCache();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace utils
} // namespace themis
