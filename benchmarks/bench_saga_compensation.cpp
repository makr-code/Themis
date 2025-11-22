// Benchmark: SAGA Compensation Performance
// Measures SAGA rollback performance for different failure scenarios

#include "transaction/saga.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include "index/secondary_index.h"
#include "index/graph_index.h"
#include "index/vector_index.h"
#include <benchmark/benchmark.h>
#include <filesystem>
#include <atomic>
#include <chrono>

using namespace themis;

// ============================================================================
// Test Setup
// ============================================================================

class SagaBenchmarkFixture : public benchmark::Fixture {
public:
    void SetUp(const ::benchmark::State& /*state*/) override {
        // Clean up any existing test database
        test_db_path_ = "./data/bench_saga_tmp";
        if (std::filesystem::exists(test_db_path_)) {
            std::filesystem::remove_all(test_db_path_);
        }
        
        // Create RocksDB wrapper
        RocksDBWrapper::Config config;
        config.db_path = test_db_path_;
        config.memtable_size_mb = 128;
        config.block_cache_size_mb = 256;
        
        db_ = std::make_unique<RocksDBWrapper>(config);
        if (!db_->open()) {
            throw std::runtime_error("Failed to open database");
        }
        
        // Create index managers
        secondary_index_ = std::make_unique<SecondaryIndexManager>(*db_);
        graph_index_ = std::make_unique<GraphIndexManager>(*db_);
        vector_index_ = std::make_unique<VectorIndexManager>(*db_);
    }
    
    void TearDown(const ::benchmark::State& /*state*/) override {
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
    
protected:
    std::string test_db_path_;
    std::unique_ptr<RocksDBWrapper> db_;
    std::unique_ptr<SecondaryIndexManager> secondary_index_;
    std::unique_ptr<GraphIndexManager> graph_index_;
    std::unique_ptr<VectorIndexManager> vector_index_;
};

// ============================================================================
// Benchmark: Simple Compensation Chain
// ============================================================================

BENCHMARK_DEFINE_F(SagaBenchmarkFixture, SimpleCompensation)(benchmark::State& state) {
    const int num_steps = state.range(0);
    
    for (auto _ : state) {
        Saga saga;
        std::atomic<int> compensation_count{0};
        
        // Build saga with multiple steps
        for (int i = 0; i < num_steps; i++) {
            saga.addStep(
                "step_" + std::to_string(i),
                [&compensation_count, i]() {
                    compensation_count++;
                    // Simulate some work
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
            );
        }
        
        // Measure compensation time
        auto start = std::chrono::high_resolution_clock::now();
        saga.compensate();
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        benchmark::DoNotOptimize(duration);
        
        if (compensation_count != num_steps) {
            state.SkipWithError("Not all steps compensated");
        }
    }
    
    state.SetItemsProcessed(state.iterations() * num_steps);
    state.counters["num_steps"] = num_steps;
}

BENCHMARK_REGISTER_F(SagaBenchmarkFixture, SimpleCompensation)
    ->Arg(2)
    ->Arg(5)
    ->Arg(10)
    ->Arg(20)
    ->Unit(benchmark::kMicrosecond);

// ============================================================================
// Benchmark: Database Write Compensation
// ============================================================================

BENCHMARK_DEFINE_F(SagaBenchmarkFixture, DatabaseWriteCompensation)(benchmark::State& state) {
    const int num_writes = state.range(0);
    
    for (auto _ : state) {
        Saga saga;
        std::vector<std::string> written_keys;
        
        // Simulate distributed transaction with multiple writes
        for (int i = 0; i < num_writes; i++) {
            std::string key = "saga_entity_" + std::to_string(i);
            written_keys.push_back(key);
            
            // Forward action: write entity
            BaseEntity entity(key);
            entity.setField("value", i);
            entity.setField("timestamp", std::chrono::system_clock::now().time_since_epoch().count());
            secondary_index_->put("saga_test", entity);
            
            // Compensation action: delete entity
            saga.addStep(
                "write_" + key,
                [this, key]() {
                    secondary_index_->del("saga_test", key);
                }
            );
        }
        
        // Measure compensation time
        auto start = std::chrono::high_resolution_clock::now();
        saga.compensate();
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        benchmark::DoNotOptimize(duration);
        
        // Verify all compensations executed
        if (!saga.isFullyCompensated()) {
            state.SkipWithError("Not all steps compensated");
        }
    }
    
    state.SetItemsProcessed(state.iterations() * num_writes);
    state.counters["num_writes"] = num_writes;
}

BENCHMARK_REGISTER_F(SagaBenchmarkFixture, DatabaseWriteCompensation)
    ->Arg(5)
    ->Arg(10)
    ->Arg(20)
    ->Arg(50)
    ->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmark: Partial Compensation (Failure at Different Points)
// ============================================================================

BENCHMARK_DEFINE_F(SagaBenchmarkFixture, PartialCompensation)(benchmark::State& state) {
    const int total_steps = 10;
    const int failure_step = state.range(0); // Which step to fail at
    
    for (auto _ : state) {
        Saga saga;
        std::atomic<int> executed_steps{0};
        std::atomic<int> compensated_steps{0};
        
        // Execute steps until failure point
        for (int i = 0; i < total_steps; i++) {
            if (i < failure_step) {
                executed_steps++;
                
                saga.addStep(
                    "step_" + std::to_string(i),
                    [&compensated_steps]() {
                        compensated_steps++;
                    }
                );
            }
        }
        
        // Compensate only executed steps
        saga.compensate();
        
        if (compensated_steps != failure_step) {
            state.SkipWithError("Compensation count mismatch");
        }
    }
    
    state.SetItemsProcessed(state.iterations() * failure_step);
    state.counters["total_steps"] = total_steps;
    state.counters["failure_at_step"] = failure_step;
}

BENCHMARK_REGISTER_F(SagaBenchmarkFixture, PartialCompensation)
    ->Arg(2)   // Fail after 2 steps
    ->Arg(5)   // Fail after 5 steps
    ->Arg(8)   // Fail after 8 steps
    ->Unit(benchmark::kMicrosecond);

// ============================================================================
// Benchmark: Concurrent SAGA Execution and Compensation
// ============================================================================

static void BM_ConcurrentSagaCompensation(benchmark::State& state) {
    const int num_sagas = state.range(0);
    const int steps_per_saga = 5;
    
    for (auto _ : state) {
        std::vector<std::unique_ptr<Saga>> sagas;
        std::atomic<int> total_compensations{0};
        
        // Create multiple SAGAs
        for (int i = 0; i < num_sagas; i++) {
            auto saga = std::make_unique<Saga>();
            
            for (int j = 0; j < steps_per_saga; j++) {
                saga->addStep(
                    "saga_" + std::to_string(i) + "_step_" + std::to_string(j),
                    [&total_compensations]() {
                        total_compensations++;
                        std::this_thread::sleep_for(std::chrono::microseconds(5));
                    }
                );
            }
            
            sagas.push_back(std::move(saga));
        }
        
        // Compensate all SAGAs concurrently
        auto start = std::chrono::high_resolution_clock::now();
        
        std::vector<std::thread> threads;
        for (auto& saga : sagas) {
            threads.emplace_back([&saga]() {
                saga->compensate();
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        
        benchmark::DoNotOptimize(duration);
        
        int expected_compensations = num_sagas * steps_per_saga;
        if (total_compensations != expected_compensations) {
            state.SkipWithError("Compensation count mismatch");
        }
    }
    
    state.SetItemsProcessed(state.iterations() * num_sagas * steps_per_saga);
    state.counters["num_sagas"] = num_sagas;
    state.counters["steps_per_saga"] = steps_per_saga;
}

BENCHMARK(BM_ConcurrentSagaCompensation)
    ->Arg(5)
    ->Arg(10)
    ->Arg(20)
    ->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmark: Compensation with Exceptions
// ============================================================================

BENCHMARK_DEFINE_F(SagaBenchmarkFixture, CompensationWithErrors)(benchmark::State& state) {
    const int num_steps = 10;
    const int failing_step = 5; // Middle step will fail
    
    for (auto _ : state) {
        Saga saga;
        std::atomic<int> successful_compensations{0};
        
        for (int i = 0; i < num_steps; i++) {
            saga.addStep(
                "step_" + std::to_string(i),
                [i, failing_step, &successful_compensations]() {
                    if (i == failing_step) {
                        throw std::runtime_error("Intentional compensation failure");
                    }
                    successful_compensations++;
                }
            );
        }
        
        // Compensate (some will fail)
        saga.compensate();
        
        // Should have compensated all except the one that failed
        int expected = num_steps - 1;
        if (successful_compensations != expected) {
            state.SkipWithError("Unexpected compensation count");
        }
    }
    
    state.SetItemsProcessed(state.iterations() * num_steps);
}

BENCHMARK_REGISTER_F(SagaBenchmarkFixture, CompensationWithErrors)
    ->Unit(benchmark::kMicrosecond);

// ============================================================================
// Benchmark: Nested SAGA Pattern
// ============================================================================

BENCHMARK_DEFINE_F(SagaBenchmarkFixture, NestedSagaPattern)(benchmark::State& state) {
    const int outer_steps = 3;
    const int inner_steps = 5;
    
    for (auto _ : state) {
        Saga outer_saga;
        std::vector<std::unique_ptr<Saga>> inner_sagas;
        std::atomic<int> total_compensations{0};
        
        for (int i = 0; i < outer_steps; i++) {
            auto inner_saga = std::make_unique<Saga>();
            
            // Add inner steps
            for (int j = 0; j < inner_steps; j++) {
                inner_saga->addStep(
                    "inner_" + std::to_string(i) + "_" + std::to_string(j),
                    [&total_compensations]() {
                        total_compensations++;
                    }
                );
            }
            
            // Add outer step that compensates inner saga
            Saga* inner_ptr = inner_saga.get();
            outer_saga.addStep(
                "outer_" + std::to_string(i),
                [inner_ptr, &total_compensations]() {
                    inner_ptr->compensate();
                    total_compensations++;
                }
            );
            
            inner_sagas.push_back(std::move(inner_saga));
        }
        
        // Compensate outer saga (which will compensate inner sagas)
        outer_saga.compensate();
        
        int expected = outer_steps + (outer_steps * inner_steps);
        if (total_compensations != expected) {
            state.SkipWithError("Nested compensation failed");
        }
    }
    
    state.SetItemsProcessed(state.iterations() * (outer_steps + outer_steps * inner_steps));
    state.counters["outer_steps"] = outer_steps;
    state.counters["inner_steps"] = inner_steps;
}

BENCHMARK_REGISTER_F(SagaBenchmarkFixture, NestedSagaPattern)
    ->Unit(benchmark::kMicrosecond);

// ============================================================================
// Benchmark: Compensation Latency Distribution
// ============================================================================

static void BM_CompensationLatencyDistribution(benchmark::State& state) {
    const int num_steps = state.range(0);
    
    std::vector<int64_t> latencies;
    
    for (auto _ : state) {
        Saga saga;
        
        for (int i = 0; i < num_steps; i++) {
            saga.addStep(
                "step_" + std::to_string(i),
                [i]() {
                    // Variable work simulation
                    auto sleep_time = 10 + (i % 50); // 10-60 microseconds
                    std::this_thread::sleep_for(std::chrono::microseconds(sleep_time));
                }
            );
        }
        
        auto start = std::chrono::high_resolution_clock::now();
        saga.compensate();
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        latencies.push_back(duration);
    }
    
    // Calculate statistics
    std::sort(latencies.begin(), latencies.end());
    size_t p50_idx = latencies.size() / 2;
    size_t p95_idx = (latencies.size() * 95) / 100;
    size_t p99_idx = (latencies.size() * 99) / 100;
    
    state.counters["p50_us"] = latencies[p50_idx];
    state.counters["p95_us"] = latencies[p95_idx];
    state.counters["p99_us"] = latencies[p99_idx];
    state.counters["num_steps"] = num_steps;
    
    state.SetItemsProcessed(state.iterations() * num_steps);
}

BENCHMARK(BM_CompensationLatencyDistribution)
    ->Arg(5)
    ->Arg(10)
    ->Arg(20)
    ->Iterations(100)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
