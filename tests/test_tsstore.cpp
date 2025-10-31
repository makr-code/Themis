#include <gtest/gtest.h>
#include "timeseries/tsstore.h"
#include "storage/rocksdb_wrapper.h"
#include <filesystem>
#include <chrono>
#include <thread>

namespace fs = std::filesystem;

class TSStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_db_path_ = "./data/themis_timeseries_test";
        fs::remove_all(test_db_path_);
        
        themis::RocksDBWrapper::Config config;
        config.db_path = test_db_path_;
        config.memtable_size_mb = 64;
        config.block_cache_size_mb = 256;
        config.max_background_jobs = 2;
        config.compression_default = "lz4";
        config.compression_bottommost = "zstd";
        
        db_ = std::make_unique<themis::RocksDBWrapper>(config);
        ASSERT_TRUE(db_->open());
        ts_store_ = std::make_unique<themis::TSStore>(db_->getRawDB());
        
        // Setup test timestamps
        base_time_ = std::chrono::system_clock::now().time_since_epoch();
        t0_ = std::chrono::duration_cast<std::chrono::milliseconds>(base_time_).count();
        t1_ = t0_ + 1000;   // +1 second
        t2_ = t0_ + 2000;   // +2 seconds
        t3_ = t0_ + 3000;   // +3 seconds
        t4_ = t0_ + 4000;   // +4 seconds
        t5_ = t0_ + 5000;   // +5 seconds
    }

    void TearDown() override {
        ts_store_.reset();
        db_.reset();
        fs::remove_all(test_db_path_);
    }
    
    // Helper: Create data point
    themis::TSStore::DataPoint createDataPoint(
        const std::string& metric,
        const std::string& entity,
        int64_t timestamp_ms,
        double value,
        nlohmann::json tags = nlohmann::json::object()
    ) {
        themis::TSStore::DataPoint point;
        point.metric = metric;
        point.entity = entity;
        point.timestamp_ms = timestamp_ms;
        point.value = value;
        point.tags = tags;
        return point;
    }

    std::string test_db_path_;
    std::unique_ptr<themis::RocksDBWrapper> db_;
    std::unique_ptr<themis::TSStore> ts_store_;
    
    std::chrono::system_clock::duration base_time_;
    int64_t t0_, t1_, t2_, t3_, t4_, t5_;
};

// ===== Basic Operations =====

TEST_F(TSStoreTest, PutDataPoint_SinglePoint_Success) {
    auto point = createDataPoint("cpu_usage", "server01", t0_, 75.5);
    
    auto status = ts_store_->putDataPoint(point);
    ASSERT_TRUE(status.ok) << status.message;
}

TEST_F(TSStoreTest, PutDataPoint_EmptyMetric_ReturnsError) {
    auto point = createDataPoint("", "server01", t0_, 75.5);
    
    auto status = ts_store_->putDataPoint(point);
    EXPECT_FALSE(status.ok);
    EXPECT_TRUE(status.message.find("Metric") != std::string::npos);
}

TEST_F(TSStoreTest, PutDataPoint_EmptyEntity_ReturnsError) {
    auto point = createDataPoint("cpu_usage", "", t0_, 75.5);
    
    auto status = ts_store_->putDataPoint(point);
    EXPECT_FALSE(status.ok);
    EXPECT_TRUE(status.message.find("Entity") != std::string::npos);
}

TEST_F(TSStoreTest, PutDataPoints_BatchWrite_Success) {
    std::vector<themis::TSStore::DataPoint> points = {
        createDataPoint("cpu_usage", "server01", t0_, 75.5),
        createDataPoint("cpu_usage", "server01", t1_, 80.2),
        createDataPoint("cpu_usage", "server01", t2_, 78.9)
    };
    
    auto status = ts_store_->putDataPoints(points);
    ASSERT_TRUE(status.ok) << status.message;
}

// ===== Query Tests =====

TEST_F(TSStoreTest, Query_SinglePoint_ReturnsCorrectData) {
    auto point = createDataPoint("cpu_usage", "server01", t0_, 75.5);
    ASSERT_TRUE(ts_store_->putDataPoint(point).ok);
    
    themis::TSStore::QueryOptions opts;
    opts.metric = "cpu_usage";
    opts.entity = "server01";
    opts.from_timestamp_ms = t0_;
    opts.to_timestamp_ms = t0_;
    
    auto [status, results] = ts_store_->query(opts);
    ASSERT_TRUE(status.ok) << status.message;
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].metric, "cpu_usage");
    EXPECT_EQ(results[0].entity, "server01");
    EXPECT_EQ(results[0].timestamp_ms, t0_);
    EXPECT_DOUBLE_EQ(results[0].value, 75.5);
}

TEST_F(TSStoreTest, Query_TimeRange_ReturnsFilteredPoints) {
    std::vector<themis::TSStore::DataPoint> points = {
        createDataPoint("cpu_usage", "server01", t0_, 70.0),
        createDataPoint("cpu_usage", "server01", t1_, 75.0),
        createDataPoint("cpu_usage", "server01", t2_, 80.0),
        createDataPoint("cpu_usage", "server01", t3_, 85.0),
        createDataPoint("cpu_usage", "server01", t4_, 90.0)
    };
    ASSERT_TRUE(ts_store_->putDataPoints(points).ok);
    
    themis::TSStore::QueryOptions opts;
    opts.metric = "cpu_usage";
    opts.entity = "server01";
    opts.from_timestamp_ms = t1_;
    opts.to_timestamp_ms = t3_;
    
    auto [status, results] = ts_store_->query(opts);
    ASSERT_TRUE(status.ok);
    ASSERT_EQ(results.size(), 3u); // t1, t2, t3
    EXPECT_DOUBLE_EQ(results[0].value, 75.0);
    EXPECT_DOUBLE_EQ(results[1].value, 80.0);
    EXPECT_DOUBLE_EQ(results[2].value, 85.0);
}

TEST_F(TSStoreTest, Query_MultipleEntities_ReturnsAllWhenNoEntityFilter) {
    std::vector<themis::TSStore::DataPoint> points = {
        createDataPoint("cpu_usage", "server01", t0_, 70.0),
        createDataPoint("cpu_usage", "server02", t0_, 65.0),
        createDataPoint("cpu_usage", "server03", t0_, 80.0)
    };
    ASSERT_TRUE(ts_store_->putDataPoints(points).ok);
    
    themis::TSStore::QueryOptions opts;
    opts.metric = "cpu_usage";
    // No entity filter = query all entities
    opts.from_timestamp_ms = t0_;
    opts.to_timestamp_ms = t0_;
    
    auto [status, results] = ts_store_->query(opts);
    ASSERT_TRUE(status.ok);
    EXPECT_EQ(results.size(), 3u);
}

TEST_F(TSStoreTest, Query_WithLimit_ReturnsLimitedResults) {
    std::vector<themis::TSStore::DataPoint> points;
    for (int i = 0; i < 100; i++) {
        points.push_back(createDataPoint("cpu_usage", "server01", t0_ + i * 100, 50.0 + i));
    }
    ASSERT_TRUE(ts_store_->putDataPoints(points).ok);
    
    themis::TSStore::QueryOptions opts;
    opts.metric = "cpu_usage";
    opts.entity = "server01";
    opts.from_timestamp_ms = t0_;
    opts.to_timestamp_ms = t0_ + 20000;
    opts.limit = 10;
    
    auto [status, results] = ts_store_->query(opts);
    ASSERT_TRUE(status.ok);
    EXPECT_EQ(results.size(), 10u);
}

TEST_F(TSStoreTest, Query_WithTagFilter_ReturnsOnlyMatchingPoints) {
    nlohmann::json tags_prod = {{"env", "prod"}, {"region", "us-east"}};
    nlohmann::json tags_dev = {{"env", "dev"}, {"region", "us-east"}};
    
    std::vector<themis::TSStore::DataPoint> points = {
        createDataPoint("cpu_usage", "server01", t0_, 70.0, tags_prod),
        createDataPoint("cpu_usage", "server02", t0_, 65.0, tags_dev),
        createDataPoint("cpu_usage", "server03", t0_, 80.0, tags_prod)
    };
    ASSERT_TRUE(ts_store_->putDataPoints(points).ok);
    
    themis::TSStore::QueryOptions opts;
    opts.metric = "cpu_usage";
    opts.from_timestamp_ms = t0_;
    opts.to_timestamp_ms = t0_;
    opts.tag_filter = {{"env", "prod"}};
    
    auto [status, results] = ts_store_->query(opts);
    ASSERT_TRUE(status.ok);
    EXPECT_EQ(results.size(), 2u); // Only server01 and server03
    for (const auto& result : results) {
        EXPECT_EQ(result.tags["env"], "prod");
    }
}

TEST_F(TSStoreTest, Query_EmptyMetric_ReturnsError) {
    themis::TSStore::QueryOptions opts;
    opts.metric = ""; // Empty metric
    
    auto [status, results] = ts_store_->query(opts);
    EXPECT_FALSE(status.ok);
    EXPECT_TRUE(status.message.find("Metric") != std::string::npos);
}

// ===== Aggregation Tests =====

TEST_F(TSStoreTest, Aggregate_ComputesCorrectStatistics) {
    std::vector<themis::TSStore::DataPoint> points = {
        createDataPoint("cpu_usage", "server01", t0_, 70.0),
        createDataPoint("cpu_usage", "server01", t1_, 80.0),
        createDataPoint("cpu_usage", "server01", t2_, 90.0),
        createDataPoint("cpu_usage", "server01", t3_, 60.0),
        createDataPoint("cpu_usage", "server01", t4_, 85.0)
    };
    ASSERT_TRUE(ts_store_->putDataPoints(points).ok);
    
    themis::TSStore::QueryOptions opts;
    opts.metric = "cpu_usage";
    opts.entity = "server01";
    opts.from_timestamp_ms = t0_;
    opts.to_timestamp_ms = t4_;
    
    auto [status, agg] = ts_store_->aggregate(opts);
    ASSERT_TRUE(status.ok);
    
    EXPECT_EQ(agg.count, 5u);
    EXPECT_DOUBLE_EQ(agg.min, 60.0);
    EXPECT_DOUBLE_EQ(agg.max, 90.0);
    EXPECT_DOUBLE_EQ(agg.sum, 385.0); // 70+80+90+60+85
    EXPECT_DOUBLE_EQ(agg.avg, 77.0);  // 385/5
    EXPECT_EQ(agg.first_timestamp_ms, t0_);
    EXPECT_EQ(agg.last_timestamp_ms, t4_);
}

TEST_F(TSStoreTest, Aggregate_EmptyResult_ReturnsZeroStats) {
    themis::TSStore::QueryOptions opts;
    opts.metric = "nonexistent_metric";
    opts.from_timestamp_ms = t0_;
    opts.to_timestamp_ms = t4_;
    
    auto [status, agg] = ts_store_->aggregate(opts);
    ASSERT_TRUE(status.ok);
    EXPECT_EQ(agg.count, 0u);
}

TEST_F(TSStoreTest, Aggregate_SinglePoint_ReturnsCorrectStats) {
    auto point = createDataPoint("cpu_usage", "server01", t0_, 75.5);
    ASSERT_TRUE(ts_store_->putDataPoint(point).ok);
    
    themis::TSStore::QueryOptions opts;
    opts.metric = "cpu_usage";
    opts.entity = "server01";
    opts.from_timestamp_ms = t0_;
    opts.to_timestamp_ms = t0_;
    
    auto [status, agg] = ts_store_->aggregate(opts);
    ASSERT_TRUE(status.ok);
    
    EXPECT_EQ(agg.count, 1u);
    EXPECT_DOUBLE_EQ(agg.min, 75.5);
    EXPECT_DOUBLE_EQ(agg.max, 75.5);
    EXPECT_DOUBLE_EQ(agg.avg, 75.5);
    EXPECT_DOUBLE_EQ(agg.sum, 75.5);
}

// ===== Performance Tests =====

TEST_F(TSStoreTest, Performance_Query1000Points_UnderThreshold) {
    // Insert 1000 data points
    std::vector<themis::TSStore::DataPoint> points;
    for (int i = 0; i < 1000; i++) {
        points.push_back(createDataPoint("cpu_usage", "server01", t0_ + i, 50.0 + i * 0.01));
    }
    ASSERT_TRUE(ts_store_->putDataPoints(points).ok);
    
    themis::TSStore::QueryOptions opts;
    opts.metric = "cpu_usage";
    opts.entity = "server01";
    opts.from_timestamp_ms = t0_;
    opts.to_timestamp_ms = t0_ + 1000;
    
    auto start = std::chrono::high_resolution_clock::now();
    auto [status, results] = ts_store_->query(opts);
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    ASSERT_TRUE(status.ok);
    EXPECT_EQ(results.size(), 1000u);
    EXPECT_LT(duration_ms, 100); // Should be < 100ms (target: <10ms, relaxed for CI)
    
    std::cout << "Query 1000 points took: " << duration_ms << "ms" << std::endl;
}

TEST_F(TSStoreTest, Performance_BatchWrite1000Points_Fast) {
    std::vector<themis::TSStore::DataPoint> points;
    for (int i = 0; i < 1000; i++) {
        points.push_back(createDataPoint("cpu_usage", "server01", t0_ + i, 50.0 + i));
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    auto status = ts_store_->putDataPoints(points);
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    ASSERT_TRUE(status.ok);
    EXPECT_LT(duration_ms, 500); // Should be < 500ms
    
    std::cout << "Batch write 1000 points took: " << duration_ms << "ms" << std::endl;
}

// ===== Stats Tests =====

TEST_F(TSStoreTest, GetStats_ReturnsAccurateMetrics) {
    std::vector<themis::TSStore::DataPoint> points = {
        createDataPoint("cpu_usage", "server01", t0_, 70.0),
        createDataPoint("cpu_usage", "server01", t5_, 80.0),
        createDataPoint("memory_usage", "server01", t2_, 90.0)
    };
    ASSERT_TRUE(ts_store_->putDataPoints(points).ok);
    
    auto stats = ts_store_->getStats();
    
    EXPECT_EQ(stats.total_data_points, 3u);
    EXPECT_EQ(stats.total_metrics, 2u); // cpu_usage, memory_usage
    EXPECT_GT(stats.total_size_bytes, 0u);
    EXPECT_EQ(stats.oldest_timestamp_ms, t0_);
    EXPECT_EQ(stats.newest_timestamp_ms, t5_);
}

TEST_F(TSStoreTest, GetStats_EmptyStore_ReturnsZeros) {
    auto stats = ts_store_->getStats();
    
    EXPECT_EQ(stats.total_data_points, 0u);
    EXPECT_EQ(stats.total_metrics, 0u);
    EXPECT_EQ(stats.total_size_bytes, 0u);
}

// ===== Deletion Tests =====

TEST_F(TSStoreTest, DeleteOldData_RemovesOldPoints) {
    std::vector<themis::TSStore::DataPoint> points = {
        createDataPoint("cpu_usage", "server01", t0_, 70.0),
        createDataPoint("cpu_usage", "server01", t1_, 75.0),
        createDataPoint("cpu_usage", "server01", t2_, 80.0),
        createDataPoint("cpu_usage", "server01", t3_, 85.0),
        createDataPoint("cpu_usage", "server01", t4_, 90.0)
    };
    ASSERT_TRUE(ts_store_->putDataPoints(points).ok);
    
    // Delete points before t2
    size_t deleted = ts_store_->deleteOldData(t2_);
    EXPECT_EQ(deleted, 2u); // t0, t1
    
    // Verify remaining points
    themis::TSStore::QueryOptions opts;
    opts.metric = "cpu_usage";
    opts.entity = "server01";
    opts.from_timestamp_ms = 0;
    opts.to_timestamp_ms = INT64_MAX;
    
    auto [status, results] = ts_store_->query(opts);
    ASSERT_TRUE(status.ok);
    EXPECT_EQ(results.size(), 3u); // t2, t3, t4
    EXPECT_DOUBLE_EQ(results[0].value, 80.0);
}

TEST_F(TSStoreTest, DeleteMetric_RemovesAllPointsForMetric) {
    std::vector<themis::TSStore::DataPoint> points = {
        createDataPoint("cpu_usage", "server01", t0_, 70.0),
        createDataPoint("cpu_usage", "server02", t0_, 75.0),
        createDataPoint("memory_usage", "server01", t0_, 80.0)
    };
    ASSERT_TRUE(ts_store_->putDataPoints(points).ok);
    
    auto status = ts_store_->deleteMetric("cpu_usage");
    ASSERT_TRUE(status.ok);
    
    // Verify cpu_usage is gone
    themis::TSStore::QueryOptions opts1;
    opts1.metric = "cpu_usage";
    opts1.from_timestamp_ms = 0;
    opts1.to_timestamp_ms = INT64_MAX;
    
    auto [status1, results1] = ts_store_->query(opts1);
    EXPECT_EQ(results1.size(), 0u);
    
    // Verify memory_usage still exists
    themis::TSStore::QueryOptions opts2;
    opts2.metric = "memory_usage";
    opts2.from_timestamp_ms = 0;
    opts2.to_timestamp_ms = INT64_MAX;
    
    auto [status2, results2] = ts_store_->query(opts2);
    EXPECT_EQ(results2.size(), 1u);
}

TEST_F(TSStoreTest, Clear_RemovesAllData) {
    std::vector<themis::TSStore::DataPoint> points = {
        createDataPoint("cpu_usage", "server01", t0_, 70.0),
        createDataPoint("memory_usage", "server01", t0_, 80.0)
    };
    ASSERT_TRUE(ts_store_->putDataPoints(points).ok);
    
    ts_store_->clear();
    
    auto stats = ts_store_->getStats();
    EXPECT_EQ(stats.total_data_points, 0u);
    EXPECT_EQ(stats.total_metrics, 0u);
}

// ===== Real-World Scenarios =====

TEST_F(TSStoreTest, RealWorld_MonitoringPipeline) {
    // Simulate monitoring pipeline: ingest, query recent, aggregate
    
    // 1. Ingest metrics from multiple servers
    std::vector<themis::TSStore::DataPoint> points;
    for (int server = 1; server <= 3; server++) {
        for (int i = 0; i < 60; i++) { // 60 seconds
            std::string entity = "server0" + std::to_string(server);
            points.push_back(createDataPoint("cpu_usage", entity, t0_ + i * 1000, 50.0 + i + server * 5));
        }
    }
    ASSERT_TRUE(ts_store_->putDataPoints(points).ok);
    
    // 2. Query recent data (last 10 seconds) for server01
    themis::TSStore::QueryOptions recent_opts;
    recent_opts.metric = "cpu_usage";
    recent_opts.entity = "server01";
    recent_opts.from_timestamp_ms = t0_ + 50000;
    recent_opts.to_timestamp_ms = t0_ + 60000;
    
    auto [st1, recent] = ts_store_->query(recent_opts);
    ASSERT_TRUE(st1.ok);
    EXPECT_EQ(recent.size(), 10u);
    
    // 3. Aggregate all servers over 1 minute
    themis::TSStore::QueryOptions agg_opts;
    agg_opts.metric = "cpu_usage";
    agg_opts.from_timestamp_ms = t0_;
    agg_opts.to_timestamp_ms = t0_ + 60000;
    
    auto [st2, agg] = ts_store_->aggregate(agg_opts);
    ASSERT_TRUE(st2.ok);
    EXPECT_EQ(agg.count, 180u); // 60 points * 3 servers
    EXPECT_GT(agg.max, agg.min);
}

TEST_F(TSStoreTest, RealWorld_AlertingWithThresholds) {
    // Simulate alerting: detect when CPU > 90% for 3 consecutive readings
    
    std::vector<themis::TSStore::DataPoint> points = {
        createDataPoint("cpu_usage", "server01", t0_, 85.0),
        createDataPoint("cpu_usage", "server01", t1_, 92.0), // Over threshold
        createDataPoint("cpu_usage", "server01", t2_, 94.0), // Over threshold
        createDataPoint("cpu_usage", "server01", t3_, 95.0), // Over threshold (alert!)
        createDataPoint("cpu_usage", "server01", t4_, 80.0)
    };
    ASSERT_TRUE(ts_store_->putDataPoints(points).ok);
    
    themis::TSStore::QueryOptions opts;
    opts.metric = "cpu_usage";
    opts.entity = "server01";
    opts.from_timestamp_ms = t0_;
    opts.to_timestamp_ms = t4_;
    
    auto [status, results] = ts_store_->query(opts);
    ASSERT_TRUE(status.ok);
    
    // Check for 3 consecutive values > 90
    int consecutive_over_threshold = 0;
    bool alert_triggered = false;
    
    for (const auto& point : results) {
        if (point.value > 90.0) {
            consecutive_over_threshold++;
            if (consecutive_over_threshold >= 3) {
                alert_triggered = true;
                break;
            }
        } else {
            consecutive_over_threshold = 0;
        }
    }
    
    EXPECT_TRUE(alert_triggered);
}
