#include <gtest/gtest.h>
#include "timeseries/tsstore.h"
#include "timeseries/retention.h"
#include "storage/rocksdb_wrapper.h"
#include <filesystem>
#include <chrono>

namespace fs = std::filesystem;
using namespace themis;

class RetentionTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_path_ = "./data/themis_retention_test";
        fs::remove_all(db_path_);
        RocksDBWrapper::Config cfg; cfg.db_path = db_path_;
        db_ = std::make_unique<RocksDBWrapper>(cfg);
        ASSERT_TRUE(db_->open());
        store_ = std::make_unique<TSStore>(db_->getRawDB());
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        // Insert two metrics with different ages
        TSStore::DataPoint p1{"cpu","srv1", now - 60000, 0.5};
        TSStore::DataPoint p2{"cpu","srv1", now - 30000, 0.7};
        TSStore::DataPoint p3{"mem","srv1", now - 120000, 0.8};
        ASSERT_TRUE(store_->putDataPoint(p1).ok);
        ASSERT_TRUE(store_->putDataPoint(p2).ok);
        ASSERT_TRUE(store_->putDataPoint(p3).ok);
    }
    void TearDown() override {
        store_.reset(); db_.reset(); fs::remove_all(db_path_);
    }
    std::string db_path_;
    std::unique_ptr<RocksDBWrapper> db_;
    std::unique_ptr<TSStore> store_;
};

TEST_F(RetentionTest, ApplyPerMetricRetention) {
    RetentionPolicy pol; pol.per_metric["cpu"] = std::chrono::seconds(45);
    pol.per_metric["mem"] = std::chrono::seconds(90);
    RetentionManager rm(store_.get(), pol);
    size_t deleted = rm.apply();
    // cpu: delete entries older than 45s → 1; mem: older than 90s → 1
    EXPECT_GE(deleted, 2u);
}
