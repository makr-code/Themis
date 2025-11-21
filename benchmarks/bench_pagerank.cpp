// Benchmark: PageRank Performance
// Measures PageRank computation performance for different graph sizes

#include "index/graph_index.h"
#include "index/graph_analytics.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include <benchmark/benchmark.h>
#include <filesystem>
#include <random>

using namespace themis;

// ============================================================================
// Test Setup  
// ============================================================================

class PageRankBenchmarkFixture : public benchmark::Fixture {
public:
    void SetUp(const ::benchmark::State& state) override {
        // Clean up any existing test database
        test_db_path_ = "./data/bench_pagerank_tmp";
        if (std::filesystem::exists(test_db_path_)) {
            std::filesystem::remove_all(test_db_path_);
        }
        
        // Create RocksDB wrapper
        RocksDBWrapper::Config config;
        config.db_path = test_db_path_;
        config.memtable_size_mb = 512;
        config.block_cache_size_mb = 1024;
        
        db_ = std::make_unique<RocksDBWrapper>(config);
        if (!db_->open()) {
            throw std::runtime_error("Failed to open database");
        }
        
        // Create graph index manager
        graph_mgr_ = std::make_unique<GraphIndexManager>(*db_);
        
        // Create graph analytics
        analytics_ = std::make_unique<GraphAnalytics>(*graph_mgr_);
        
        // Build test graph
        int num_nodes = state.range(0);
        int avg_degree = state.range(1);
        buildWebGraph(num_nodes, avg_degree);
    }
    
    void TearDown(const ::benchmark::State& /*state*/) override {
        analytics_.reset();
        graph_mgr_.reset();
        db_->close();
        db_.reset();
        
        // Clean up test database
        if (std::filesystem::exists(test_db_path_)) {
            std::filesystem::remove_all(test_db_path_);
        }
    }
    
    void buildWebGraph(int num_nodes, int avg_out_degree) {
        std::mt19937 rng(42);
        
        // Create nodes (web pages)
        for (int i = 0; i < num_nodes; i++) {
            std::string node_id = "page_" + std::to_string(i);
            node_ids_.push_back(node_id);
            
            BaseEntity node(node_id);
            node.setField("url", "http://example.com/page" + std::to_string(i));
            node.setField("index", static_cast<int64_t>(i));
            
            graph_mgr_->addVertex("web_graph", node);
        }
        
        // Create edges (links between pages)
        // Use preferential attachment for realistic web graph
        std::uniform_int_distribution<int> node_dist(0, num_nodes - 1);
        std::uniform_real_distribution<double> prob_dist(0.0, 1.0);
        
        for (int i = 0; i < num_nodes; i++) {
            int edges_to_add = avg_out_degree;
            
            for (int j = 0; j < edges_to_add; j++) {
                int target;
                
                // Preferential attachment: more likely to link to earlier pages
                if (prob_dist(rng) < 0.3 && i > 0) {
                    // Link to earlier page (authority)
                    target = node_dist(rng) % i;
                } else {
                    // Random link
                    target = node_dist(rng);
                }
                
                if (target != i) {
                    std::string edge_id = "link_" + std::to_string(i) + "_" + std::to_string(target);
                    
                    BaseEntity edge(edge_id);
                    edge.setField("type", "hyperlink");
                    
                    graph_mgr_->addEdge("web_graph", node_ids_[i], node_ids_[target], edge);
                }
            }
        }
    }
    
protected:
    std::string test_db_path_;
    std::unique_ptr<RocksDBWrapper> db_;
    std::unique_ptr<GraphIndexManager> graph_mgr_;
    std::unique_ptr<GraphAnalytics> analytics_;
    std::vector<std::string> node_ids_;
};

// ============================================================================
// Benchmark: PageRank Computation
// ============================================================================

BENCHMARK_DEFINE_F(PageRankBenchmarkFixture, PageRankStandard)(benchmark::State& state) {
    const double damping = 0.85;
    const int max_iterations = 20;
    const double tolerance = 1e-6;
    
    for (auto _ : state) {
        auto [status, ranks] = analytics_->pageRank(node_ids_, damping, max_iterations, tolerance);
        
        if (!status.ok) {
            state.SkipWithError("PageRank computation failed");
        }
        
        benchmark::DoNotOptimize(ranks);
    }
    
    state.SetItemsProcessed(state.iterations());
    state.counters["num_nodes"] = node_ids_.size();
    state.counters["max_iterations"] = max_iterations;
}

BENCHMARK_REGISTER_F(PageRankBenchmarkFixture, PageRankStandard)
    ->Args({100, 5})      // Small graph
    ->Args({1000, 5})     // Medium graph
    ->Args({10000, 5})    // Large graph
    ->Args({100000, 5})   // Web-scale graph
    ->Args({100, 20})     // Dense small graph
    ->Args({1000, 20})    // Dense medium graph
    ->Unit(benchmark::kMillisecond)
    ->Iterations(5);

// ============================================================================
// Benchmark: PageRank Convergence Analysis
// ============================================================================

BENCHMARK_DEFINE_F(PageRankBenchmarkFixture, PageRankConvergence)(benchmark::State& state) {
    const double damping = 0.85;
    const int max_iterations = state.range(2); // Variable iterations
    const double tolerance = 1e-6;
    
    int total_iterations = 0;
    
    for (auto _ : state) {
        auto [status, ranks] = analytics_->pageRank(node_ids_, damping, max_iterations, tolerance);
        
        if (!status.ok) {
            state.SkipWithError("PageRank computation failed");
        }
        
        // Count actual iterations (would need API support)
        total_iterations += max_iterations;
        
        benchmark::DoNotOptimize(ranks);
    }
    
    state.SetItemsProcessed(state.iterations());
    state.counters["avg_iterations"] = static_cast<double>(total_iterations) / state.iterations();
}

BENCHMARK_REGISTER_F(PageRankBenchmarkFixture, PageRankConvergence)
    ->Args({1000, 5, 10})   // 10 iterations
    ->Args({1000, 5, 20})   // 20 iterations
    ->Args({1000, 5, 50})   // 50 iterations
    ->Args({1000, 5, 100})  // 100 iterations
    ->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmark: PageRank with Different Damping Factors
// ============================================================================

static void BM_PageRankDampingFactors(benchmark::State& state) {
    // Setup
    std::string test_db_path = "./data/bench_pagerank_damping_tmp";
    if (std::filesystem::exists(test_db_path)) {
        std::filesystem::remove_all(test_db_path);
    }
    
    RocksDBWrapper::Config config;
    config.db_path = test_db_path;
    config.memtable_size_mb = 256;
    
    auto db = std::make_unique<RocksDBWrapper>(config);
    db->open();
    
    auto graph_mgr = std::make_unique<GraphIndexManager>(*db);
    auto analytics = std::make_unique<GraphAnalytics>(*graph_mgr);
    
    // Build small test graph
    const int num_nodes = 1000;
    std::vector<std::string> node_ids;
    
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> node_dist(0, num_nodes - 1);
    
    for (int i = 0; i < num_nodes; i++) {
        std::string node_id = "node_" + std::to_string(i);
        node_ids.push_back(node_id);
        
        BaseEntity node(node_id);
        node.setField("index", static_cast<int64_t>(i));
        graph_mgr->addVertex("test", node);
    }
    
    for (int i = 0; i < num_nodes; i++) {
        for (int j = 0; j < 5; j++) {
            int target = node_dist(rng);
            if (target != i) {
                std::string edge_id = "e_" + std::to_string(i) + "_" + std::to_string(target);
                BaseEntity edge(edge_id);
                graph_mgr->addEdge("test", node_ids[i], node_ids[target], edge);
            }
        }
    }
    
    // Benchmark different damping factors
    double damping = state.range(0) / 100.0; // Convert from int to double
    
    for (auto _ : state) {
        auto [status, ranks] = analytics->pageRank(node_ids, damping, 20, 1e-6);
        benchmark::DoNotOptimize(ranks);
    }
    
    // Cleanup
    analytics.reset();
    graph_mgr.reset();
    db->close();
    db.reset();
    
    if (std::filesystem::exists(test_db_path)) {
        std::filesystem::remove_all(test_db_path);
    }
    
    state.SetItemsProcessed(state.iterations());
    state.counters["damping_factor"] = damping;
}

BENCHMARK(BM_PageRankDampingFactors)
    ->Arg(50)   // 0.50 damping
    ->Arg(75)   // 0.75 damping
    ->Arg(85)   // 0.85 damping (standard)
    ->Arg(90)   // 0.90 damping
    ->Arg(95)   // 0.95 damping
    ->Unit(benchmark::kMillisecond)
    ->Iterations(3);

// ============================================================================
// Benchmark: Personalized PageRank
// ============================================================================

BENCHMARK_DEFINE_F(PageRankBenchmarkFixture, PersonalizedPageRank)(benchmark::State& state) {
    // Personalized PageRank from specific seed nodes
    const double damping = 0.85;
    const int max_iterations = 20;
    
    // Select seed nodes (e.g., user's bookmarks)
    std::vector<std::string> seed_nodes;
    for (size_t i = 0; i < node_ids_.size() && i < 10; i++) {
        seed_nodes.push_back(node_ids_[i]);
    }
    
    for (auto _ : state) {
        // Standard PageRank (would need personalized version in API)
        auto [status, ranks] = analytics_->pageRank(node_ids_, damping, max_iterations, 1e-6);
        
        if (!status.ok) {
            state.SkipWithError("Personalized PageRank failed");
        }
        
        benchmark::DoNotOptimize(ranks);
    }
    
    state.SetItemsProcessed(state.iterations());
    state.counters["num_seeds"] = seed_nodes.size();
}

BENCHMARK_REGISTER_F(PageRankBenchmarkFixture, PersonalizedPageRank)
    ->Args({1000, 5})
    ->Args({10000, 5})
    ->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmark: Top-K PageRank Results
// ============================================================================

BENCHMARK_DEFINE_F(PageRankBenchmarkFixture, TopKPageRank)(benchmark::State& state) {
    const double damping = 0.85;
    const int max_iterations = 20;
    const int k = state.range(2); // Top-K to extract
    
    for (auto _ : state) {
        auto [status, ranks] = analytics_->pageRank(node_ids_, damping, max_iterations, 1e-6);
        
        if (!status.ok) {
            state.SkipWithError("PageRank failed");
        }
        
        // Extract top-K
        std::vector<std::pair<std::string, double>> rank_vec(ranks.begin(), ranks.end());
        std::partial_sort(
            rank_vec.begin(),
            rank_vec.begin() + std::min(k, static_cast<int>(rank_vec.size())),
            rank_vec.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; }
        );
        
        benchmark::DoNotOptimize(rank_vec);
    }
    
    state.SetItemsProcessed(state.iterations());
    state.counters["k"] = k;
}

BENCHMARK_REGISTER_F(PageRankBenchmarkFixture, TopKPageRank)
    ->Args({10000, 5, 10})    // Top-10
    ->Args({10000, 5, 100})   // Top-100
    ->Args({10000, 5, 1000})  // Top-1000
    ->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmark: PageRank Throughput
// ============================================================================

BENCHMARK_DEFINE_F(PageRankBenchmarkFixture, PageRankThroughput)(benchmark::State& state) {
    const double damping = 0.85;
    const int max_iterations = 20;
    
    int64_t total_nodes_processed = 0;
    
    for (auto _ : state) {
        auto [status, ranks] = analytics_->pageRank(node_ids_, damping, max_iterations, 1e-6);
        
        if (!status.ok) {
            state.SkipWithError("PageRank failed");
        }
        
        total_nodes_processed += node_ids_.size();
        benchmark::DoNotOptimize(ranks);
    }
    
    state.SetItemsProcessed(total_nodes_processed);
    state.counters["nodes_per_sec"] = benchmark::Counter(
        total_nodes_processed, benchmark::Counter::kIsRate);
}

BENCHMARK_REGISTER_F(PageRankBenchmarkFixture, PageRankThroughput)
    ->Args({1000, 5})
    ->Args({10000, 5})
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
