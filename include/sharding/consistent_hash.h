#pragma once

#include "sharding/urn.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <functional>
#include <mutex>

namespace themis::sharding {

/**
 * Consistent Hashing Ring for even data distribution
 * 
 * Uses virtual nodes to ensure balanced distribution even with uneven
 * number of shards. Each shard gets multiple positions on the hash ring
 * (virtual nodes) to improve balance.
 * 
 * Benefits:
 * - Minimal data movement on shard add/remove (only 1/N of data moves)
 * - Even distribution with virtual nodes
 * - Fast lookup O(log N) where N is number of virtual nodes
 */
class ConsistentHashRing {
public:
    /**
     * Add a shard to the ring with virtual nodes
     * @param shard_id Unique shard identifier (e.g., "shard_001")
     * @param virtual_nodes Number of virtual nodes (higher = better balance, default 150)
     */
    void addShard(const std::string& shard_id, size_t virtual_nodes = 150);
    
    /**
     * Remove a shard from the ring
     * @param shard_id Shard identifier to remove
     */
    void removeShard(const std::string& shard_id);
    
    /**
     * Get shard for a given key hash
     * Uses clockwise search on the ring to find the first shard
     * @param hash 64-bit hash value
     * @return Shard ID, or empty string if ring is empty
     */
    std::string getShardForHash(uint64_t hash) const;
    
    /**
     * Get shard for a URN
     * Convenience method that hashes the URN and finds the shard
     * @param urn URN to route
     * @return Shard ID, or empty string if ring is empty
     */
    std::string getShardForURN(const URN& urn) const;
    
    /**
     * Get N successor shards (for replication)
     * Returns the next N distinct shards clockwise on the ring
     * @param hash Starting hash position
     * @param count Number of successors to find
     * @return List of shard IDs (may be less than count if fewer shards exist)
     */
    std::vector<std::string> getSuccessors(uint64_t hash, size_t count) const;
    
    /**
     * Get hash range for a shard (min, max)
     * Returns the minimum and maximum hash values this shard is responsible for
     * Note: In consistent hashing, a shard may have multiple ranges (one per virtual node)
     * This returns the overall min/max across all virtual nodes
     * @param shard_id Shard identifier
     * @return Pair of (min_hash, max_hash), or (0, 0) if shard not found
     */
    std::pair<uint64_t, uint64_t> getShardRange(const std::string& shard_id) const;
    
    /**
     * Get all unique shards in the ring
     * @return List of unique shard IDs
     */
    std::vector<std::string> getAllShards() const;
    
    /**
     * Calculate balance factor (standard deviation of virtual nodes per shard)
     * Lower is better. < 5% is considered well-balanced
     * @return Balance factor as percentage (0.0 to 100.0)
     */
    double getBalanceFactor() const;
    
    /**
     * Get total number of virtual nodes in the ring
     * @return Total virtual node count
     */
    size_t getVirtualNodeCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return ring_.size();
    }
    
    /**
     * Get number of unique shards
     * @return Number of unique shards
     */
    size_t getShardCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return shard_tokens_.size();
    }
    
    /**
     * Clear all shards from the ring
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        ring_.clear();
        shard_tokens_.clear();
    }
    
private:
    // Token (hash) → Shard ID mapping
    // The ring is represented as a sorted map where keys are hash positions
    std::map<uint64_t, std::string> ring_;
    
    // Shard ID → Virtual Node Tokens
    // Tracks which hash positions belong to each shard
    std::map<std::string, std::vector<uint64_t>> shard_tokens_;
    
    // Mutex for thread-safe operations
    mutable std::mutex mutex_;
    
    /**
     * Hash function for virtual node generation
     * Combines shard_id and virtual_node_index to generate unique positions
     * @param key String to hash
     * @return 64-bit hash value
     */
    uint64_t hash(const std::string& key) const;
};

} // namespace themis::sharding
