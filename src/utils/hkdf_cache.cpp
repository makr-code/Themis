#include "utils/hkdf_cache.h"
#include "utils/hkdf_helper.h"

#include <unordered_map>
#include <list>
#include <mutex>
#include <thread>
#include <functional>

namespace themis {
namespace utils {

struct HKDFCache::Impl {
    using Key = std::string; // simple string key constructed from ikm|salt|info|len
    size_t capacity = 1024;
    // LRU: list of keys, front = most recent
    std::list<Key> lru;
    std::unordered_map<Key, std::vector<uint8_t>> map;
    // Protect structural operations in case someone shares instance (defensive)
    std::mutex mutex;

    static Key make_key(const std::vector<uint8_t>& ikm,
                        const std::vector<uint8_t>& salt,
                        const std::string& info,
                        size_t outlen) {
        Key k;
        k.reserve(ikm.size() + salt.size() + info.size() + 16);
        k.append(reinterpret_cast<const char*>(ikm.data()), ikm.size());
        k.push_back('\x00');
        k.append(reinterpret_cast<const char*>(salt.data()), salt.size());
        k.push_back('\x00');
        k.append(info);
        k.push_back('\x00');
        k.append(std::to_string(outlen));
        return k;
    }
};

HKDFCache::HKDFCache() : impl_(new Impl()) {}
HKDFCache::~HKDFCache() = default;

HKDFCache& HKDFCache::threadLocal() {
    thread_local HKDFCache instance;
    return instance;
}

void HKDFCache::setCapacity(size_t cap) {
    impl_->capacity = cap ? cap : 1;
}

void HKDFCache::clear() {
    impl_->lru.clear();
    impl_->map.clear();
}

std::vector<uint8_t> HKDFCache::derive_cached(const std::vector<uint8_t>& ikm,
                                               const std::vector<uint8_t>& salt,
                                               const std::string& info,
                                               size_t output_length) {
    auto k = Impl::make_key(ikm, salt, info, output_length);
    std::lock_guard<std::mutex> guard(impl_->mutex);
    auto it = impl_->map.find(k);
    if (it != impl_->map.end()) {
        // move key to front
        impl_->lru.remove(k);
        impl_->lru.push_front(k);
        return it->second;
    }
    // Miss -> derive
    auto out = HKDFHelper::derive(ikm, salt, info, output_length);
    // Insert
    impl_->lru.push_front(k);
    impl_->map.emplace(k, out);
    // Evict if needed
    if (impl_->map.size() > impl_->capacity) {
        auto last = impl_->lru.back();
        impl_->map.erase(last);
        impl_->lru.pop_back();
    }
    return out;
}



} // namespace utils
} // namespace themis
