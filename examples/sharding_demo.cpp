#include "sharding/urn.h"
#include "sharding/consistent_hash.h"
#include "sharding/shard_topology.h"
#include "sharding/urn_resolver.h"
#include <iostream>
#include <memory>

using namespace themis::sharding;

int main() {
    std::cout << "=== ThemisDB Horizontal Sharding Demo ===" << std::endl;
    
    // Create URN
    auto urn = URN::parse("urn:themis:relational:customers:users:550e8400-e29b-41d4-a716-446655440000");
    std::cout << "URN: " << urn->toString() << std::endl;
    
    // Setup hash ring
    auto hash_ring = std::make_shared<ConsistentHashRing>();
    hash_ring->addShard("shard_001", 150);
    hash_ring->addShard("shard_002", 150);
    
    std::cout << "Shards: " << hash_ring->getShardCount() << std::endl;
    std::cout << "Target shard: " << hash_ring->getShardForURN(*urn) << std::endl;
    
    return 0;
}
