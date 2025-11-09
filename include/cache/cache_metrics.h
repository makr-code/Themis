#pragma once

#include "cache/l1_tinylfu_cache.h"
#include "utils/hkdf_cache.h"
#include <string>
#include <sstream>

namespace themis { namespace cache {

// Prometheus-Metriken für Caches
class CacheMetrics {
public:
    static CacheMetrics& instance() {
        static CacheMetrics inst;
        return inst;
    }

    // Register Cache-Instanzen (optional)
    void registerL1Cache(L1TinyLFUCache* cache, const std::string& name) {
        l1_caches_[name] = cache;
    }

    // Export Prometheus-Metriken
    std::string toPrometheus() const {
        std::ostringstream oss;
        
        // L1 Caches
        for (const auto& kv : l1_caches_) {
            auto stats = kv.second->getStats();
            oss << "# HELP themis_cache_hits_total Total cache hits\n";
            oss << "# TYPE themis_cache_hits_total counter\n";
            oss << "themis_cache_hits_total{cache=\"" << kv.first << "\"} " << stats.hits << "\n";
            
            oss << "# HELP themis_cache_misses_total Total cache misses\n";
            oss << "# TYPE themis_cache_misses_total counter\n";
            oss << "themis_cache_misses_total{cache=\"" << kv.first << "\"} " << stats.misses << "\n";
            
            oss << "# HELP themis_cache_evictions_total Total cache evictions\n";
            oss << "# TYPE themis_cache_evictions_total counter\n";
            oss << "themis_cache_evictions_total{cache=\"" << kv.first << "\"} " << stats.evictions << "\n";
            
            oss << "# HELP themis_cache_admissions_total Total cache admissions\n";
            oss << "# TYPE themis_cache_admissions_total counter\n";
            oss << "themis_cache_admissions_total{cache=\"" << kv.first << "\"} " << stats.admissions << "\n";
            
            oss << "# HELP themis_cache_size Current cache size\n";
            oss << "# TYPE themis_cache_size gauge\n";
            oss << "themis_cache_size{cache=\"" << kv.first << "\"} " << stats.size << "\n";
            
            oss << "# HELP themis_cache_hit_rate Cache hit rate\n";
            oss << "# TYPE themis_cache_hit_rate gauge\n";
            oss << "themis_cache_hit_rate{cache=\"" << kv.first << "\"} " << stats.hit_rate() << "\n";
        }
        
        return oss.str();
    }

private:
    CacheMetrics() = default;
    std::unordered_map<std::string, L1TinyLFUCache*> l1_caches_;
};

}} // namespace themis::cache
