// Benchmark: Shard Routing Performance
// Measures routing latency for different shard topologies and request patterns

#include "sharding/shard_router.h"
#include "sharding/consistent_hash.h"
#include "sharding/urn_resolver.h"
#include "sharding/remote_executor.h"
#include "sharding/shard_topology.h"
#include "sharding/urn.h"
#include <benchmark/benchmark.h>
#include <memory>
#include <random>
#include <string>
#include <map>
#include <cmath>

using namespace themis::sharding;

// ============================================================================
// Benchmark Fixtures
// ============================================================================

class ShardRoutingFixture : public benchmark::Fixture {
public:
    void SetUp(const ::benchmark::State& state) override {
        num_shards_ = state.range(0);
        
        // Create consistent hash ring and topology
        hash_ring_ = std::make_shared<ConsistentHashRing>();
        ShardTopology::Config topo_cfg{ /*metadata_endpoint=*/"", /*cluster_name=*/"bench", /*refresh_interval_sec=*/0, /*enable_health_checks=*/false };
        topology_ = std::make_shared<ShardTopology>(topo_cfg);

        // Add shards to ring and topology
        for (int i = 0; i < num_shards_; i++) {
            std::string shard_id = "shard_" + std::to_string(i);
            hash_ring_->addShard(shard_id, 150); // 150 virtual nodes per shard

            ShardInfo info;
            info.shard_id = shard_id;
            info.primary_endpoint = "http://" + shard_id + ".example.com:8080";
            info.datacenter = "dc1";
            info.rack = "rack01";
            info.token_start = 0;
            info.token_end = 0;
            info.is_healthy = true;
            info.capabilities = {"read", "write"};
            topology_->addShard(info);
        }

        // Create URN resolver and remote executor; make all requests local to avoid network
        local_shard_id_ = "shard_0";
        resolver_ = std::make_shared<URNResolver>(topology_, hash_ring_, local_shard_id_);

        RemoteExecutor::Config rexec_cfg{};
        rexec_cfg.local_shard_id = local_shard_id_;
        executor_ = std::make_shared<RemoteExecutor>(rexec_cfg);

        // Create shard router
        ShardRouter::Config config;
        config.local_shard_id = local_shard_id_;
        config.scatter_timeout_ms = 5000;
        config.max_concurrent_shards = 16;
        router_ = std::make_unique<ShardRouter>(resolver_, executor_, config);
        
        // Pre-generate random URNs for testing
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> dist(1, 1000000);
        
        for (int i = 0; i < 10000; i++) {
            // Build a valid URN: urn:themis:{model}:{namespace}:{collection}:{uuid}
            // Generate a pseudo UUID v4-like string
            auto to_hex = [](uint32_t x) {
                static const char* hex = "0123456789abcdef";
                std::string s(8, '0');
                for (int i = 7; i >= 0; --i) { s[i] = hex[x & 0xF]; x >>= 4; }
                return s;
            };
            uint32_t a = static_cast<uint32_t>(dist(rng));
            uint32_t b = static_cast<uint32_t>(dist(rng));
            uint32_t c = static_cast<uint32_t>(dist(rng));
            uint32_t d = static_cast<uint32_t>(dist(rng));
            std::string uuid = to_hex(a).substr(0,8) + "-" + to_hex(b).substr(0,4) + "-" + to_hex(c).substr(0,4) + "-" + to_hex(d).substr(0,4) + "-" + to_hex(a^b^c^d);

            URN u{"document", "bench", "users", uuid};
            test_urns_.push_back(u);
        }
    }
    
    void TearDown(const ::benchmark::State& /*state*/) override {
        router_.reset();
        executor_.reset();
        resolver_.reset();
        hash_ring_.reset();
        topology_.reset();
        test_urns_.clear();
    }
    
protected:
    int num_shards_;
    std::shared_ptr<ConsistentHashRing> hash_ring_;
    std::shared_ptr<ShardTopology> topology_;
    std::shared_ptr<URNResolver> resolver_;
    std::shared_ptr<RemoteExecutor> executor_;
    std::unique_ptr<ShardRouter> router_;
    std::vector<URN> test_urns_;
    std::string local_shard_id_;
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
        auto shard_id = hash_ring_->getShardForURN(urn);
        benchmark::DoNotOptimize(shard_id);
        
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
    
    auto ring = std::make_shared<ConsistentHashRing>();
    
    // Add shards
    for (int i = 0; i < num_shards; i++) {
        std::string shard_id = "shard_" + std::to_string(i);
        ring->addShard(shard_id, 150);
    }
    
    // Generate keys and measure distribution
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(1, 1000000);
    
    std::map<std::string, int> shard_counts;
    
    for (auto _ : state) {
        shard_counts.clear();
        
        for (int i = 0; i < num_keys; i++) {
            // Build a URN and map via ring
            auto to_hex = [](uint32_t x) {
                static const char* hex = "0123456789abcdef";
                std::string s(8, '0');
                for (int i = 7; i >= 0; --i) { s[i] = hex[x & 0xF]; x >>= 4; }
                return s;
            };
            uint32_t a = static_cast<uint32_t>(dist(rng));
            uint32_t b = static_cast<uint32_t>(dist(rng));
            uint32_t c = static_cast<uint32_t>(dist(rng));
            uint32_t d = static_cast<uint32_t>(dist(rng));
            std::string uuid = to_hex(a).substr(0,8) + "-" + to_hex(b).substr(0,4) + "-" + to_hex(c).substr(0,4) + "-" + to_hex(d).substr(0,4) + "-" + to_hex(a^b^c^d);
            URN urn{"document", "bench", "keys", uuid};
            auto shard_id = ring->getShardForURN(urn);
            if (!shard_id.empty()) {
                shard_counts[shard_id]++;
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

// benchmark_main wird über CMake verlinkt
