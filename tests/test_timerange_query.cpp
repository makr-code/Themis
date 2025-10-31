// Unit-Tests für Time-Range Queries (Temporal Graph Extension)

#include <gtest/gtest.h>
#include "index/graph_index.h"
#include "index/temporal_graph.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include <filesystem>

using namespace themis;

class TimeRangeQueryTest : public ::testing::Test {
protected:
    std::unique_ptr<RocksDBWrapper> db;
    std::unique_ptr<GraphIndexManager> graphIdx;
    std::string dbPath = "data/themis_timerange_test";

    void SetUp() override {
        if (std::filesystem::exists(dbPath)) {
            std::filesystem::remove_all(dbPath);
        }

        RocksDBWrapper::Config config;
        config.db_path = dbPath;
        config.memtable_size_mb = 64;
        config.block_cache_size_mb = 256;
        
        db = std::make_unique<RocksDBWrapper>(config);
        ASSERT_TRUE(db->open());
        graphIdx = std::make_unique<GraphIndexManager>(*db);
    }

    void TearDown() override {
        graphIdx.reset();
        db.reset();
        if (std::filesystem::exists(dbPath)) {
            std::filesystem::remove_all(dbPath);
        }
    }

    // Helper: Create edges with different temporal periods
    void createTemporalEdges() {
        // Edge e1: A -> B, valid 1000-2000
        BaseEntity e1("e1");
        e1.setField("id", "e1");
        e1.setField("_from", "A");
        e1.setField("_to", "B");
        e1.setField("valid_from", 1000);
        e1.setField("valid_to", 2000);
        graphIdx->addEdge(e1);

        // Edge e2: A -> C, valid 1500-3000
        BaseEntity e2("e2");
        e2.setField("id", "e2");
        e2.setField("_from", "A");
        e2.setField("_to", "C");
        e2.setField("valid_from", 1500);
        e2.setField("valid_to", 3000);
        graphIdx->addEdge(e2);

        // Edge e3: B -> C, valid 2500-4000
        BaseEntity e3("e3");
        e3.setField("id", "e3");
        e3.setField("_from", "B");
        e3.setField("_to", "C");
        e3.setField("valid_from", 2500);
        e3.setField("valid_to", 4000);
        graphIdx->addEdge(e3);

        // Edge e4: C -> D, no temporal bounds (always valid)
        BaseEntity e4("e4");
        e4.setField("id", "e4");
        e4.setField("_from", "C");
        e4.setField("_to", "D");
        graphIdx->addEdge(e4);
    }
};

TEST_F(TimeRangeQueryTest, TimeRangeFilter_Overlap) {
    TimeRangeFilter filter = TimeRangeFilter::between(1200, 1800);
    
    // Edge [1000, 2000] overlaps with [1200, 1800]
    EXPECT_TRUE(filter.hasOverlap(1000, 2000));
    
    // Edge [1500, 3000] overlaps with [1200, 1800]
    EXPECT_TRUE(filter.hasOverlap(1500, 3000));
    
    // Edge [2500, 4000] does NOT overlap with [1200, 1800]
    EXPECT_FALSE(filter.hasOverlap(2500, 4000));
    
    // Edge [500, 1100] overlaps partially with [1200, 1800]
    EXPECT_FALSE(filter.hasOverlap(500, 1100));
}

TEST_F(TimeRangeQueryTest, TimeRangeFilter_FullContainment) {
    TimeRangeFilter filter = TimeRangeFilter::between(1000, 3000);
    
    // Edge [1200, 1800] is fully contained in [1000, 3000]
    EXPECT_TRUE(filter.fullyContains(1200, 1800));
    
    // Edge [500, 2000] is NOT fully contained (starts before range)
    EXPECT_FALSE(filter.fullyContains(500, 2000));
    
    // Edge [2000, 4000] is NOT fully contained (ends after range)
    EXPECT_FALSE(filter.fullyContains(2000, 4000));
    
    // Edge [1000, 3000] is fully contained (exact bounds)
    EXPECT_TRUE(filter.fullyContains(1000, 3000));
}

TEST_F(TimeRangeQueryTest, GetEdgesInTimeRange_Overlap) {
    createTemporalEdges();

    // Query range [1200, 1800]: should include e1 and e2 (overlap)
    auto [st, edges] = graphIdx->getEdgesInTimeRange(1200, 1800, false);
    ASSERT_TRUE(st.ok) << st.message;
    
    // e1 [1000-2000] overlaps, e2 [1500-3000] overlaps, e3 [2500-4000] no overlap, e4 always valid
    EXPECT_GE(edges.size(), 2);
    
    std::vector<std::string> edgeIds;
    for (const auto& e : edges) {
        edgeIds.push_back(e.edgeId);
    }
    
    EXPECT_TRUE(std::find(edgeIds.begin(), edgeIds.end(), "e1") != edgeIds.end());
    EXPECT_TRUE(std::find(edgeIds.begin(), edgeIds.end(), "e2") != edgeIds.end());
}

TEST_F(TimeRangeQueryTest, GetEdgesInTimeRange_FullContainment) {
    createTemporalEdges();

    // Query range [1000, 3000] with full containment
    auto [st, edges] = graphIdx->getEdgesInTimeRange(1000, 3000, true);
    ASSERT_TRUE(st.ok) << st.message;
    
    std::vector<std::string> edgeIds;
    for (const auto& e : edges) {
        edgeIds.push_back(e.edgeId);
    }
    
    // e1 [1000-2000] fully contained, e2 [1500-3000] fully contained
    // e3 [2500-4000] NOT fully contained (ends after range)
    EXPECT_TRUE(std::find(edgeIds.begin(), edgeIds.end(), "e1") != edgeIds.end());
    EXPECT_TRUE(std::find(edgeIds.begin(), edgeIds.end(), "e2") != edgeIds.end());
    EXPECT_TRUE(std::find(edgeIds.begin(), edgeIds.end(), "e3") == edgeIds.end());
}

TEST_F(TimeRangeQueryTest, GetOutEdgesInTimeRange) {
    createTemporalEdges();

    // Query outgoing edges from A in range [1200, 2500]
    auto [st, edges] = graphIdx->getOutEdgesInTimeRange("A", 1200, 2500, false);
    ASSERT_TRUE(st.ok) << st.message;
    
    // A has e1 [1000-2000] and e2 [1500-3000], both overlap with [1200, 2500]
    EXPECT_EQ(edges.size(), 2);
    
    for (const auto& e : edges) {
        EXPECT_EQ(e.fromPk, "A");
        EXPECT_TRUE(e.edgeId == "e1" || e.edgeId == "e2");
    }
}

TEST_F(TimeRangeQueryTest, GetOutEdgesInTimeRange_NoMatch) {
    createTemporalEdges();

    // Query outgoing edges from A in range [5000, 6000] (far future)
    auto [st, edges] = graphIdx->getOutEdgesInTimeRange("A", 5000, 6000, false);
    ASSERT_TRUE(st.ok) << st.message;
    
    // No edges from A are valid in this range
    EXPECT_EQ(edges.size(), 0);
}

TEST_F(TimeRangeQueryTest, UnboundedEdges_AlwaysIncluded) {
    createTemporalEdges();

    // Query range [100, 200] (very early time)
    auto [st, edges] = graphIdx->getEdgesInTimeRange(100, 200, false);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Only e4 (unbounded) should match (no valid_from/valid_to)
    bool hasE4 = false;
    for (const auto& e : edges) {
        if (e.edgeId == "e4") {
            hasE4 = true;
            EXPECT_FALSE(e.valid_from.has_value());
            EXPECT_FALSE(e.valid_to.has_value());
        }
    }
    EXPECT_TRUE(hasE4);
}

TEST_F(TimeRangeQueryTest, EdgeInfo_ContainsTemporalData) {
    createTemporalEdges();

    auto [st, edges] = graphIdx->getEdgesInTimeRange(1000, 2000, false);
    ASSERT_TRUE(st.ok);
    ASSERT_GT(edges.size(), 0);

    // Verify EdgeInfo structure contains temporal data
    for (const auto& e : edges) {
        EXPECT_FALSE(e.edgeId.empty());
        EXPECT_FALSE(e.fromPk.empty());
        EXPECT_FALSE(e.toPk.empty());
        // valid_from/valid_to are optional - some edges may not have them
    }
}
