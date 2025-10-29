// Compression validation & microbenchmarks
// Compares none vs lz4 vs zstd for CRUD operations and write amplification

#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include "utils/logger.h"
#include <benchmark/benchmark.h>
#include <random>
#include <filesystem>
#include <iostream>

using namespace themis;

namespace {

// Generate random JSON blob of given size
std::string generateRandomBlob(size_t size_bytes) {
    static std::mt19937_64 rng(42);
    static std::uniform_int_distribution<int> dist(0, 255);
    
    std::string blob = R"({"data":")" ;
    // Fill with random hex to reach target size
    size_t needed = size_bytes - blob.size() - 4; // reserve for "\"}"
    for (size_t i = 0; i < needed; ++i) {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02x", dist(rng));
        blob += hex[0];
    }
    blob += "\"}";
    return blob;
}

// Benchmark fixture with compression config
class CompressionFixture : public benchmark::Fixture {
public:
    void SetUp(const benchmark::State& state) override {
        // Extract compression type from state range
        int compression_id = static_cast<int>(state.range(0));
        std::string comp_type;
        if (compression_id == 0) comp_type = "none";
        else if (compression_id == 1) comp_type = "lz4";
        else if (compression_id == 2) comp_type = "zstd";
        else comp_type = "none";
        
        // Setup temp DB path
        db_path_ = "./bench_compression_" + comp_type + "_" + std::to_string(static_cast<int>(state.thread_index()));
        std::filesystem::remove_all(db_path_);
        
        RocksDBWrapper::Config config;
        config.db_path = db_path_;
        config.compression_default = comp_type;
        config.compression_bottommost = comp_type;
        config.memtable_size_mb = 64;
        config.block_cache_size_mb = 256;
        config.max_background_jobs = 2;
        config.enable_wal = false; // Disable WAL for cleaner measurement
        
        db_ = std::make_unique<RocksDBWrapper>(config);
        if (!db_->open()) {
            throw std::runtime_error("Failed to open RocksDB for benchmark");
        }
        
        THEMIS_INFO("Benchmark setup: compression={}, path={}", comp_type, db_path_);
    }
    
    void TearDown(const benchmark::State& state) override {
        // Print active compression type for validation
        if (state.thread_index() == 0) {
            std::string active = db_->getCompressionType();
            std::cout << "Active compression: " << active << "\n";
        }
        
        db_->close();
        db_.reset();
        std::filesystem::remove_all(db_path_);
    }
    
protected:
    std::unique_ptr<RocksDBWrapper> db_;
    std::string db_path_;
};

// Benchmark: Sequential writes (batch)
BENCHMARK_DEFINE_F(CompressionFixture, SequentialWrite)(benchmark::State& state) {
    size_t blob_size = state.range(1); // Second range: blob size in bytes
    int num_keys = 1000;
    
    for (auto _ : state) {
        state.PauseTiming();
        std::string blob = generateRandomBlob(blob_size);
        std::vector<uint8_t> blob_vec(blob.begin(), blob.end());
        state.ResumeTiming();
        
        for (int i = 0; i < num_keys; ++i) {
            std::string key = "table:key_" + std::to_string(i);
            db_->put(key, blob_vec);
        }
        
        state.PauseTiming();
        db_->flush();
        state.ResumeTiming();
    }
    
    state.SetItemsProcessed(state.iterations() * num_keys);
    state.SetBytesProcessed(state.iterations() * num_keys * blob_size);
}

// Benchmark: Random reads
BENCHMARK_DEFINE_F(CompressionFixture, RandomRead)(benchmark::State& state) {
    size_t blob_size = state.range(1);
    int num_keys = 1000;
    
    // Pre-populate
    std::string blob = generateRandomBlob(blob_size);
    std::vector<uint8_t> blob_vec(blob.begin(), blob.end());
    for (int i = 0; i < num_keys; ++i) {
        std::string key = "table:key_" + std::to_string(i);
        db_->put(key, blob_vec);
    }
    db_->flush();
    
    std::mt19937_64 rng(123);
    std::uniform_int_distribution<int> dist(0, num_keys - 1);
    
    for (auto _ : state) {
        int idx = dist(rng);
        std::string key = "table:key_" + std::to_string(idx);
        auto result = db_->get(key);
        benchmark::DoNotOptimize(result);
    }
    
    state.SetItemsProcessed(state.iterations());
}

// Register benchmarks with compression types (0=none, 1=lz4, 2=zstd) and blob sizes
BENCHMARK_REGISTER_F(CompressionFixture, SequentialWrite)
    ->Args({0, 512})   // none, 512B
    ->Args({1, 512})   // lz4, 512B
    ->Args({2, 512})   // zstd, 512B
    ->Args({0, 4096})  // none, 4KB
    ->Args({1, 4096})  // lz4, 4KB
    ->Args({2, 4096})  // zstd, 4KB
    ->Args({0, 16384}) // none, 16KB
    ->Args({1, 16384}) // lz4, 16KB
    ->Args({2, 16384}) // zstd, 16KB
    ->Unit(benchmark::kMillisecond);

BENCHMARK_REGISTER_F(CompressionFixture, RandomRead)
    ->Args({0, 4096})
    ->Args({1, 4096})
    ->Args({2, 4096})
    ->Unit(benchmark::kMicrosecond);

} // namespace

BENCHMARK_MAIN();
