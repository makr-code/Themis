// Thread-local HKDF LRU cache
#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <functional>

namespace themis {
namespace utils {

class HKDFCache {
public:
    // Create or return thread-local instance
    static HKDFCache& threadLocal();

    // Derive with cache: ikm, salt, info, output_length
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
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <chrono>

namespace themis {
namespace utils {

/**
 * HKDFCache: kleiner thread-sicherer Cache für abgeleitete Schlüssel
 * Key = hash(ikm || salt || info || out_len). Value = derived key bytes + ts
 * Default-TTL: 5 Minuten
 */
class HKDFCache {
public:
    static HKDFCache& instance();

    // Leert den Cache
    void clear();

    // Setzt TTL in Millisekunden
    void setTtlMs(uint64_t ttl_ms);

    // Setzt Kapazität (Anzahl Einträge); bei Überschreitung wird zufällig gelöscht
    void setCapacity(size_t capacity);

    // Hole aus Cache oder leite ab (HKDF-SHA256)
    std::vector<uint8_t> deriveCached(
        const std::vector<uint8_t>& ikm,
        const std::vector<uint8_t>& salt,
        const std::string& info,
        size_t out_len);

private:
    HKDFCache() = default;
    HKDFCache(const HKDFCache&) = delete;
    HKDFCache& operator=(const HKDFCache&) = delete;

    struct Entry {
        std::vector<uint8_t> key;
        uint64_t ts_ms;
    };

    std::string makeKey(const std::vector<uint8_t>& ikm,
                        const std::vector<uint8_t>& salt,
                        const std::string& info,
                        size_t out_len) const;

    void evictIfNeeded();

    std::unordered_map<std::string, Entry> map_;
    std::mutex mtx_;
    uint64_t ttl_ms_ = 5 * 60 * 1000; // 5 Minuten
    size_t capacity_ = 2048;
};

} // namespace utils
} // namespace themis
