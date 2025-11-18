// Semantic Query Cache Implementation

#include "query/semantic_cache.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <regex>
#include <cmath>

namespace themis {

SemanticQueryCache::SemanticQueryCache(
    RocksDBWrapper& db, 
    VectorIndexManager& vim,
    const Config& config
) : db_(db), vim_(vim), config_(config) {
    // Initialize vector index for query embeddings
    vim_.init("query_cache", config_.embedding_dim);
}

// Delegating constructor with default config
SemanticQueryCache::SemanticQueryCache(RocksDBWrapper& db, VectorIndexManager& vim)
    : SemanticQueryCache(db, vim, Config{}) {}

// ===== Cache Operations =====

SemanticQueryCache::Status SemanticQueryCache::put(
    std::string_view query,
    std::string_view result_json
) {
    if (query.empty() || result_json.empty()) {
        return Status::Error("Query and result cannot be empty");
    }
    
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    // Check if we need to evict
    if (stats_.current_entries >= config_.max_entries) {
        auto st = evictOne_();
        if (!st.ok) {
            return st;
        }
    }
    
    // Compute query embedding
    auto embedding = computeQueryEmbedding_(query);
    
    // Create cache entry
    CacheEntry entry;
    entry.query = std::string(query);
    entry.result_json = std::string(result_json);
    entry.embedding = embedding;
    entry.created_at = std::chrono::system_clock::now();
    entry.last_accessed = entry.created_at;
    entry.hit_count = 0;
    entry.result_size = result_json.size();
    
    // Save to database
    auto st = saveCacheEntry_(entry);
    if (!st.ok) {
        return st;
    }
    
    // Add to vector index for similarity search
    BaseEntity embEntity(entry.query);
    embEntity.setField("id", entry.query);
    embEntity.setField("query", entry.query);
    embEntity.setField("embedding", embedding);
    
    auto stVec = vim_.addEntity(embEntity, "embedding");
    if (!stVec.ok) {
        return Status::Error("Failed to add to vector index: " + stVec.message);
    }
    
    // Update LRU
    updateLRU_(query);
    
    // Update stats
    stats_.current_entries++;
    stats_.total_result_bytes += result_json.size();
    
    return Status::OK();
}

SemanticQueryCache::LookupResult SemanticQueryCache::get(std::string_view query) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.total_lookups++;
    
    LookupResult result;
    
    // 1. Try exact match first (if enabled)
    if (config_.enable_exact_match) {
        auto entry = loadCacheEntry_(query);
        if (entry.has_value()) {
            // Check expiration
            if (!entry->isExpired(config_.ttl)) {
                result.found = true;
                result.exact_match = true;
                result.result_json = entry->result_json;
                result.similarity = 1.0f;
                result.matched_query = std::string(query);
                
                // Update access time and hit count
                entry->last_accessed = std::chrono::system_clock::now();
                entry->hit_count++;
                saveCacheEntry_(*entry);
                
                // Update LRU
                updateLRU_(query);
                
                stats_.exact_hits++;
                return result;
            } else {
                // Expired entry - remove it (using removeInternal_ since we hold stats_mutex_)
                removeInternal_(query);
            }
        }
    }
    
    // 2. Try similarity match (if enabled)
    if (config_.enable_similarity_match) {
        auto embedding = computeQueryEmbedding_(query);
        
        // Search for similar queries
        auto [st, searchResults] = vim_.searchKnn(embedding, 1);
        if (st.ok && !searchResults.empty()) {
            const auto& match = searchResults[0];
            float similarity = 1.0f - match.distance;  // Convert distance to similarity
            
            if (similarity >= config_.similarity_threshold) {
                // Load the matched cache entry
                auto entry = loadCacheEntry_(match.pk);
                if (entry.has_value() && !entry->isExpired(config_.ttl)) {
                    result.found = true;
                    result.exact_match = false;
                    result.result_json = entry->result_json;
                    result.similarity = similarity;
                    result.matched_query = match.pk;
                    
                    // Update access time and hit count
                    entry->last_accessed = std::chrono::system_clock::now();
                    entry->hit_count++;
                    saveCacheEntry_(*entry);
                    
                    // Update LRU (for matched query)
                    updateLRU_(match.pk);
                    
                    stats_.similarity_hits++;
                    return result;
                }
            }
        }
    }
    
    // 3. Cache miss
    stats_.misses++;
    return result;
}

SemanticQueryCache::Status SemanticQueryCache::remove(std::string_view query) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return removeInternal_(query);
}

SemanticQueryCache::Status SemanticQueryCache::removeInternal_(std::string_view query) {
    // NOTE: Assumes stats_mutex_ is already locked by caller!
    
    // Load entry to get size
    auto entry = loadCacheEntry_(query);
    if (!entry.has_value()) {
        return Status::Error("Entry not found");
    }
    
    // Remove from database
    std::string key = makeCacheEntryKey_(query);
    db_.del(key);
    
    // Remove from exact match index
    std::string exactKey = makeExactMatchKey_(query);
    db_.del(exactKey);
    
    // Note: We don't remove from vector index (would need vector index delete API)
    // Vector index entries become stale but won't match on similarity threshold
    
    // Update LRU
    {
        std::lock_guard<std::mutex> lruLock(lru_mutex_);
        auto it = lru_map_.find(std::string(query));
        if (it != lru_map_.end()) {
            lru_list_.erase(it->second);
            lru_map_.erase(it);
        }
    }
    
    // Update stats
    if (stats_.current_entries > 0) {
        stats_.current_entries--;
    }
    if (stats_.total_result_bytes >= entry->result_size) {
        stats_.total_result_bytes -= entry->result_size;
    }
    
    return Status::OK();
}

SemanticQueryCache::Status SemanticQueryCache::clear() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    // Clear LRU
    {
        std::lock_guard<std::mutex> lruLock(lru_mutex_);
        lru_list_.clear();
        lru_map_.clear();
    }
    
    // Remove all cache entries from database
    db_.scanPrefix("qcache:", [this](std::string_view key, std::string_view /*val*/) {
        db_.del(std::string(key));
        return true;
    });
    
    // Reset stats
    stats_.current_entries = 0;
    stats_.total_result_bytes = 0;
    
    return Status::OK();
}

// ===== Statistics =====

SemanticQueryCache::CacheStats SemanticQueryCache::getStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void SemanticQueryCache::resetStats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.total_lookups = 0;
    stats_.exact_hits = 0;
    stats_.similarity_hits = 0;
    stats_.misses = 0;
    stats_.evictions = 0;
    // Keep current_entries and total_result_bytes
}

// ===== Configuration =====

void SemanticQueryCache::setConfig(const Config& config) {
    config_ = config;
}

SemanticQueryCache::Config SemanticQueryCache::getConfig() const {
    return config_;
}

// ===== Maintenance =====

SemanticQueryCache::Status SemanticQueryCache::evictExpired() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    int evicted = 0;
    std::vector<std::string> toRemove;
    
    // Scan all cache entries
    db_.scanPrefix("qcache:entry:", [this, &toRemove](std::string_view key, std::string_view val) {
        BaseEntity entity = BaseEntity::deserialize(std::string(key), 
                                                    std::vector<uint8_t>(val.begin(), val.end()));
        
        auto createdOpt = entity.getFieldAsInt("created_at");
        if (createdOpt.has_value()) {
            auto created = std::chrono::system_clock::time_point(
                std::chrono::milliseconds(*createdOpt));
            
            auto now = std::chrono::system_clock::now();
            auto age = std::chrono::duration_cast<std::chrono::seconds>(now - created);
            
            if (age > config_.ttl) {
                auto queryOpt = entity.getFieldAsString("query");
                if (queryOpt.has_value()) {
                    toRemove.push_back(*queryOpt);
                }
            }
        }
        return true;
    });
    
    // Remove expired entries (using removeInternal_ since we hold stats_mutex_)
    for (const auto& query : toRemove) {
        removeInternal_(query);
        evicted++;
    }
    
    stats_.evictions += evicted;
    
    return Status::OK();
}

SemanticQueryCache::Status SemanticQueryCache::evictLRU(size_t count) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    for (size_t i = 0; i < count; ++i) {
        auto st = evictOne_();
        if (!st.ok) {
            return st;
        }
    }
    
    return Status::OK();
}

// ===== Helper Methods =====

std::vector<float> SemanticQueryCache::computeQueryEmbedding_(std::string_view query) const {
    // Simple feature-based embedding (MVP)
    // Future: Use sentence encoder (Sentence-BERT) or GPT embeddings
    
    auto features = extractQueryFeatures_(query);
    
    // Create embedding vector
    std::vector<float> embedding(config_.embedding_dim, 0.0f);
    
    // Hash features into embedding space
    int idx = 0;
    for (const auto& [feature, value] : features) {
        // Use feature hash to determine position
        std::hash<std::string> hasher;
        size_t hash = hasher(feature);
        int pos = hash % config_.embedding_dim;
        
        embedding[pos] += value;
        
        // Add to adjacent positions for smoothing
        if (pos > 0) {
            embedding[pos - 1] += value * 0.5f;
        }
        if (pos < config_.embedding_dim - 1) {
            embedding[pos + 1] += value * 0.5f;
        }
        
        idx++;
        if (idx >= 100) break;  // Limit features
    }
    
    // L2 normalization
    float norm = 0.0f;
    for (float val : embedding) {
        norm += val * val;
    }
    norm = std::sqrt(norm);
    
    if (norm > 0.0f) {
        for (float& val : embedding) {
            val /= norm;
        }
    }
    
    return embedding;
}

std::string SemanticQueryCache::makeExactMatchKey_(std::string_view query) const {
    std::ostringstream oss;
    oss << "qcache:exact:" << query;
    return oss.str();
}

std::string SemanticQueryCache::makeCacheEntryKey_(std::string_view query) const {
    std::ostringstream oss;
    oss << "qcache:entry:" << query;
    return oss.str();
}

std::optional<SemanticQueryCache::CacheEntry> 
SemanticQueryCache::loadCacheEntry_(std::string_view query) const {
    std::string key = makeCacheEntryKey_(query);
    auto blob = db_.get(key);
    
    if (!blob.has_value()) {
        return std::nullopt;
    }
    
    BaseEntity entity = BaseEntity::deserialize(key, *blob);
    
    CacheEntry entry;
    
    auto queryOpt = entity.getFieldAsString("query");
    if (queryOpt.has_value()) entry.query = *queryOpt;
    
    auto resultOpt = entity.getFieldAsString("result_json");
    if (resultOpt.has_value()) entry.result_json = *resultOpt;
    
    auto embOpt = entity.getFieldAsVector("embedding");
    if (embOpt.has_value()) entry.embedding = *embOpt;
    
    auto createdOpt = entity.getFieldAsInt("created_at");
    if (createdOpt.has_value()) {
        entry.created_at = std::chrono::system_clock::time_point(
            std::chrono::milliseconds(*createdOpt));
    }
    
    auto accessedOpt = entity.getFieldAsInt("last_accessed");
    if (accessedOpt.has_value()) {
        entry.last_accessed = std::chrono::system_clock::time_point(
            std::chrono::milliseconds(*accessedOpt));
    }
    
    auto hitOpt = entity.getFieldAsInt("hit_count");
    if (hitOpt.has_value()) entry.hit_count = static_cast<int>(*hitOpt);
    
    auto sizeOpt = entity.getFieldAsInt("result_size");
    if (sizeOpt.has_value()) entry.result_size = static_cast<int>(*sizeOpt);
    
    return entry;
}

SemanticQueryCache::Status SemanticQueryCache::saveCacheEntry_(const CacheEntry& entry) {
    std::string key = makeCacheEntryKey_(entry.query);
    
    BaseEntity entity(key);
    entity.setField("id", key);
    entity.setField("query", entry.query);
    entity.setField("result_json", entry.result_json);
    entity.setField("embedding", entry.embedding);
    entity.setField("created_at", std::chrono::duration_cast<std::chrono::milliseconds>(
        entry.created_at.time_since_epoch()).count());
    entity.setField("last_accessed", std::chrono::duration_cast<std::chrono::milliseconds>(
        entry.last_accessed.time_since_epoch()).count());
    entity.setField("hit_count", entry.hit_count);
    entity.setField("result_size", static_cast<int>(entry.result_size));
    
    db_.put(key, entity.serialize());
    
    // Also create exact match index
    std::string exactKey = makeExactMatchKey_(entry.query);
    db_.put(exactKey, std::vector<uint8_t>(entry.query.begin(), entry.query.end()));
    
    return Status::OK();
}

void SemanticQueryCache::updateLRU_(std::string_view query) {
    std::lock_guard<std::mutex> lock(lru_mutex_);
    
    std::string queryStr(query);
    
    // Remove from current position (if exists)
    auto it = lru_map_.find(queryStr);
    if (it != lru_map_.end()) {
        lru_list_.erase(it->second);
    }
    
    // Add to front (most recent)
    lru_list_.push_front(queryStr);
    lru_map_[queryStr] = lru_list_.begin();
}

SemanticQueryCache::Status SemanticQueryCache::evictOne_() {
    // NOTE: Assumes stats_mutex_ is already locked by caller!
    
    std::string lru_query;
    
    // Get LRU query in limited scope
    {
        std::lock_guard<std::mutex> lruLock(lru_mutex_);
        
        if (lru_list_.empty()) {
            return Status::Error("No entries to evict");
        }
        
        // Get least recently used (back of list)
        lru_query = lru_list_.back();
    }
    
    // Remove it (uses removeInternal_ which assumes stats_mutex_ held)
    auto st = removeInternal_(lru_query);
    if (st.ok) {
        stats_.evictions++;
    }
    
    return st;
}

// ===== Feature Extraction =====

std::vector<std::string> SemanticQueryCache::tokenizeQuery_(std::string_view query) const {
    std::vector<std::string> tokens;
    std::string current;
    
    for (char c : query) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            current += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (!current.empty()) {
            tokens.push_back(current);
            current.clear();
        }
    }
    
    if (!current.empty()) {
        tokens.push_back(current);
    }
    
    return tokens;
}

std::map<std::string, float> SemanticQueryCache::extractQueryFeatures_(std::string_view query) const {
    std::map<std::string, float> features;
    
    // Tokenize query
    auto tokens = tokenizeQuery_(query);
    
    // Count token frequencies
    std::map<std::string, int> token_counts;
    for (const auto& token : tokens) {
        token_counts[token]++;
    }
    
    // TF-IDF-like features (simple TF for now)
    float total = static_cast<float>(tokens.size());
    for (const auto& [token, count] : token_counts) {
        features[token] = static_cast<float>(count) / total;
    }
    
    // Add bigram features
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        std::string bigram = tokens[i] + "_" + tokens[i + 1];
        features[bigram] = 0.5f / total;  // Lower weight for bigrams
    }
    
    // Add structural features
    features["length"] = std::min(1.0f, static_cast<float>(query.size()) / 100.0f);
    features["tokens"] = std::min(1.0f, static_cast<float>(tokens.size()) / 20.0f);
    
    // Detect keywords
    std::vector<std::string> keywords = {
        "find", "for", "filter", "return", "sort", "limit",
        "where", "and", "or", "not", "in", "like"
    };
    
    for (const auto& kw : keywords) {
        if (std::find(tokens.begin(), tokens.end(), kw) != tokens.end()) {
            features["kw_" + kw] = 1.0f;
        }
    }
    
    return features;
}

} // namespace themis
