#include <gtest/gtest.h>
#include "index/graph_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include <filesystem>
#include <chrono>

using namespace themis;

class TemporalAggregationTest : public ::testing::Test {
protected:
    void SetUp() override {
        testDbPath_ = "test_temporal_aggregation_db";
        std::filesystem::remove_all(testDbPath_);
        
        themis::RocksDBWrapper::Config config;
        config.db_path = testDbPath_;
        config.memtable_size_mb = 64;
        config.block_cache_size_mb = 256;
        config.max_background_jobs = 2;
        config.compression_default = "lz4";
        config.compression_bottommost = "zstd";
        
        db_ = std::make_unique<RocksDBWrapper>(config);
        ASSERT_TRUE(db_->open());
        
        graphIdx_ = std::make_unique<GraphIndexManager>(*db_);
        
        // Create test graph with temporal edges
        createTemporalTestGraph();
    }
    
    void TearDown() override {
        graphIdx_.reset();
        db_->close();
        db_.reset();
        std::filesystem::remove_all(testDbPath_);
    }
    
    void createTemporalTestGraph() {
        // Create nodes
        BaseEntity alice("alice");
        alice.setField("id", std::string("alice"));
        alice.setField("name", std::string("Alice"));
        db_->put("entity:alice", alice.serialize());
        
        BaseEntity bob("bob");
        bob.setField("id", std::string("bob"));
        bob.setField("name", std::string("Bob"));
        db_->put("entity:bob", bob.serialize());
        
        BaseEntity charlie("charlie");
        charlie.setField("id", std::string("charlie"));
        charlie.setField("name", std::string("Charlie"));
        db_->put("entity:charlie", charlie.serialize());
        
        // Create temporal edges with different validity periods
        // Edge 1: valid from 1000 to 2000 (duration: 1000ms)
        BaseEntity edge1("edge1");
        edge1.setField("id", std::string("edge1"));
        edge1.setField("_from", std::string("alice"));
        edge1.setField("_to", std::string("bob"));
        edge1.setField("valid_from", static_cast<int64_t>(1000));
        edge1.setField("valid_to", static_cast<int64_t>(2000));
        edge1.setField("_weight", 1.0);
        graphIdx_->addEdge(edge1);
        
        // Edge 2: valid from 1500 to 3000 (duration: 1500ms)
        BaseEntity edge2("edge2");
        edge2.setField("id", std::string("edge2"));
        edge2.setField("_from", std::string("bob"));
        edge2.setField("_to", std::string("charlie"));
        edge2.setField("valid_from", static_cast<int64_t>(1500));
        edge2.setField("valid_to", static_cast<int64_t>(3000));
        edge2.setField("_weight", 1.0);
        graphIdx_->addEdge(edge2);
        
        // Edge 3: valid from 2500 to 4000 (duration: 1500ms)
        BaseEntity edge3("edge3");
        edge3.setField("id", std::string("edge3"));
        edge3.setField("_from", std::string("alice"));
        edge3.setField("_to", std::string("charlie"));
        edge3.setField("valid_from", static_cast<int64_t>(2500));
        edge3.setField("valid_to", static_cast<int64_t>(4000));
        edge3.setField("_weight", 1.0);
        graphIdx_->addEdge(edge3);
        
        // Edge 4: unbounded (no valid_from/valid_to)
        BaseEntity edge4("edge4");
        edge4.setField("id", std::string("edge4"));
        edge4.setField("_from", std::string("charlie"));
        edge4.setField("_to", std::string("alice"));
        edge4.setField("_weight", 1.0);
        graphIdx_->addEdge(edge4);
        
        // Edge 5: only valid_from (unbounded end)
        BaseEntity edge5("edge5");
        edge5.setField("id", std::string("edge5"));
        edge5.setField("_from", std::string("bob"));
        edge5.setField("_to", std::string("alice"));
        edge5.setField("valid_from", static_cast<int64_t>(3500));
        edge5.setField("_weight", 1.0);
        graphIdx_->addEdge(edge5);
    }
    
    std::string testDbPath_;
    std::unique_ptr<RocksDBWrapper> db_;
    std::unique_ptr<GraphIndexManager> graphIdx_;
};

TEST_F(TemporalAggregationTest, GetTemporalStats_AllEdgesOverlap) {
    // Query range [1000, 4000] should include all 5 edges
    auto [status, stats] = graphIdx_->getTemporalStats(1000, 4000, false);
    
    ASSERT_TRUE(status.ok) << status.message;
    EXPECT_EQ(stats.edge_count, 5);  // All edges have some overlap
    EXPECT_EQ(stats.bounded_edge_count, 3);  // Only edge1, edge2, edge3 have both bounds
    EXPECT_GT(stats.avg_duration_ms, 0.0);
    EXPECT_EQ(stats.total_duration_ms, 4000.0);  // 1000 + 1500 + 1500
    EXPECT_EQ(stats.avg_duration_ms, 4000.0 / 3.0);
    EXPECT_EQ(*stats.min_duration_ms, 1000);
    EXPECT_EQ(*stats.max_duration_ms, 1500);
    EXPECT_EQ(*stats.earliest_start, 1000);
    EXPECT_EQ(*stats.latest_end, 4000);
}

TEST_F(TemporalAggregationTest, GetTemporalStats_PartialOverlap) {
    // Query range [1800, 2800] should include edge1, edge2, edge3
    auto [status, stats] = graphIdx_->getTemporalStats(1800, 2800, false);
    
    ASSERT_TRUE(status.ok) << status.message;
    EXPECT_EQ(stats.edge_count, 4);  // edge1, edge2, edge3, edge4(unbounded)
    EXPECT_EQ(stats.bounded_edge_count, 3);
    EXPECT_EQ(stats.total_duration_ms, 4000.0);
    EXPECT_EQ(*stats.earliest_start, 1000);
    EXPECT_EQ(*stats.latest_end, 4000);
}

TEST_F(TemporalAggregationTest, GetTemporalStats_FullyContainedOnly) {
    // Query range [1000, 2500] with full containment
    // Only edge1 (1000-2000) is fully contained
    auto [status, stats] = graphIdx_->getTemporalStats(1000, 2500, true);
    
    ASSERT_TRUE(status.ok) << status.message;
    EXPECT_EQ(stats.edge_count, 1);  // Only edge1
    EXPECT_EQ(stats.fully_contained_count, 1);
    EXPECT_EQ(stats.bounded_edge_count, 1);
    EXPECT_EQ(stats.total_duration_ms, 1000.0);
    EXPECT_EQ(stats.avg_duration_ms, 1000.0);
    EXPECT_EQ(*stats.min_duration_ms, 1000);
    EXPECT_EQ(*stats.max_duration_ms, 1000);
}

TEST_F(TemporalAggregationTest, GetTemporalStats_NoOverlap) {
    // Query range [5000, 6000] should find only unbounded edge4
    auto [status, stats] = graphIdx_->getTemporalStats(5000, 6000, false);
    
    ASSERT_TRUE(status.ok) << status.message;
    EXPECT_EQ(stats.edge_count, 2);  // edge4 (unbounded) and edge5 (starts at 3500)
    EXPECT_EQ(stats.bounded_edge_count, 0);  // No bounded edges
    EXPECT_EQ(stats.total_duration_ms, 0.0);
    EXPECT_EQ(stats.avg_duration_ms, 0.0);
}

TEST_F(TemporalAggregationTest, GetTemporalStats_ToStringFormat) {
    auto [status, stats] = graphIdx_->getTemporalStats(1000, 4000, false);
    
    ASSERT_TRUE(status.ok) << status.message;
    std::string output = stats.toString();
    
    // Check that toString contains expected information
    EXPECT_NE(output.find("Total edges: 5"), std::string::npos);
    EXPECT_NE(output.find("Bounded edges: 3"), std::string::npos);
    EXPECT_NE(output.find("Average duration"), std::string::npos);
    EXPECT_NE(output.find("Earliest start: 1000"), std::string::npos);
    EXPECT_NE(output.find("Latest end: 4000"), std::string::npos);
}

TEST_F(TemporalAggregationTest, GetTemporalStats_EmptyDatabase) {
    // Create empty database
    std::string emptyPath = "test_empty_temporal_db";
    std::filesystem::remove_all(emptyPath);
    
    themis::RocksDBWrapper::Config config;
    config.db_path = emptyPath;
    config.memtable_size_mb = 64;
    config.block_cache_size_mb = 256;
    
    RocksDBWrapper emptyDb(config);
    ASSERT_TRUE(emptyDb.open());
    GraphIndexManager emptyGraph(emptyDb);
    
    auto [status, stats] = emptyGraph.getTemporalStats(1000, 2000, false);
    
    ASSERT_TRUE(status.ok) << status.message;
    EXPECT_EQ(stats.edge_count, 0);
    EXPECT_EQ(stats.bounded_edge_count, 0);
    EXPECT_EQ(stats.total_duration_ms, 0.0);
    EXPECT_FALSE(stats.earliest_start.has_value());
    EXPECT_FALSE(stats.latest_end.has_value());
    
    emptyDb.close();
    std::filesystem::remove_all(emptyPath);
}
