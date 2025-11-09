#include "utils/hkdf_cache.h"
#include "utils/hkdf_helper.h"
#include <functional>
#include <random>

namespace themis {
namespace utils {

static uint64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

HKDFCache& HKDFCache::instance() {
    static HKDFCache inst;
    return inst;
}

void HKDFCache::clear() {
    std::lock_guard lg(mtx_);
    map_.clear();
}

void HKDFCache::setTtlMs(uint64_t ttl_ms) {
    std::lock_guard lg(mtx_);
    ttl_ms_ = ttl_ms;
}

void HKDFCache::setCapacity(size_t capacity) {
    std::lock_guard lg(mtx_);
    capacity_ = capacity;
    evictIfNeeded();
}

std::string HKDFCache::makeKey(const std::vector<uint8_t>& ikm,
                               const std::vector<uint8_t>& salt,
                               const std::string& info,
                               size_t out_len) const {
    std::hash<std::string> h;
    std::string composite;
    composite.reserve(ikm.size() + salt.size() + info.size() + 16);
    composite.append(reinterpret_cast<const char*>(ikm.data()), ikm.size());
    composite.push_back('|');
    composite.append(reinterpret_cast<const char*>(salt.data()), salt.size());
    composite.push_back('|');
    composite.append(info);
    composite.push_back('|');
    composite.append(std::to_string(out_len));
    auto hv = h(composite);
    return std::to_string(hv);
}

void HKDFCache::evictIfNeeded() {
    if (map_.size() <= capacity_) return;
    // Simple random eviction until size fits
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    while (map_.size() > capacity_) {
        auto it = map_.begin();
        std::advance(it, rng() % map_.size());
        map_.erase(it);
    }
}

std::vector<uint8_t> HKDFCache::deriveCached(
    const std::vector<uint8_t>& ikm,
    const std::vector<uint8_t>& salt,
    const std::string& info,
    size_t out_len) {
    const uint64_t now = now_ms();
    const std::string key = makeKey(ikm, salt, info, out_len);
    {
        std::lock_guard lg(mtx_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            if (now - it->second.ts_ms <= ttl_ms_) {
                return it->second.key; // cache hit
            } else {
                map_.erase(it); // expired
            }
        }
    }
    // Miss → derive outside lock
    auto derived = HKDFHelper::derive(ikm, salt, info, out_len);
    {
        std::lock_guard lg(mtx_);
        map_[key] = Entry{derived, now};
        evictIfNeeded();
    }
    return derived;
}

} // namespace utils
} // namespace themis
