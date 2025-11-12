#include <gtest/gtest.h>
#include "index/graph_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include <filesystem>

using namespace themis;

class TemporalAggregationPropertyTest : public ::testing::Test {
protected:
    void SetUp() override {
        testDbPath_ = "test_temporal_aggregation_prop_db";
        std::filesystem::remove_all(testDbPath_);

        themis::RocksDBWrapper::Config config;
        config.db_path = testDbPath_;
        config.memtable_size_mb = 64;
        config.block_cache_size_mb = 256;

        db_ = std::make_unique<RocksDBWrapper>(config);
        ASSERT_TRUE(db_->open());

        graphIdx_ = std::make_unique<GraphIndexManager>(*db_);

        createTemporalTestGraph();
    }

    void TearDown() override {
        graphIdx_.reset();
        db_->close();
        db_.reset();
        std::filesystem::remove_all(testDbPath_);
    }

    void createTemporalTestGraph() {
        // nodes
        BaseEntity n1("n1"); n1.setField("id", std::string("n1")); db_->put("entity:n1", n1.serialize());
        BaseEntity n2("n2"); n2.setField("id", std::string("n2")); db_->put("entity:n2", n2.serialize());

        // e1: 1000-2000 cost=10 type=A
        BaseEntity e1("e1");
        e1.setField("id", std::string("e1"));
        e1.setField("_from", std::string("n1"));
        e1.setField("_to", std::string("n2"));
        e1.setField("valid_from", static_cast<int64_t>(1000));
        e1.setField("valid_to", static_cast<int64_t>(2000));
        e1.setField("cost", 10.0);
        e1.setField("_type", std::string("A"));
        graphIdx_->addEdge(e1);

        // e2: 1500-3000 cost=20 type=B
        BaseEntity e2("e2");
        e2.setField("id", std::string("e2"));
        e2.setField("_from", std::string("n2"));
        e2.setField("_to", std::string("n1"));
        e2.setField("valid_from", static_cast<int64_t>(1500));
        e2.setField("valid_to", static_cast<int64_t>(3000));
        e2.setField("cost", 20.0);
        e2.setField("_type", std::string("B"));
        graphIdx_->addEdge(e2);

        // e3: 1200-1800 cost=30 type=A
        BaseEntity e3("e3");
        e3.setField("id", std::string("e3"));
        e3.setField("_from", std::string("n1"));
        e3.setField("_to", std::string("n2"));
        e3.setField("valid_from", static_cast<int64_t>(1200));
        e3.setField("valid_to", static_cast<int64_t>(1800));
        e3.setField("cost", 30.0);
        e3.setField("_type", std::string("A"));
        graphIdx_->addEdge(e3);

        // e4: 1100-1150 no cost
        BaseEntity e4("e4");
        e4.setField("id", std::string("e4"));
        e4.setField("_from", std::string("n2"));
        e4.setField("_to", std::string("n1"));
        e4.setField("valid_from", static_cast<int64_t>(1100));
        e4.setField("valid_to", static_cast<int64_t>(1150));
        e4.setField("_type", std::string("B"));
        graphIdx_->addEdge(e4);

        // e5: unbounded cost=40 type=A
        BaseEntity e5("e5");
        e5.setField("id", std::string("e5"));
        e5.setField("_from", std::string("n1"));
        e5.setField("_to", std::string("n2"));
        e5.setField("cost", 40.0);
        e5.setField("_type", std::string("A"));
        graphIdx_->addEdge(e5);
    }

    std::string testDbPath_;
    std::unique_ptr<RocksDBWrapper> db_;
    std::unique_ptr<GraphIndexManager> graphIdx_;
};

TEST_F(TemporalAggregationPropertyTest, SumAvgMinMaxNoType) {
    // range [1000,2000] overlaps e1,e2,e3,e4,e5 (e4 has no cost)
    auto [st, res] = graphIdx_->aggregateEdgePropertyInTimeRange("cost", GraphIndexManager::Aggregation::SUM, 1000, 2000, false, std::nullopt);
    ASSERT_TRUE(st.ok) << st.message;
    EXPECT_EQ(res.count, 4u); // four edges with numeric cost
    EXPECT_DOUBLE_EQ(res.value, 100.0);

    auto [st2, res2] = graphIdx_->aggregateEdgePropertyInTimeRange("cost", GraphIndexManager::Aggregation::AVG, 1000, 2000, false, std::nullopt);
    ASSERT_TRUE(st2.ok) << st2.message;
    EXPECT_EQ(res2.count, 4u);
    EXPECT_DOUBLE_EQ(res2.value, 25.0);

    auto [st3, rmin] = graphIdx_->aggregateEdgePropertyInTimeRange("cost", GraphIndexManager::Aggregation::MIN, 1000, 2000, false, std::nullopt);
    ASSERT_TRUE(st3.ok) << st3.message;
    EXPECT_EQ(rmin.count, 4u);
    EXPECT_DOUBLE_EQ(rmin.value, 10.0);

    auto [st4, rmax] = graphIdx_->aggregateEdgePropertyInTimeRange("cost", GraphIndexManager::Aggregation::MAX, 1000, 2000, false, std::nullopt);
    ASSERT_TRUE(st4.ok) << st4.message;
    EXPECT_EQ(rmax.count, 4u);
    EXPECT_DOUBLE_EQ(rmax.value, 40.0);
}

TEST_F(TemporalAggregationPropertyTest, CountAllEdges) {
    auto [st, res] = graphIdx_->aggregateEdgePropertyInTimeRange("ignored", GraphIndexManager::Aggregation::COUNT, 1000, 2000, false, std::nullopt);
    ASSERT_TRUE(st.ok) << st.message;
    EXPECT_EQ(res.count, 5u);
}

TEST_F(TemporalAggregationPropertyTest, TypeFilterSum) {
    auto [st, res] = graphIdx_->aggregateEdgePropertyInTimeRange("cost", GraphIndexManager::Aggregation::SUM, 1000, 2000, false, std::optional<std::string_view>(std::string_view("A")));
    ASSERT_TRUE(st.ok) << st.message;
    EXPECT_EQ(res.count, 3u);
    EXPECT_DOUBLE_EQ(res.value, 80.0);
}

TEST_F(TemporalAggregationPropertyTest, NonexistentProperty) {
    auto [st, res] = graphIdx_->aggregateEdgePropertyInTimeRange("no_such", GraphIndexManager::Aggregation::SUM, 1000, 2000, false, std::nullopt);
    ASSERT_TRUE(st.ok) << st.message;
    EXPECT_EQ(res.count, 0u);
    EXPECT_DOUBLE_EQ(res.value, 0.0);
}
