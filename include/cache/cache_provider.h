#pragma once

#include <string>
#include <string_view>
#include <cstdint>

namespace themis { namespace cache {

struct CacheValue {
    std::string payload;   // serialized JSON (entity or result page)
    uint64_t version{0};   // monotone version (e.g., WAL index)
    uint64_t ts_ms{0};     // insert timestamp (ms)
};

class CacheProvider {
public:
    virtual ~CacheProvider() = default;
    virtual bool Get(std::string_view key, CacheValue& out) = 0;
    virtual void Put(std::string_view key, const CacheValue& v, uint64_t ttl_ms) = 0;
    virtual void Invalidate(std::string_view key) = 0;
};

inline std::string makeEntityKey(const std::string& urn) { return urn; }

}} // namespace themis::cache
