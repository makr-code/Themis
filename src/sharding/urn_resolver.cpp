#include "sharding/urn_resolver.h"

namespace themis::sharding {

URNResolver::URNResolver(
    std::shared_ptr<ShardTopology> topology,
    std::shared_ptr<ConsistentHashRing> hash_ring,
    const std::string& local_shard_id
) : topology_(topology), 
    hash_ring_(hash_ring),
    local_shard_id_(local_shard_id) {
}

std::optional<ShardInfo> URNResolver::resolvePrimary(const URN& urn) const {
    // Get shard ID from consistent hash ring
    std::string shard_id = hash_ring_->getShardForURN(urn);
    
    if (shard_id.empty()) {
        return std::nullopt;
    }
    
    // Look up shard info from topology
    return topology_->getShard(shard_id);
}

std::vector<ShardInfo> URNResolver::resolveReplicas(const URN& urn, size_t replica_count) const {
    std::vector<ShardInfo> result;
    
    // Get primary shard
    auto primary = resolvePrimary(urn);
    if (!primary) {
        return result; // Empty result
    }
    
    result.push_back(*primary);
    
    // Get successor shards from the hash ring (for replication)
    uint64_t hash = urn.hash();
    std::vector<std::string> successor_ids = hash_ring_->getSuccessors(hash, replica_count + 1);
    
    // Skip the first one (it's the primary), add the rest
    for (size_t i = 1; i < successor_ids.size() && result.size() <= replica_count; ++i) {
        auto replica = topology_->getShard(successor_ids[i]);
        if (replica && replica->is_healthy) {
            result.push_back(*replica);
        }
    }
    
    return result;
}

bool URNResolver::isLocal(const URN& urn) const {
    if (local_shard_id_.empty()) {
        return false; // No local shard configured
    }
    
    std::string shard_id = hash_ring_->getShardForURN(urn);
    return shard_id == local_shard_id_;
}

std::string URNResolver::getShardId(const URN& urn) const {
    return hash_ring_->getShardForURN(urn);
}

std::vector<ShardInfo> URNResolver::getAllShards() const {
    return topology_->getAllShards();
}

std::vector<ShardInfo> URNResolver::getHealthyShards() const {
    return topology_->getHealthyShards();
}

void URNResolver::refreshTopology() {
    topology_->refresh();
}

} // namespace themis::sharding
