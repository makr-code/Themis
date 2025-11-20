#include <gtest/gtest.h>
#include "sharding/urn.h"
#include "sharding/consistent_hash.h"
#include "sharding/shard_topology.h"
#include "sharding/urn_resolver.h"
#include <memory>

using namespace themis::sharding;

// ============================================================================
// URN Tests
// ============================================================================

TEST(URNTest, ParseValidURN) {
    auto urn = URN::parse("urn:themis:relational:customers:users:550e8400-e29b-41d4-a716-446655440000");
    
    ASSERT_TRUE(urn.has_value());
    EXPECT_EQ(urn->model, "relational");
    EXPECT_EQ(urn->namespace_, "customers");
    EXPECT_EQ(urn->collection, "users");
    EXPECT_EQ(urn->uuid, "550e8400-e29b-41d4-a716-446655440000");
}

TEST(URNTest, ParseGraphURN) {
    auto urn = URN::parse("urn:themis:graph:social:nodes:7c9e6679-7425-40de-944b-e07fc1f90ae7");
    
    ASSERT_TRUE(urn.has_value());
    EXPECT_EQ(urn->model, "graph");
    EXPECT_EQ(urn->namespace_, "social");
    EXPECT_EQ(urn->collection, "nodes");
}

TEST(URNTest, ParseVectorURN) {
    auto urn = URN::parse("urn:themis:vector:embeddings:documents:f47ac10b-58cc-4372-a567-0e02b2c3d479");
    
    ASSERT_TRUE(urn.has_value());
    EXPECT_EQ(urn->model, "vector");
}

TEST(URNTest, ParseInvalidPrefix) {
    auto urn = URN::parse("urn:wrong:relational:customers:users:550e8400-e29b-41d4-a716-446655440000");
    EXPECT_FALSE(urn.has_value());
}

TEST(URNTest, ParseInvalidUUID) {
    auto urn = URN::parse("urn:themis:relational:customers:users:invalid-uuid");
    EXPECT_FALSE(urn.has_value());
}

TEST(URNTest, ParseInvalidModel) {
    auto urn = URN::parse("urn:themis:invalidmodel:customers:users:550e8400-e29b-41d4-a716-446655440000");
    EXPECT_FALSE(urn.has_value());
}

TEST(URNTest, ParseTooFewParts) {
    auto urn = URN::parse("urn:themis:relational:customers");
    EXPECT_FALSE(urn.has_value());
}

TEST(URNTest, ToStringRoundTrip) {
    std::string original = "urn:themis:relational:customers:users:550e8400-e29b-41d4-a716-446655440000";
    auto urn = URN::parse(original);
    
    ASSERT_TRUE(urn.has_value());
    EXPECT_EQ(urn->toString(), original);
}

TEST(URNTest, ValidUUID) {
    auto urn = URN::parse("urn:themis:relational:test:test:550e8400-e29b-41d4-a716-446655440000");
    ASSERT_TRUE(urn.has_value());
    EXPECT_TRUE(urn->isValidUUID());
}

TEST(URNTest, GetResourceId) {
    auto urn = URN::parse("urn:themis:relational:customers:users:550e8400-e29b-41d4-a716-446655440000");
    ASSERT_TRUE(urn.has_value());
    EXPECT_EQ(urn->getResourceId(), "users:550e8400-e29b-41d4-a716-446655440000");
}

TEST(URNTest, Hash) {
    auto urn1 = URN::parse("urn:themis:relational:customers:users:550e8400-e29b-41d4-a716-446655440000");
    auto urn2 = URN::parse("urn:themis:relational:customers:users:550e8400-e29b-41d4-a716-446655440000");
    auto urn3 = URN::parse("urn:themis:relational:customers:users:7c9e6679-7425-40de-944b-e07fc1f90ae7");
    
    ASSERT_TRUE(urn1.has_value() && urn2.has_value() && urn3.has_value());
    
    // Same URN should produce same hash
    EXPECT_EQ(urn1->hash(), urn2->hash());
    
    // Different URNs should (likely) produce different hashes
    EXPECT_NE(urn1->hash(), urn3->hash());
}

TEST(URNTest, Equality) {
    auto urn1 = URN::parse("urn:themis:relational:customers:users:550e8400-e29b-41d4-a716-446655440000");
    auto urn2 = URN::parse("urn:themis:relational:customers:users:550e8400-e29b-41d4-a716-446655440000");
    auto urn3 = URN::parse("urn:themis:relational:customers:users:7c9e6679-7425-40de-944b-e07fc1f90ae7");
    
    ASSERT_TRUE(urn1.has_value() && urn2.has_value() && urn3.has_value());
    
    EXPECT_EQ(*urn1, *urn2);
    EXPECT_NE(*urn1, *urn3);
}

// ============================================================================
// ConsistentHashRing Tests
// ============================================================================

TEST(ConsistentHashTest, AddShard) {
    ConsistentHashRing ring;
    ring.addShard("shard_001");
    
    EXPECT_EQ(ring.getShardCount(), 1);
    EXPECT_EQ(ring.getVirtualNodeCount(), 150); // Default virtual nodes
}

TEST(ConsistentHashTest, AddMultipleShards) {
    ConsistentHashRing ring;
    ring.addShard("shard_001");
    ring.addShard("shard_002");
    ring.addShard("shard_003");
    
    EXPECT_EQ(ring.getShardCount(), 3);
    EXPECT_EQ(ring.getVirtualNodeCount(), 450); // 3 shards * 150 virtual nodes
}

TEST(ConsistentHashTest, RemoveShard) {
    ConsistentHashRing ring;
    ring.addShard("shard_001");
    ring.addShard("shard_002");
    
    EXPECT_EQ(ring.getShardCount(), 2);
    
    ring.removeShard("shard_001");
    
    EXPECT_EQ(ring.getShardCount(), 1);
    EXPECT_EQ(ring.getVirtualNodeCount(), 150);
}

TEST(ConsistentHashTest, GetShardForHash) {
    ConsistentHashRing ring;
    ring.addShard("shard_001");
    ring.addShard("shard_002");
    
    uint64_t hash = 12345;
    std::string shard = ring.getShardForHash(hash);
    
    EXPECT_FALSE(shard.empty());
    EXPECT_TRUE(shard == "shard_001" || shard == "shard_002");
}

TEST(ConsistentHashTest, GetShardForURN) {
    ConsistentHashRing ring;
    ring.addShard("shard_001");
    ring.addShard("shard_002");
    ring.addShard("shard_003");
    
    auto urn = URN::parse("urn:themis:relational:customers:users:550e8400-e29b-41d4-a716-446655440000");
    ASSERT_TRUE(urn.has_value());
    
    std::string shard = ring.getShardForURN(*urn);
    EXPECT_FALSE(shard.empty());
}

TEST(ConsistentHashTest, ConsistentMapping) {
    ConsistentHashRing ring;
    ring.addShard("shard_001");
    ring.addShard("shard_002");
    
    auto urn = URN::parse("urn:themis:relational:customers:users:550e8400-e29b-41d4-a716-446655440000");
    ASSERT_TRUE(urn.has_value());
    
    std::string shard1 = ring.getShardForURN(*urn);
    std::string shard2 = ring.getShardForURN(*urn);
    
    // Same URN should always map to same shard
    EXPECT_EQ(shard1, shard2);
}

TEST(ConsistentHashTest, GetSuccessors) {
    ConsistentHashRing ring;
    ring.addShard("shard_001");
    ring.addShard("shard_002");
    ring.addShard("shard_003");
    
    uint64_t hash = 12345;
    auto successors = ring.getSuccessors(hash, 2);
    
    EXPECT_EQ(successors.size(), 2);
    // All successors should be unique
    EXPECT_NE(successors[0], successors[1]);
}

TEST(ConsistentHashTest, GetAllShards) {
    ConsistentHashRing ring;
    ring.addShard("shard_001");
    ring.addShard("shard_002");
    ring.addShard("shard_003");
    
    auto shards = ring.getAllShards();
    
    EXPECT_EQ(shards.size(), 3);
    EXPECT_TRUE(std::find(shards.begin(), shards.end(), "shard_001") != shards.end());
    EXPECT_TRUE(std::find(shards.begin(), shards.end(), "shard_002") != shards.end());
    EXPECT_TRUE(std::find(shards.begin(), shards.end(), "shard_003") != shards.end());
}

TEST(ConsistentHashTest, BalanceFactor) {
    ConsistentHashRing ring;
    ring.addShard("shard_001");
    ring.addShard("shard_002");
    ring.addShard("shard_003");
    
    double balance = ring.getBalanceFactor();
    
    // With equal virtual nodes, balance should be very low (< 1%)
    EXPECT_LT(balance, 1.0);
}

// ============================================================================
// ShardTopology Tests
// ============================================================================

TEST(ShardTopologyTest, AddAndGetShard) {
    ShardTopology::Config config{
        .metadata_endpoint = "http://localhost:2379",
        .cluster_name = "test-cluster",
        .refresh_interval_sec = 0,
        .enable_health_checks = false
    };
    
    ShardTopology topology(config);
    
    ShardInfo shard{
        .shard_id = "shard_001",
        .primary_endpoint = "localhost:8080",
        .replica_endpoints = {"localhost:8081", "localhost:8082"},
        .datacenter = "dc1",
        .rack = "rack1",
        .token_start = 0,
        .token_end = 1000,
        .is_healthy = true
    };
    
    topology.addShard(shard);
    
    auto retrieved = topology.getShard("shard_001");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->shard_id, "shard_001");
    EXPECT_EQ(retrieved->primary_endpoint, "localhost:8080");
    EXPECT_TRUE(retrieved->is_healthy);
}

TEST(ShardTopologyTest, RemoveShard) {
    ShardTopology::Config config{
        .metadata_endpoint = "http://localhost:2379",
        .cluster_name = "test-cluster",
        .refresh_interval_sec = 0,
        .enable_health_checks = false
    };
    
    ShardTopology topology(config);
    
    ShardInfo shard{
        .shard_id = "shard_001",
        .primary_endpoint = "localhost:8080",
        .is_healthy = true
    };
    
    topology.addShard(shard);
    EXPECT_TRUE(topology.hasShard("shard_001"));
    
    topology.removeShard("shard_001");
    EXPECT_FALSE(topology.hasShard("shard_001"));
}

TEST(ShardTopologyTest, UpdateHealth) {
    ShardTopology::Config config{
        .metadata_endpoint = "http://localhost:2379",
        .cluster_name = "test-cluster",
        .refresh_interval_sec = 0,
        .enable_health_checks = false
    };
    
    ShardTopology topology(config);
    
    ShardInfo shard{
        .shard_id = "shard_001",
        .primary_endpoint = "localhost:8080",
        .is_healthy = true
    };
    
    topology.addShard(shard);
    
    topology.updateHealth("shard_001", false);
    
    auto retrieved = topology.getShard("shard_001");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_FALSE(retrieved->is_healthy);
}

TEST(ShardTopologyTest, GetHealthyShards) {
    ShardTopology::Config config{
        .metadata_endpoint = "http://localhost:2379",
        .cluster_name = "test-cluster",
        .refresh_interval_sec = 0,
        .enable_health_checks = false
    };
    
    ShardTopology topology(config);
    
    topology.addShard(ShardInfo{.shard_id = "shard_001", .is_healthy = true});
    topology.addShard(ShardInfo{.shard_id = "shard_002", .is_healthy = false});
    topology.addShard(ShardInfo{.shard_id = "shard_003", .is_healthy = true});
    
    auto healthy = topology.getHealthyShards();
    
    EXPECT_EQ(healthy.size(), 2);
}

// ============================================================================
// URNResolver Tests
// ============================================================================

TEST(URNResolverTest, ResolvePrimary) {
    auto topology = std::make_shared<ShardTopology>(ShardTopology::Config{
        .metadata_endpoint = "http://localhost:2379",
        .cluster_name = "test-cluster",
        .refresh_interval_sec = 0,
        .enable_health_checks = false
    });
    
    auto hash_ring = std::make_shared<ConsistentHashRing>();
    hash_ring->addShard("shard_001");
    hash_ring->addShard("shard_002");
    
    topology->addShard(ShardInfo{
        .shard_id = "shard_001",
        .primary_endpoint = "localhost:8080",
        .is_healthy = true
    });
    
    topology->addShard(ShardInfo{
        .shard_id = "shard_002",
        .primary_endpoint = "localhost:8081",
        .is_healthy = true
    });
    
    URNResolver resolver(topology, hash_ring);
    
    auto urn = URN::parse("urn:themis:relational:customers:users:550e8400-e29b-41d4-a716-446655440000");
    ASSERT_TRUE(urn.has_value());
    
    auto shard = resolver.resolvePrimary(*urn);
    ASSERT_TRUE(shard.has_value());
    EXPECT_TRUE(shard->shard_id == "shard_001" || shard->shard_id == "shard_002");
}

TEST(URNResolverTest, IsLocal) {
    auto topology = std::make_shared<ShardTopology>(ShardTopology::Config{
        .metadata_endpoint = "http://localhost:2379",
        .cluster_name = "test-cluster",
        .refresh_interval_sec = 0,
        .enable_health_checks = false
    });
    
    auto hash_ring = std::make_shared<ConsistentHashRing>();
    hash_ring->addShard("shard_001");
    hash_ring->addShard("shard_002");
    
    URNResolver resolver(topology, hash_ring, "shard_001");
    
    auto urn = URN::parse("urn:themis:relational:customers:users:550e8400-e29b-41d4-a716-446655440000");
    ASSERT_TRUE(urn.has_value());
    
    std::string target_shard = hash_ring->getShardForURN(*urn);
    bool should_be_local = (target_shard == "shard_001");
    
    EXPECT_EQ(resolver.isLocal(*urn), should_be_local);
}
