#include <gtest/gtest.h>
#include "timeseries/tsstore.h"
#include "timeseries/continuous_agg.h"
#include "storage/rocksdb_wrapper.h"
#include <filesystem>
#include <chrono>

using namespace themis;
namespace fs = std::filesystem;

class ContinuousAggTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_path_ = "./data/themis_contagg_test";
        fs::remove_all(db_path_);
        RocksDBWrapper::Config cfg; cfg.db_path = db_path_;
        db_ = std::make_unique<RocksDBWrapper>(cfg);
        ASSERT_TRUE(db_->open());
        store_ = std::make_unique<TSStore>(db_->getRawDB());
        base_ = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
    void TearDown() override {
        store_.reset(); db_.reset(); fs::remove_all(db_path_);
    }
    std::string db_path_;
    std::unique_ptr<RocksDBWrapper> db_;
    std::unique_ptr<TSStore> store_;
    int64_t base_;
};

TEST_F(ContinuousAggTest, RefreshWindowedAvg) {
    // Insert 2 minutes of points every 10s
    for (int i = 0; i < 12; ++i) {
        TSStore::DataPoint p{"temp","sensorA", base_ + i*10000, 20.0 + i};
        ASSERT_TRUE(store_->putDataPoint(p).ok);
    }

    ContinuousAggregateManager mgr(store_.get());
    AggConfig cfg; cfg.metric = "temp"; cfg.entity = std::string("sensorA"); cfg.window.size = std::chrono::minutes(1);
    mgr.refresh(cfg, base_, base_ + 119000);

    // Query derived metric
    TSStore::QueryOptions q; q.metric = ContinuousAggregateManager::derivedMetricName("temp", std::chrono::minutes(1)); q.entity = std::string("sensorA"); q.from_timestamp_ms = base_; q.to_timestamp_ms = base_ + 120000; q.limit = 10;
    auto [st, pts] = store_->query(q);
    ASSERT_TRUE(st.ok);
    ASSERT_EQ(pts.size(), 2u);
    // First minute values: 20..25 (6 points) avg = 22.5
    EXPECT_NEAR(pts[0].value, 22.5, 1e-9);
    // Metadata contains count
    ASSERT_TRUE(pts[0].metadata.contains("count"));
    EXPECT_EQ(pts[0].metadata["count"].get<size_t>(), 6u);
}
