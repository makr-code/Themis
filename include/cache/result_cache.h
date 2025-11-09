#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace themis { namespace cache {

// Query-Result-Cache (AQL) – speichert seitenweise Ergebnisse unter Plan-Hash
class ResultCache {
public:
    virtual ~ResultCache() = default;

    // Key-Komponenten
    struct Key {
        std::string plan_hash;    // normalized query + params
        std::string namespace_;   // tenant isolation
        std::string shard_scope;  // all|namespace|single-shard
        uint64_t page{0};         // page index
    };

    struct Entry {
        std::string page_json;    // serialized JSON array
        uint64_t ts_ms{0};        // insert timestamp
        uint64_t ttl_ms{0};       // time to live
    };

    virtual std::optional<Entry> Get(const Key& k) = 0;
    virtual void Put(const Key& k, const Entry& e) = 0;
    virtual void InvalidatePlan(const std::string& plan_hash) = 0;
};

}} // namespace themis::cache
