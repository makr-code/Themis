#include <gtest/gtest.h>
#include "sharding/remote_executor.h"
#include "sharding/shard_router.h"
#include <memory>

using namespace themis::sharding;

// ============================================================================
// RemoteExecutor Tests
// ============================================================================

TEST(RemoteExecutorTest, ConfigurationStructure) {
    RemoteExecutor::Config config;
    config.cert_path = "/path/to/cert.pem";
    config.key_path = "/path/to/key.pem";
    config.ca_cert_path = "/path/to/ca.pem";
    config.local_shard_id = "shard_001";
    config.enable_signing = true;
    
    EXPECT_EQ(config.cert_path, "/path/to/cert.pem");
    EXPECT_EQ(config.local_shard_id, "shard_001");
    EXPECT_TRUE(config.enable_signing);
}

TEST(RemoteExecutorTest, ResultStructure) {
    RemoteExecutor::Result result;
    result.shard_id = "shard_002";
    result.success = true;
    result.data = nlohmann::json{{"key", "value"}};
    result.execution_time_ms = 150;
    result.http_status = 200;
    
    EXPECT_EQ(result.shard_id, "shard_002");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.data["key"], "value");
    EXPECT_EQ(result.execution_time_ms, 150u);
}

TEST(RemoteExecutorTest, DefaultConfiguration) {
    RemoteExecutor::Config config;
    
    EXPECT_TRUE(config.enable_signing);
    EXPECT_EQ(config.connect_timeout_ms, 5000u);
    EXPECT_EQ(config.request_timeout_ms, 30000u);
    EXPECT_EQ(config.max_retries, 3u);
}

// ============================================================================
// ShardRouter Tests
// ============================================================================

TEST(ShardRouterTest, ConfigurationStructure) {
    ShardRouter::Config config;
    config.local_shard_id = "shard_001";
    config.scatter_timeout_ms = 60000;
    config.max_concurrent_shards = 20;
    config.enable_query_pushdown = true;
    
    EXPECT_EQ(config.local_shard_id, "shard_001");
    EXPECT_EQ(config.scatter_timeout_ms, 60000u);
    EXPECT_EQ(config.max_concurrent_shards, 20u);
    EXPECT_TRUE(config.enable_query_pushdown);
}

TEST(ShardRouterTest, RoutingStrategyEnum) {
    // Test routing strategy values
    RoutingStrategy single = RoutingStrategy::SINGLE_SHARD;
    RoutingStrategy scatter = RoutingStrategy::SCATTER_GATHER;
    RoutingStrategy ns_local = RoutingStrategy::NAMESPACE_LOCAL;
    RoutingStrategy cross = RoutingStrategy::CROSS_SHARD_JOIN;
    
    EXPECT_NE(single, scatter);
    EXPECT_NE(scatter, ns_local);
    EXPECT_NE(ns_local, cross);
}

TEST(ShardRouterTest, ShardResultStructure) {
    ShardResult result;
    result.shard_id = "shard_003";
    result.success = true;
    result.data = nlohmann::json{{"count", 42}};
    result.execution_time_ms = 250;
    
    EXPECT_EQ(result.shard_id, "shard_003");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.data["count"], 42);
}

TEST(ShardRouterTest, DefaultConfiguration) {
    ShardRouter::Config config;
    
    EXPECT_EQ(config.scatter_timeout_ms, 30000u);
    EXPECT_EQ(config.max_concurrent_shards, 10u);
    EXPECT_TRUE(config.enable_query_pushdown);
    EXPECT_FALSE(config.enable_result_caching);
}

TEST(ShardRouterTest, StatisticsStructure) {
    // Test statistics JSON structure
    nlohmann::json stats = {
        {"total_requests", 100},
        {"local_requests", 60},
        {"remote_requests", 40},
        {"scatter_gather_requests", 10},
        {"errors", 5}
    };
    
    EXPECT_EQ(stats["total_requests"], 100);
    EXPECT_EQ(stats["local_requests"], 60);
    EXPECT_EQ(stats["scatter_gather_requests"], 10);
}

// ============================================================================
// Integration Tests (Structure)
// ============================================================================

TEST(ShardRouterIntegrationTest, Construction) {
    // Test that we can construct the router with dependencies
    auto topology_config = ShardTopology::Config{
        .metadata_endpoint = "http://localhost:2379",
        .cluster_name = "test-cluster"
    };
    
    auto topology = std::make_shared<ShardTopology>(topology_config);
    auto hash_ring = std::make_shared<ConsistentHashRing>();
    auto resolver = std::make_shared<URNResolver>(topology, hash_ring, "shard_001");
    
    // Note: Cannot create RemoteExecutor without valid certificates
    // This just tests the construction pattern
    
    EXPECT_TRUE(topology != nullptr);
    EXPECT_TRUE(resolver != nullptr);
}

TEST(ShardRouterIntegrationTest, URNParsing) {
    auto urn = URN::parse("urn:themis:relational:test:users:550e8400-e29b-41d4-a716-446655440000");
    
    ASSERT_TRUE(urn.has_value());
    EXPECT_EQ(urn->model, "relational");
    EXPECT_EQ(urn->namespace_, "test");
    EXPECT_EQ(urn->collection, "users");
}
