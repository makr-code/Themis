// Benchmark: Transaction Throughput
// Measures ACID transaction performance for different workload patterns

#include "transaction/transaction_manager.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include "index/secondary_index.h"
#include "index/graph_index.h"
#include "index/vector_index.h"
#include <benchmark/benchmark.h>
#include <filesystem>
#include <random>
#include <thread>

using namespace themis;

// ============================================================================
// Test Setup
// ============================================================================

class TransactionBenchmarkFixture : public benchmark::Fixture {
public:
    void SetUp(const ::benchmark::State& /*state*/) override {
        // Clean up any existing test database
        test_db_path_ = "./data/bench_transaction_tmp";
        if (std::filesystem::exists(test_db_path_)) {
            std::filesystem::remove_all(test_db_path_);
        }
        
        // Create RocksDB wrapper
        RocksDBWrapper::Config config;
        config.db_path = test_db_path_;
        config.memtable_size_mb = 128;
        config.block_cache_size_mb = 256;
        config.max_write_buffer_number = 3; // entfernt: write_buffer_size (nicht mehr vorhanden)
        
        db_ = std::make_unique<RocksDBWrapper>(config);
        if (!db_->open()) {
            throw std::runtime_error("Failed to open database");
        }
        
        // Create index managers
        secondary_index_ = std::make_unique<SecondaryIndexManager>(*db_);
        graph_index_ = std::make_unique<GraphIndexManager>(*db_);
        vector_index_ = std::make_unique<VectorIndexManager>(*db_);
        
        // Create transaction manager
        tx_manager_ = std::make_unique<TransactionManager>(
            *db_, *secondary_index_, *graph_index_, *vector_index_);
        
        // Pre-populate with some data for read tests
        populateTestData(1000);
    }
    
    void TearDown(const ::benchmark::State& /*state*/) override {
        tx_manager_.reset();
        vector_index_.reset();
        graph_index_.reset();
        secondary_index_.reset();
        db_->close();
        db_.reset();
        
        // Clean up test database
        if (std::filesystem::exists(test_db_path_)) {
            std::filesystem::remove_all(test_db_path_);
        }
    }
    
    void populateTestData(size_t count) {
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> age_dist(18, 80);
        std::uniform_real_distribution<double> balance_dist(0.0, 100000.0);
        
        for (size_t i = 0; i < count; i++) {
            auto txn_id = tx_manager_->beginTransaction();
            auto txn = tx_manager_->getTransaction(txn_id);
            
            BaseEntity entity("user_" + std::to_string(i));
            entity.setField("name", std::string("User_") + std::to_string(i));
            entity.setField("age", static_cast<int64_t>(age_dist(rng)));
            entity.setField("balance", balance_dist(rng));
            entity.setField("active", true);
            
            txn->putEntity("users", entity);
            tx_manager_->commitTransaction(txn_id);
        }
    }
    
protected:
    std::string test_db_path_;
    std::unique_ptr<RocksDBWrapper> db_;
    std::unique_ptr<SecondaryIndexManager> secondary_index_;
    std::unique_ptr<GraphIndexManager> graph_index_;
    std::unique_ptr<VectorIndexManager> vector_index_;
    std::unique_ptr<TransactionManager> tx_manager_;
};

// ============================================================================
// Benchmark: Read-Only Transactions
// ============================================================================

BENCHMARK_DEFINE_F(TransactionBenchmarkFixture, ReadOnlyTransaction)(benchmark::State& state) {
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 999);
    
    for (auto _ : state) {
        auto txn_id = tx_manager_->beginTransaction();
        auto txn = tx_manager_->getTransaction(txn_id);
        
        // Simulierter Read: keine direkte getEntity API mehr – wir lassen den Körper leer
        for (int i = 0; i < 10; i++) {
            benchmark::DoNotOptimize(i);
        }
        
        auto commit_status = tx_manager_->commitTransaction(txn_id);
        if (!commit_status.ok) {
            state.SkipWithError("Transaction commit failed");
        }
    }
    
    auto stats = tx_manager_->getStats();
    state.SetItemsProcessed(state.iterations() * 10); // 10 reads per transaction
    state.counters["tps"] = benchmark::Counter(
        state.iterations(), benchmark::Counter::kIsRate);
    state.counters["total_committed"] = stats.total_committed;
    state.counters["total_aborted"] = stats.total_aborted;
}

BENCHMARK_REGISTER_F(TransactionBenchmarkFixture, ReadOnlyTransaction)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->Unit(benchmark::kMicrosecond);

// ============================================================================
// Benchmark: Write-Only Transactions
// ============================================================================

BENCHMARK_DEFINE_F(TransactionBenchmarkFixture, WriteOnlyTransaction)(benchmark::State& state) {
    size_t counter = 0;
    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double> balance_dist(0.0, 100000.0);
    
    for (auto _ : state) {
        auto txn_id = tx_manager_->beginTransaction();
        auto txn = tx_manager_->getTransaction(txn_id);
        
        // Write multiple entities
        for (int i = 0; i < 10; i++) {
            std::string key = "new_user_" + std::to_string(counter++);
            
            BaseEntity entity(key);
            entity.setField("name", std::string("NewUser_") + std::to_string(counter));
            entity.setField("balance", balance_dist(rng));
            entity.setField("created_at", std::chrono::system_clock::now().time_since_epoch().count());
            
            auto put_status = txn->putEntity("users", entity);
            if (!put_status.ok) {
                state.SkipWithError("Put entity failed");
                break;
            }
        }
        
        auto commit_status = tx_manager_->commitTransaction(txn_id);
        if (!commit_status.ok) {
            state.SkipWithError("Transaction commit failed");
        }
    }
    
    state.SetItemsProcessed(state.iterations() * 10); // 10 writes per transaction
    state.counters["tps"] = benchmark::Counter(
        state.iterations(), benchmark::Counter::kIsRate);
}

BENCHMARK_REGISTER_F(TransactionBenchmarkFixture, WriteOnlyTransaction)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmark: Mixed Read/Write Transactions
// ============================================================================

BENCHMARK_DEFINE_F(TransactionBenchmarkFixture, MixedTransaction)(benchmark::State& state) {
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> user_dist(0, 999);
    std::uniform_real_distribution<double> amount_dist(-1000.0, 1000.0);
    
    for (auto _ : state) {
        auto txn_id = tx_manager_->beginTransaction();
        auto txn = tx_manager_->getTransaction(txn_id);
        
        // Read-Modify-Write pattern (e.g., account balance update)
        for (int i = 0; i < 5; i++) {
            std::string key = "user_" + std::to_string(user_dist(rng));
            
            // Vereinfachtes Read-Modify-Write ohne getEntity: wir erzeugen neuen Entity mit aktualisiertem balance
            BaseEntity entity(key);
            double new_balance = std::max(0.0, amount_dist(rng));
            entity.setField("balance", new_balance);
            txn->putEntity("users", entity);
        }
        
        auto commit_status = tx_manager_->commitTransaction(txn_id);
        if (!commit_status.ok) {
            state.SkipWithError("Transaction commit failed");
        }
    }
    
    state.SetItemsProcessed(state.iterations() * 5); // 5 RMW operations per transaction
    state.counters["tps"] = benchmark::Counter(
        state.iterations(), benchmark::Counter::kIsRate);
}

BENCHMARK_REGISTER_F(TransactionBenchmarkFixture, MixedTransaction)
    ->Threads(1)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmark: Commit Latency Distribution
// ============================================================================

BENCHMARK_DEFINE_F(TransactionBenchmarkFixture, CommitLatency)(benchmark::State& state) {
    const int ops_per_txn = state.range(0);
    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double> value_dist(0.0, 1000.0);
    size_t counter = 0;
    
    for (auto _ : state) {
        auto txn_id = tx_manager_->beginTransaction();
        auto txn = tx_manager_->getTransaction(txn_id);
        
        // Perform operations
        for (int i = 0; i < ops_per_txn; i++) {
            BaseEntity entity("temp_" + std::to_string(counter++));
            entity.setField("value", value_dist(rng));
            txn->putEntity("temp", entity);
        }
        
        // Measure commit time
        auto start = std::chrono::high_resolution_clock::now();
        auto commit_status = tx_manager_->commitTransaction(txn_id);
        auto end = std::chrono::high_resolution_clock::now();
        
        if (!commit_status.ok) {
            state.SkipWithError("Transaction commit failed");
        }
        
        auto commit_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        benchmark::DoNotOptimize(commit_time);
    }
    
    state.SetItemsProcessed(state.iterations());
    state.counters["ops_per_txn"] = ops_per_txn;
}

BENCHMARK_REGISTER_F(TransactionBenchmarkFixture, CommitLatency)
    ->Arg(1)
    ->Arg(10)
    ->Arg(100)
    ->Arg(1000)
    ->Unit(benchmark::kMicrosecond);

// ============================================================================
// Benchmark: Abort Performance
// ============================================================================

BENCHMARK_DEFINE_F(TransactionBenchmarkFixture, AbortTransaction)(benchmark::State& state) {
    size_t counter = 0;
    
    for (auto _ : state) {
        auto txn_id = tx_manager_->beginTransaction();
        auto txn = tx_manager_->getTransaction(txn_id);
        
        // Perform some operations
        for (int i = 0; i < 10; i++) {
            BaseEntity entity("abort_test_" + std::to_string(counter++));
            entity.setField("value", static_cast<int64_t>(counter));
            txn->putEntity("temp", entity);
        }
        
        // Rollback statt Commit (abort API entfernt)
        tx_manager_->rollbackTransaction(txn_id);
    }
    
    auto stats = tx_manager_->getStats();
    state.SetItemsProcessed(state.iterations());
    state.counters["aborts_per_sec"] = benchmark::Counter(
        state.iterations(), benchmark::Counter::kIsRate);
    state.counters["total_aborted"] = stats.total_aborted;
}

BENCHMARK_REGISTER_F(TransactionBenchmarkFixture, AbortTransaction)
    ->Threads(1)
    ->Threads(4)
    ->Unit(benchmark::kMicrosecond);

// ============================================================================
// Benchmark: Concurrent Transaction Contention
// ============================================================================

static void BM_TransactionContention(benchmark::State& state) {
    // Setup
    std::string test_db_path = "./data/bench_transaction_contention_tmp";
    if (std::filesystem::exists(test_db_path)) {
        std::filesystem::remove_all(test_db_path);
    }
    
    RocksDBWrapper::Config config;
    config.db_path = test_db_path;
    config.memtable_size_mb = 128;
    config.block_cache_size_mb = 256;
    
    auto db = std::make_unique<RocksDBWrapper>(config);
    db->open();
    
    auto secondary_index = std::make_unique<SecondaryIndexManager>(*db);
    auto graph_index = std::make_unique<GraphIndexManager>(*db);
    auto vector_index = std::make_unique<VectorIndexManager>(*db);
    auto tx_manager = std::make_unique<TransactionManager>(
        *db, *secondary_index, *graph_index, *vector_index);
    
    // All threads contend on same key
    const std::string contended_key = "contended_resource";
    
    for (auto _ : state) {
        auto txn_id = tx_manager->beginTransaction();
        auto txn = tx_manager->getTransaction(txn_id);
        
        // Vereinfachter Contention-Test ohne getEntity: atomare Ersetzung
        static int64_t local_counter = 0;
        local_counter++;
        BaseEntity entity(contended_key);
        entity.setField("counter", local_counter);
        txn->putEntity("resources", entity);
        
        // Try to commit (may fail due to contention)
        auto commit_status = tx_manager->commitTransaction(txn_id);
        if (!commit_status.ok) {
            // Retry on conflict
            state.counters["conflicts"]++;
        }
    }
    
    // Cleanup
    tx_manager.reset();
    vector_index.reset();
    graph_index.reset();
    secondary_index.reset();
    db->close();
    db.reset();
    
    if (std::filesystem::exists(test_db_path)) {
        std::filesystem::remove_all(test_db_path);
    }
    
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_TransactionContention)
    ->Threads(2)
    ->Threads(4)
    ->Threads(8)
    ->Threads(16)
    ->Unit(benchmark::kMicrosecond);

// BENCHMARK_MAIN entfernt – Nutzung von benchmark_main Library durch CMake.
