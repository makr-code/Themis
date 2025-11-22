// Benchmark: Shard Routing Performance
// Measures routing latency for different shard topologies and request patterns

#include "sharding/shard_router.h"
#include "sharding/consistent_hash.h"
#include "sharding/urn_resolver.h"
#include "sharding/remote_executor.h"
#include "sharding/urn.h"
#include <benchmark/benchmark.h>
#include <memory>
#include <random>
#include <string>

using namespace themis::sharding;

// ============================================================================
// Test Setup - Mock Components
// ============================================================================

class MockRemoteExecutor : public RemoteExecutor {
public:
    struct Response {
        bool success = true;
        nlohmann::json data;
    };
    
    Response execute(const std::string& /*endpoint*/, 
                    const std::string& /*method*/,
                    const std::string& /*path*/,
                    const std::optional<nlohmann::json>& /*body*/) override {
        // Simulate network latency (10-50 microseconds)
        std::this_thread::sleep_for(std::chrono::microseconds(20));
        
        Response resp;
        resp.success = true;
        resp.data = nlohmann::json{{"result", "ok"}};
        return resp;
    }
};

// ============================================================================
// Benchmark Fixtures
// ============================================================================

class ShardRoutingFixture : public benchmark::Fixture {
public:
    void SetUp(const ::benchmark::State& state) override {
        num_shards_ = state.range(0);
        
        // Create consistent hash with specified number of shards
        consistent_hash_ = std::make_shared<ConsistentHash>(num_shards_);
        
        // Add virtual nodes for better distribution
        for (int i = 0; i < num_shards_; i++) {
            std::string shard_id = "shard_" + std::to_string(i);
            std::string endpoint = "http://shard" + std::to_string(i) + ".example.com:8080";
            
            consistent_hash_->addNode(shard_id, endpoint, 150); // 150 virtual nodes per shard
        }
        
        // Create URN resolver and remote executor
        resolver_ = std::make_shared<URNResolver>(consistent_hash_);
        executor_ = std::make_shared<MockRemoteExecutor>();
        
        // Create shard router
        ShardRouter::Config config;
        config.scatter_gather_timeout_ms = 5000;
        config.max_parallel_requests = 16;
        router_ = std::make_unique<ShardRouter>(resolver_, executor_, config);
        
        // Pre-generate random URNs for testing
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> dist(1, 1000000);
        
        for (int i = 0; i < 10000; i++) {
            std::string urn_str = "urn:themis:user:" + std::to_string(dist(rng));
            test_urns_.push_back(URN::parse(urn_str).value());
        }
    }
    
    void TearDown(const ::benchmark::State& /*state*/) override {
        router_.reset();
        executor_.reset();
        resolver_.reset();
        consistent_hash_.reset();
        test_urns_.clear();
    }
    
protected:
    int num_shards_;
    std::shared_ptr<ConsistentHash> consistent_hash_;
    std::shared_ptr<URNResolver> resolver_;
    std::shared_ptr<MockRemoteExecutor> executor_;
    std::unique_ptr<ShardRouter> router_;
    std::vector<URN> test_urns_;
};

// ============================================================================
// Benchmarks: Single Shard Routing
// ============================================================================

BENCHMARK_DEFINE_F(ShardRoutingFixture, SingleShardLookup)(benchmark::State& state) {
    size_t urn_index = 0;
    
    for (auto _ : state) {
        const URN& urn = test_urns_[urn_index % test_urns_.size()];
        
        auto result = router_->get(urn);
        benchmark::DoNotOptimize(result);
        
        urn_index++;
    }
    
    state.SetItemsProcessed(state.iterations());
    state.counters["shards"] = num_shards_;
    state.counters["requests_per_sec"] = benchmark::Counter(
        state.iterations(), benchmark::Counter::kIsRate);
}

// Test with different shard counts: 10, 100, 1000 shards
BENCHMARK_REGISTER_F(ShardRoutingFixture, SingleShardLookup)
    ->Arg(10)
    ->Arg(100)
    ->Arg(1000)
    ->Unit(benchmark::kMicrosecond);

// ============================================================================
// Benchmarks: Consistent Hash Distribution
// ============================================================================

BENCHMARK_DEFINE_F(ShardRoutingFixture, ConsistentHashPerformance)(benchmark::State& state) {
    size_t urn_index = 0;
    
    for (auto _ : state) {
        const URN& urn = test_urns_[urn_index % test_urns_.size()];
        
        // Direct hash lookup (without full routing)
        auto shard_info = consistent_hash_->getNode(urn.toString());
        benchmark::DoNotOptimize(shard_info);
        
        urn_index++;
    }
    
    state.SetItemsProcessed(state.iterations());
    state.counters["shards"] = num_shards_;
    state.counters["lookups_per_sec"] = benchmark::Counter(
        state.iterations(), benchmark::Counter::kIsRate);
}

BENCHMARK_REGISTER_F(ShardRoutingFixture, ConsistentHashPerformance)
    ->Arg(10)
    ->Arg(100)
    ->Arg(1000)
    ->Unit(benchmark::kNanosecond);

// ============================================================================
// Benchmarks: Batch Operations
// ============================================================================

BENCHMARK_DEFINE_F(ShardRoutingFixture, BatchRouting)(benchmark::State& state) {
    const int batch_size = state.range(1);
    size_t urn_index = 0;
    
    for (auto _ : state) {
        std::vector<URN> batch;
        batch.reserve(batch_size);
        
        for (int i = 0; i < batch_size; i++) {
            batch.push_back(test_urns_[(urn_index + i) % test_urns_.size()]);
        }
        
        // Route each URN in batch
        for (const auto& urn : batch) {
            auto result = router_->get(urn);
            benchmark::DoNotOptimize(result);
        }
        
        urn_index += batch_size;
    }
    
    state.SetItemsProcessed(state.iterations() * batch_size);
    state.counters["shards"] = num_shards_;
    state.counters["batch_size"] = batch_size;
}

BENCHMARK_REGISTER_F(ShardRoutingFixture, BatchRouting)
    ->Args({10, 10})    // 10 shards, batch size 10
    ->Args({10, 100})   // 10 shards, batch size 100
    ->Args({100, 10})   // 100 shards, batch size 10
    ->Args({100, 100})  // 100 shards, batch size 100
    ->Unit(benchmark::kMicrosecond);

// ============================================================================
// Benchmarks: Distribution Quality
// ============================================================================

static void BM_DistributionQuality(benchmark::State& state) {
    const int num_shards = state.range(0);
    const int num_keys = 10000;
    
    auto hash = std::make_shared<ConsistentHash>(num_shards);
    
    // Add nodes
    for (int i = 0; i < num_shards; i++) {
        std::string shard_id = "shard_" + std::to_string(i);
        std::string endpoint = "http://shard" + std::to_string(i) + ".example.com:8080";
        hash->addNode(shard_id, endpoint, 150);
    }
    
    // Generate keys and measure distribution
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(1, 1000000);
    
    std::map<std::string, int> shard_counts;
    
    for (auto _ : state) {
        shard_counts.clear();
        
        for (int i = 0; i < num_keys; i++) {
            std::string key = "key_" + std::to_string(dist(rng));
            auto shard_info = hash->getNode(key);
            
            if (shard_info) {
                shard_counts[shard_info->shard_id]++;
            }
        }
        
        // Calculate standard deviation of distribution
        double mean = static_cast<double>(num_keys) / num_shards;
        double variance = 0.0;
        
        for (const auto& [shard, count] : shard_counts) {
            double diff = count - mean;
            variance += diff * diff;
        }
        
        double std_dev = std::sqrt(variance / num_shards);
        benchmark::DoNotOptimize(std_dev);
        
        state.counters["mean_keys_per_shard"] = mean;
        state.counters["std_dev"] = std_dev;
        state.counters["cv"] = std_dev / mean; // Coefficient of variation
    }
    
    state.SetItemsProcessed(state.iterations() * num_keys);
}

BENCHMARK(BM_DistributionQuality)
    ->Arg(10)
    ->Arg(100)
    ->Arg(1000)
    ->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmarks: Hot Shard Scenario
// ============================================================================

BENCHMARK_DEFINE_F(ShardRoutingFixture, HotShardPattern)(benchmark::State& state) {
    // 80% of requests go to 20% of URNs (hot keys)
    const size_t hot_set_size = test_urns_.size() / 5; // 20% hot
    
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> hot_dist(0, 99);
    std::uniform_int_distribution<size_t> hot_urn_dist(0, hot_set_size - 1);
    std::uniform_int_distribution<size_t> cold_urn_dist(hot_set_size, test_urns_.size() - 1);
    
    for (auto _ : state) {
        // 80% chance of accessing hot key
        bool is_hot = hot_dist(rng) < 80;
        
        const URN& urn = is_hot 
            ? test_urns_[hot_urn_dist(rng)]
            : test_urns_[cold_urn_dist(rng)];
        
        auto result = router_->get(urn);
        benchmark::DoNotOptimize(result);
    }
    
    state.SetItemsProcessed(state.iterations());
    state.counters["shards"] = num_shards_;
}

BENCHMARK_REGISTER_F(ShardRoutingFixture, HotShardPattern)
    ->Arg(10)
    ->Arg(100)
    ->Arg(1000)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
