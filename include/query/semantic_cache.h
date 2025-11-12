// Semantic Query Cache - Similarity-Based Query Result Caching
// 
// Instead of exact string matching, uses semantic embeddings to find similar queries.
// Example: "FIND users WHERE age > 30" matches "FIND users WHERE age >= 31"
//
// Architecture:
// - Query → Embedding (sentence encoding or feature hashing)
// - Cache lookup via similarity search (vector index)
// - LRU eviction policy with TTL support
// - Multi-level cache: exact match → similarity match → execute

#pragma once

#include "storage/rocksdb_wrapper.h"
#include "index/vector_index.h"
#include "storage/base_entity.h"
#include <string>
#include <vector>
#include <optional>
#include <map>
#include <chrono>
#include <mutex>
#include <list>

namespace themis {

class SemanticQueryCache {
public:
    // Configuration
    struct Config {
        size_t max_entries = 1000;              // Max cached queries
        float similarity_threshold = 0.85f;      // Min similarity for cache hit (0-1)
        int embedding_dim = 128;                 // Query embedding dimension
        std::chrono::seconds ttl{3600};         // Time-to-live (1 hour default)
        bool enable_exact_match = true;          // Try exact match first
        bool enable_similarity_match = true;     // Fall back to similarity
        
        Config() = default;
    };
    
    // Cache entry metadata
    struct CacheEntry {
        std::string query;                       // Original query string
        std::string result_json;                 // Serialized result
        std::vector<float> embedding;            // Query embedding
        std::chrono::system_clock::time_point created_at;
        std::chrono::system_clock::time_point last_accessed;
        int hit_count = 0;                       // Number of cache hits
        size_t result_size = 0;                  // Size in bytes
        
        bool isExpired(std::chrono::seconds ttl) const {
            auto now = std::chrono::system_clock::now();
            auto age = std::chrono::duration_cast<std::chrono::seconds>(now - created_at);
            return age > ttl;
        }
    };
    
    // Cache lookup result
    struct LookupResult {
        bool found = false;
        bool exact_match = false;                // True if exact string match
        std::string result_json;
        float similarity = 0.0f;                 // Similarity score (if found)
        std::string matched_query;               // Matched query (if different)
        
        LookupResult() = default;
        explicit LookupResult(bool f) : found(f) {}
    };
    
    // Cache statistics
    struct CacheStats {
        size_t total_lookups = 0;
        size_t exact_hits = 0;
        size_t similarity_hits = 0;
        size_t misses = 0;
        size_t evictions = 0;
        size_t current_entries = 0;
        size_t total_result_bytes = 0;
        
        float hitRate() const {
            return total_lookups > 0 
                ? static_cast<float>(exact_hits + similarity_hits) / total_lookups 
                : 0.0f;
        }
        
        float exactHitRate() const {
            return total_lookups > 0
                ? static_cast<float>(exact_hits) / total_lookups
                : 0.0f;
        }
        
        float similarityHitRate() const {
            return total_lookups > 0
                ? static_cast<float>(similarity_hits) / total_lookups
                : 0.0f;
        }
    };
    
    // Status type
    struct Status {
        bool ok = false;
        std::string message;
        
        static Status OK() { return Status{true, ""}; }
        static Status Error(std::string msg) { return Status{false, std::move(msg)}; }
    };

public:
    SemanticQueryCache(RocksDBWrapper& db, VectorIndexManager& vim);
    SemanticQueryCache(RocksDBWrapper& db, VectorIndexManager& vim, const Config& config);
    ~SemanticQueryCache() = default;
    
    // Cache operations
    Status put(std::string_view query, std::string_view result_json);
    LookupResult get(std::string_view query);
    Status remove(std::string_view query);
    Status clear();
    
    // Statistics
    CacheStats getStats() const;
    void resetStats();
    
    // Configuration
    void setConfig(const Config& config);
    Config getConfig() const;
    
    // Maintenance
    Status evictExpired();                       // Remove expired entries
    Status evictLRU(size_t count = 1);          // Evict least recently used
    
private:
    // Helper methods
    std::vector<float> computeQueryEmbedding_(std::string_view query) const;
    std::string makeExactMatchKey_(std::string_view query) const;
    std::string makeCacheEntryKey_(std::string_view query) const;
    std::optional<CacheEntry> loadCacheEntry_(std::string_view query) const;
    Status saveCacheEntry_(const CacheEntry& entry);
    Status removeInternal_(std::string_view query);  // Internal remove (assumes lock held)
    void updateLRU_(std::string_view query);
    Status evictOne_();
    
    // Feature extraction for query embedding
    std::vector<std::string> tokenizeQuery_(std::string_view query) const;
    std::map<std::string, float> extractQueryFeatures_(std::string_view query) const;
    
private:
    RocksDBWrapper& db_;
    VectorIndexManager& vim_;
    Config config_;
    
    // LRU tracking (in-memory, rebuilt on startup)
    mutable std::mutex lru_mutex_;
    std::list<std::string> lru_list_;            // Most recent at front
    std::map<std::string, std::list<std::string>::iterator> lru_map_;
    
    // Statistics (in-memory)
    mutable std::mutex stats_mutex_;
    mutable CacheStats stats_;
};

} // namespace themis
