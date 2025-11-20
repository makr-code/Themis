#include "sharding/consistent_hash.h"
#include <sstream>
#include <algorithm>
#include <cmath>
#include <set>

#ifdef __has_include
  #if __has_include(<xxhash.h>)
    #include <xxhash.h>
    #define HAS_XXHASH
  #endif
#endif

namespace themis::sharding {

void ConsistentHashRing::addShard(const std::string& shard_id, size_t virtual_nodes) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // If shard already exists, remove it first
    if (shard_tokens_.find(shard_id) != shard_tokens_.end()) {
        // Don't call removeShard here to avoid double-locking
        auto& tokens = shard_tokens_[shard_id];
        for (uint64_t token : tokens) {
            ring_.erase(token);
        }
        shard_tokens_.erase(shard_id);
    }
    
    std::vector<uint64_t> tokens;
    tokens.reserve(virtual_nodes);
    
    // Generate virtual nodes
    for (size_t i = 0; i < virtual_nodes; ++i) {
        std::ostringstream oss;
        oss << shard_id << "#" << i;
        uint64_t token = hash(oss.str());
        
        ring_[token] = shard_id;
        tokens.push_back(token);
    }
    
    shard_tokens_[shard_id] = std::move(tokens);
}

void ConsistentHashRing::removeShard(const std::string& shard_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = shard_tokens_.find(shard_id);
    if (it == shard_tokens_.end()) {
        return; // Shard not found
    }
    
    // Remove all virtual nodes from the ring
    for (uint64_t token : it->second) {
        ring_.erase(token);
    }
    
    shard_tokens_.erase(it);
}

std::string ConsistentHashRing::getShardForHash(uint64_t hash) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (ring_.empty()) {
        return "";
    }
    
    // Find the first shard at or after this hash (clockwise search)
    auto it = ring_.lower_bound(hash);
    
    // If we've gone past the end, wrap around to the beginning
    if (it == ring_.end()) {
        it = ring_.begin();
    }
    
    return it->second;
}

std::string ConsistentHashRing::getShardForURN(const URN& urn) const {
    return getShardForHash(urn.hash());
}

std::vector<std::string> ConsistentHashRing::getSuccessors(uint64_t hash, size_t count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (ring_.empty() || count == 0) {
        return {};
    }
    
    std::vector<std::string> result;
    std::set<std::string> seen; // Track unique shards
    
    // Start from the position at or after the hash
    auto it = ring_.lower_bound(hash);
    if (it == ring_.end()) {
        it = ring_.begin();
    }
    
    // Collect up to 'count' unique shards
    size_t iterations = 0;
    const size_t max_iterations = ring_.size(); // Prevent infinite loop
    
    while (result.size() < count && iterations < max_iterations) {
        if (seen.find(it->second) == seen.end()) {
            result.push_back(it->second);
            seen.insert(it->second);
        }
        
        ++it;
        if (it == ring_.end()) {
            it = ring_.begin(); // Wrap around
        }
        ++iterations;
    }
    
    return result;
}

std::pair<uint64_t, uint64_t> ConsistentHashRing::getShardRange(const std::string& shard_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = shard_tokens_.find(shard_id);
    if (it == shard_tokens_.end() || it->second.empty()) {
        return {0, 0};
    }
    
    // Find min and max tokens for this shard
    const auto& tokens = it->second;
    uint64_t min_token = *std::min_element(tokens.begin(), tokens.end());
    uint64_t max_token = *std::max_element(tokens.begin(), tokens.end());
    
    return {min_token, max_token};
}

std::vector<std::string> ConsistentHashRing::getAllShards() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> shards;
    shards.reserve(shard_tokens_.size());
    
    for (const auto& [shard_id, _] : shard_tokens_) {
        shards.push_back(shard_id);
    }
    
    return shards;
}

double ConsistentHashRing::getBalanceFactor() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (shard_tokens_.empty()) {
        return 0.0;
    }
    
    // Calculate mean number of virtual nodes per shard
    double total_nodes = static_cast<double>(ring_.size());
    double mean = total_nodes / static_cast<double>(shard_tokens_.size());
    
    // Calculate standard deviation
    double variance = 0.0;
    for (const auto& [shard_id, tokens] : shard_tokens_) {
        double diff = static_cast<double>(tokens.size()) - mean;
        variance += diff * diff;
    }
    variance /= static_cast<double>(shard_tokens_.size());
    
    double std_dev = std::sqrt(variance);
    
    // Return as percentage of mean
    return (std_dev / mean) * 100.0;
}

uint64_t ConsistentHashRing::hash(const std::string& key) const {
#ifdef HAS_XXHASH
    return XXH64(key.data(), key.size(), 0);
#else
    std::hash<std::string> hasher;
    return hasher(key);
#endif
}

} // namespace themis::sharding
