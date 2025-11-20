#include "sharding/shard_topology.h"
#include <algorithm>

namespace themis::sharding {

ShardTopology::ShardTopology(const Config& config) 
    : config_(config) {
    // TODO: Initialize connection to metadata store (etcd)
    // For now, we'll use in-memory storage
}

void ShardTopology::addShard(const ShardInfo& shard) {
    std::lock_guard<std::mutex> lock(mutex_);
    shards_[shard.shard_id] = shard;
}

void ShardTopology::removeShard(const std::string& shard_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    shards_.erase(shard_id);
}

std::optional<ShardInfo> ShardTopology::getShard(const std::string& shard_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = shards_.find(shard_id);
    if (it == shards_.end()) {
        return std::nullopt;
    }
    
    return it->second;
}

std::vector<ShardInfo> ShardTopology::getAllShards() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<ShardInfo> result;
    result.reserve(shards_.size());
    
    for (const auto& [id, info] : shards_) {
        result.push_back(info);
    }
    
    return result;
}

std::vector<ShardInfo> ShardTopology::getHealthyShards() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<ShardInfo> result;
    
    for (const auto& [id, info] : shards_) {
        if (info.is_healthy) {
            result.push_back(info);
        }
    }
    
    return result;
}

void ShardTopology::updateHealth(const std::string& shard_id, bool is_healthy) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = shards_.find(shard_id);
    if (it != shards_.end()) {
        it->second.is_healthy = is_healthy;
    }
}

void ShardTopology::refresh() {
    // Load latest topology from metadata store
    loadFromMetadataStore();
}

void ShardTopology::save() {
    // Save current topology to metadata store
    saveToMetadataStore();
}

void ShardTopology::loadFromMetadataStore() {
    // TODO: Implement etcd integration
    // For Phase 1, we use in-memory storage
    // In Phase 2+, this will connect to etcd and load the shard map:
    //
    // Example etcd keys:
    //   /themis/{cluster_name}/shards/{shard_id}/endpoint
    //   /themis/{cluster_name}/shards/{shard_id}/datacenter
    //   /themis/{cluster_name}/shards/{shard_id}/health
    //   /themis/{cluster_name}/shards/{shard_id}/certificate
    //
    // For now, this is a no-op. Shards must be added via addShard()
}

void ShardTopology::saveToMetadataStore() {
    // TODO: Implement etcd integration
    // For Phase 1, this is a no-op
    // In Phase 2+, this will persist the current topology to etcd
}

} // namespace themis::sharding
