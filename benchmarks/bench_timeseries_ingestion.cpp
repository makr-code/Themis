// Benchmark: Timeseries Ingestion Performance
// Measures time-series write throughput and compression efficiency

#include "timeseries/timeseries.h"
#include "timeseries/gorilla.h"
#include "storage/rocksdb_wrapper.h"
#include <benchmark/benchmark.h>
#include <filesystem>
#include <random>
#include <cmath>

using namespace themis;

// ============================================================================
// Test Setup
// ============================================================================

class TimeseriesBenchmarkFixture : public benchmark::Fixture {
public:
    void SetUp(const ::benchmark::State& /*state*/) override {
        // Clean up any existing test database
        test_db_path_ = "./data/bench_timeseries_tmp";
        if (std::filesystem::exists(test_db_path_)) {
            std::filesystem::remove_all(test_db_path_);
        }
        
        // Create RocksDB wrapper
        RocksDBWrapper::Config config;
        config.db_path = test_db_path_;
        config.memtable_size_mb = 256;
        config.block_cache_size_mb = 512;
        config.write_buffer_size = 256 * 1024 * 1024;
        
        db_ = std::make_unique<RocksDBWrapper>(config);
        if (!db_->open()) {
            throw std::runtime_error("Failed to open database");
        }
        
        // Create timeseries store
        ts_store_ = std::make_unique<TimeSeriesStore>(db_->getDB(), nullptr);
    }
    
    void TearDown(const ::benchmark::State& /*state*/) override {
        ts_store_.reset();
        db_->close();
        db_.reset();
        
        // Clean up test database
        if (std::filesystem::exists(test_db_path_)) {
            std::filesystem::remove_all(test_db_path_);
        }
    }
    
protected:
    std::string test_db_path_;
    std::unique_ptr<RocksDBWrapper> db_;
    std::unique_ptr<TimeSeriesStore> ts_store_;
};

// ============================================================================
// Benchmark: Raw Data Ingestion
// ============================================================================

BENCHMARK_DEFINE_F(TimeseriesBenchmarkFixture, RawDataIngestion)(benchmark::State& state) {
    const std::string metric = "cpu_usage";
    const std::string entity = "server_1";
    
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> value_dist(0.0, 100.0);
    
    int64_t timestamp = 1700000000000; // Start timestamp
    size_t points_written = 0;
    
    for (auto _ : state) {
        TimeSeriesStore::DataPoint point;
        point.timestamp_ms = timestamp++;
        point.value = value_dist(rng);
        
        bool success = ts_store_->put(metric, entity, point);
        benchmark::DoNotOptimize(success);
        
        if (!success) {
            state.SkipWithError("Failed to write data point");
        }
        
        points_written++;
    }
    
    state.SetItemsProcessed(state.iterations());
    state.counters["points_per_sec"] = benchmark::Counter(
        state.iterations(), benchmark::Counter::kIsRate);
    state.counters["total_points"] = points_written;
}

BENCHMARK_REGISTER_F(TimeseriesBenchmarkFixture, RawDataIngestion)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->Unit(benchmark::kMicrosecond);

// ============================================================================
// Benchmark: Batch Ingestion
// ============================================================================

BENCHMARK_DEFINE_F(TimeseriesBenchmarkFixture, BatchIngestion)(benchmark::State& state) {
    const int batch_size = state.range(0);
    const std::string metric = "memory_usage";
    const std::string entity = "server_2";
    
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> value_dist(0.0, 16384.0); // MB
    
    int64_t timestamp = 1700000000000;
    
    for (auto _ : state) {
        std::vector<TimeSeriesStore::DataPoint> batch;
        batch.reserve(batch_size);
        
        for (int i = 0; i < batch_size; i++) {
            TimeSeriesStore::DataPoint point;
            point.timestamp_ms = timestamp++;
            point.value = value_dist(rng);
            batch.push_back(point);
        }
        
        // Write batch
        for (const auto& point : batch) {
            ts_store_->put(metric, entity, point);
        }
    }
    
    state.SetItemsProcessed(state.iterations() * batch_size);
    state.counters["batch_size"] = batch_size;
    state.counters["points_per_sec"] = benchmark::Counter(
        state.iterations() * batch_size, benchmark::Counter::kIsRate);
}

BENCHMARK_REGISTER_F(TimeseriesBenchmarkFixture, BatchIngestion)
    ->Arg(10)
    ->Arg(100)
    ->Arg(1000)
    ->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmark: Multiple Metrics/Entities
// ============================================================================

BENCHMARK_DEFINE_F(TimeseriesBenchmarkFixture, MultipleMetrics)(benchmark::State& state) {
    const int num_entities = state.range(0);
    
    std::vector<std::string> metrics = {"cpu", "memory", "disk_io", "network_in", "network_out"};
    std::vector<std::string> entities;
    for (int i = 0; i < num_entities; i++) {
        entities.push_back("server_" + std::to_string(i));
    }
    
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> value_dist(0.0, 100.0);
    std::uniform_int_distribution<int> metric_dist(0, metrics.size() - 1);
    std::uniform_int_distribution<int> entity_dist(0, entities.size() - 1);
    
    int64_t timestamp = 1700000000000;
    
    for (auto _ : state) {
        TimeSeriesStore::DataPoint point;
        point.timestamp_ms = timestamp++;
        point.value = value_dist(rng);
        
        const std::string& metric = metrics[metric_dist(rng)];
        const std::string& entity = entities[entity_dist(rng)];
        
        ts_store_->put(metric, entity, point);
    }
    
    state.SetItemsProcessed(state.iterations());
    state.counters["num_entities"] = num_entities;
    state.counters["num_metrics"] = metrics.size();
}

BENCHMARK_REGISTER_F(TimeseriesBenchmarkFixture, MultipleMetrics)
    ->Arg(10)
    ->Arg(100)
    ->Arg(1000)
    ->Unit(benchmark::kMicrosecond);

// ============================================================================
// Benchmark: Gorilla Compression
// ============================================================================

static void BM_GorillaCompression(benchmark::State& state) {
    const int num_points = state.range(0);
    
    std::mt19937 rng(42);
    // Generate realistic sensor data with gradual changes
    std::normal_distribution<double> change_dist(0.0, 1.0);
    
    std::vector<double> values;
    std::vector<int64_t> timestamps;
    
    double current_value = 20.0; // Start at 20°C
    int64_t current_timestamp = 1700000000000;
    
    for (int i = 0; i < num_points; i++) {
        current_value += change_dist(rng); // Small changes
        current_timestamp += 1000; // 1 second intervals
        
        values.push_back(current_value);
        timestamps.push_back(current_timestamp);
    }
    
    for (auto _ : state) {
        timeseries::GorillaEncoder encoder;
        
        // Compress data
        for (size_t i = 0; i < values.size(); i++) {
            encoder.addPoint(timestamps[i], values[i]);
        }
        
        auto compressed = encoder.finish();
        benchmark::DoNotOptimize(compressed);
        
        // Calculate compression ratio
        size_t raw_size = num_points * (sizeof(int64_t) + sizeof(double));
        size_t compressed_size = compressed.size();
        double compression_ratio = static_cast<double>(raw_size) / compressed_size;
        
        state.counters["raw_bytes"] = raw_size;
        state.counters["compressed_bytes"] = compressed_size;
        state.counters["compression_ratio"] = compression_ratio;
    }
    
    state.SetItemsProcessed(state.iterations() * num_points);
    state.counters["points_per_sec"] = benchmark::Counter(
        state.iterations() * num_points, benchmark::Counter::kIsRate);
}

BENCHMARK(BM_GorillaCompression)
    ->Arg(100)
    ->Arg(1000)
    ->Arg(10000)
    ->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmark: Gorilla Decompression
// ============================================================================

static void BM_GorillaDecompression(benchmark::State& state) {
    const int num_points = state.range(0);
    
    // Generate and compress data first
    timeseries::GorillaEncoder encoder;
    int64_t timestamp = 1700000000000;
    double value = 20.0;
    
    std::mt19937 rng(42);
    std::normal_distribution<double> change_dist(0.0, 1.0);
    
    for (int i = 0; i < num_points; i++) {
        value += change_dist(rng);
        timestamp += 1000;
        encoder.addPoint(timestamp, value);
    }
    
    auto compressed = encoder.finish();
    
    for (auto _ : state) {
        timeseries::GorillaDecoder decoder(compressed);
        
        std::vector<std::pair<int64_t, double>> decompressed;
        decompressed.reserve(num_points);
        
        while (decoder.hasNext()) {
            auto point = decoder.next();
            decompressed.push_back(point);
        }
        
        benchmark::DoNotOptimize(decompressed);
        
        if (decompressed.size() != static_cast<size_t>(num_points)) {
            state.SkipWithError("Decompression failed");
        }
    }
    
    state.SetItemsProcessed(state.iterations() * num_points);
    state.counters["points_per_sec"] = benchmark::Counter(
        state.iterations() * num_points, benchmark::Counter::kIsRate);
}

BENCHMARK(BM_GorillaDecompression)
    ->Arg(100)
    ->Arg(1000)
    ->Arg(10000)
    ->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmark: Time-Range Query Performance
// ============================================================================

BENCHMARK_DEFINE_F(TimeseriesBenchmarkFixture, TimeRangeQuery)(benchmark::State& state) {
    const std::string metric = "temperature";
    const std::string entity = "sensor_1";
    
    // Pre-populate with 10,000 data points
    const int total_points = 10000;
    int64_t base_timestamp = 1700000000000;
    
    std::mt19937 rng(42);
    std::normal_distribution<double> value_dist(20.0, 5.0);
    
    for (int i = 0; i < total_points; i++) {
        TimeSeriesStore::DataPoint point;
        point.timestamp_ms = base_timestamp + (i * 1000); // 1 second intervals
        point.value = value_dist(rng);
        ts_store_->put(metric, entity, point);
    }
    
    // Query different time ranges
    const int range_size = state.range(0); // in seconds
    
    for (auto _ : state) {
        int64_t start_time = base_timestamp;
        int64_t end_time = start_time + (range_size * 1000);
        
        auto results = ts_store_->query(metric, entity, start_time, end_time);
        benchmark::DoNotOptimize(results);
    }
    
    state.SetItemsProcessed(state.iterations());
    state.counters["range_seconds"] = range_size;
}

BENCHMARK_REGISTER_F(TimeseriesBenchmarkFixture, TimeRangeQuery)
    ->Arg(60)      // 1 minute
    ->Arg(300)     // 5 minutes
    ->Arg(3600)    // 1 hour
    ->Arg(86400)   // 1 day
    ->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmark: Downsampling Performance
// ============================================================================

BENCHMARK_DEFINE_F(TimeseriesBenchmarkFixture, Downsampling)(benchmark::State& state) {
    const std::string metric = "requests_per_sec";
    const std::string entity = "app_server";
    
    // Pre-populate with high-resolution data (1 point per second for 1 hour)
    const int total_points = 3600;
    int64_t base_timestamp = 1700000000000;
    
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> value_dist(100.0, 1000.0);
    
    for (int i = 0; i < total_points; i++) {
        TimeSeriesStore::DataPoint point;
        point.timestamp_ms = base_timestamp + (i * 1000);
        point.value = value_dist(rng);
        ts_store_->put(metric, entity, point);
    }
    
    const int downsample_interval = state.range(0); // in seconds
    
    for (auto _ : state) {
        int64_t start_time = base_timestamp;
        int64_t end_time = base_timestamp + (3600 * 1000);
        
        // Aggregate into intervals
        auto aggregated = ts_store_->aggregate(
            metric, entity, start_time, end_time, downsample_interval * 1000);
        
        benchmark::DoNotOptimize(aggregated);
    }
    
    state.SetItemsProcessed(state.iterations());
    state.counters["downsample_interval_sec"] = downsample_interval;
}

BENCHMARK_REGISTER_F(TimeseriesBenchmarkFixture, Downsampling)
    ->Arg(60)      // 1 minute
    ->Arg(300)     // 5 minutes
    ->Arg(3600)    // 1 hour
    ->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmark: Out-of-Order Writes
// ============================================================================

BENCHMARK_DEFINE_F(TimeseriesBenchmarkFixture, OutOfOrderWrites)(benchmark::State& state) {
    const std::string metric = "latency";
    const std::string entity = "service_1";
    
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> value_dist(0.0, 100.0);
    std::uniform_int_distribution<int> time_offset_dist(-3600, 3600); // ±1 hour jitter
    
    int64_t base_timestamp = 1700000000000;
    size_t point_count = 0;
    
    for (auto _ : state) {
        TimeSeriesStore::DataPoint point;
        // Add random time offset to simulate out-of-order arrival
        point.timestamp_ms = base_timestamp + (point_count * 1000) + (time_offset_dist(rng) * 1000);
        point.value = value_dist(rng);
        
        ts_store_->put(metric, entity, point);
        point_count++;
    }
    
    state.SetItemsProcessed(state.iterations());
    state.counters["points_per_sec"] = benchmark::Counter(
        state.iterations(), benchmark::Counter::kIsRate);
}

BENCHMARK_REGISTER_F(TimeseriesBenchmarkFixture, OutOfOrderWrites)
    ->Threads(1)
    ->Threads(4)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
