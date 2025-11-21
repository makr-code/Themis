// Benchmark: GNN Embedding Generation Performance
// Measures embedding generation for Node2Vec, GraphSAGE, and other GNN models

#include "index/gnn_embeddings.h"
#include "index/property_graph.h"
#include "index/vector_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include <benchmark/benchmark.h>
#include <filesystem>
#include <random>

using namespace themis;

// ============================================================================
// Test Setup
// ============================================================================

class GNNEmbeddingsBenchmarkFixture : public benchmark::Fixture {
public:
    void SetUp(const ::benchmark::State& state) override {
        // Clean up any existing test database
        test_db_path_ = "./data/bench_gnn_embeddings_tmp";
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
        
        // Create managers
        property_graph_ = std::make_unique<PropertyGraphManager>(*db_);
        vector_index_ = std::make_unique<VectorIndexManager>(*db_);
        gnn_manager_ = std::make_unique<GNNEmbeddingManager>(*db_, *property_graph_, *vector_index_);
        
        // Build test graph
        int num_nodes = state.range(0);
        int avg_degree = state.range(1);
        buildSocialGraph(num_nodes, avg_degree);
    }
    
    void TearDown(const ::benchmark::State& /*state*/) override {
        gnn_manager_.reset();
        vector_index_.reset();
        property_graph_.reset();
        db_->close();
        db_.reset();
        
        // Clean up test database
        if (std::filesystem::exists(test_db_path_)) {
            std::filesystem::remove_all(test_db_path_);
        }
    }
    
    void buildSocialGraph(int num_nodes, int avg_degree) {
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> node_dist(0, num_nodes - 1);
        std::normal_distribution<double> age_dist(30.0, 10.0);
        
        // Create nodes (users)
        for (int i = 0; i < num_nodes; i++) {
            std::string node_id = "user_" + std::to_string(i);
            node_ids_.push_back(node_id);
            
            BaseEntity node(node_id);
            node.setField("label", "Person");
            node.setField("name", "User " + std::to_string(i));
            node.setField("age", static_cast<int>(age_dist(rng)));
            node.setField("followers", rng() % 10000);
            
            property_graph_->addVertex("social", node);
        }
        
        // Create edges (follows relationships)
        for (int i = 0; i < num_nodes; i++) {
            int edges_to_add = avg_degree / 2 + (rng() % (avg_degree / 2 + 1));
            
            for (int j = 0; j < edges_to_add; j++) {
                int target = node_dist(rng);
                if (target != i) {
                    std::string edge_id = "follows_" + std::to_string(i) + "_" + std::to_string(target);
                    
                    BaseEntity edge(edge_id);
                    edge.setField("type", "FOLLOWS");
                    edge.setField("since", 2020 + (rng() % 5));
                    
                    property_graph_->addEdge("social", node_ids_[i], node_ids_[target], edge);
                }
            }
        }
    }
    
protected:
    std::string test_db_path_;
    std::unique_ptr<RocksDBWrapper> db_;
    std::unique_ptr<PropertyGraphManager> property_graph_;
    std::unique_ptr<VectorIndexManager> vector_index_;
    std::unique_ptr<GNNEmbeddingManager> gnn_manager_;
    std::vector<std::string> node_ids_;
};

// ============================================================================
// Benchmark: Node Embedding Generation
// ============================================================================

BENCHMARK_DEFINE_F(GNNEmbeddingsBenchmarkFixture, NodeEmbeddingGeneration)(benchmark::State& state) {
    const std::string model_name = "gcn_v1";
    const std::string label = "Person";
    
    for (auto _ : state) {
        auto status = gnn_manager_->generateNodeEmbeddings("social", label, model_name);
        
        if (!status.ok) {
            state.SkipWithError("Node embedding generation failed");
        }
        
        benchmark::DoNotOptimize(status);
    }
    
    state.SetItemsProcessed(state.iterations() * node_ids_.size());
    state.counters["num_nodes"] = node_ids_.size();
}

BENCHMARK_REGISTER_F(GNNEmbeddingsBenchmarkFixture, NodeEmbeddingGeneration)
    ->Args({100, 5})      // Small graph
    ->Args({1000, 5})     // Medium graph
    ->Args({10000, 5})    // Large graph
    ->Args({100, 20})     // Dense small graph
    ->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmark: Batch Embedding Generation
// ============================================================================

BENCHMARK_DEFINE_F(GNNEmbeddingsBenchmarkFixture, BatchEmbeddingGeneration)(benchmark::State& state) {
    const int batch_size = state.range(2);
    const std::string model_name = "graphsage_v1";
    
    for (auto _ : state) {
        // Process in batches
        for (size_t i = 0; i < node_ids_.size(); i += batch_size) {
            size_t end = std::min(i + batch_size, node_ids_.size());
            std::vector<std::string> batch(node_ids_.begin() + i, node_ids_.begin() + end);
            
            auto status = gnn_manager_->generateNodeEmbeddingsBatch("social", batch, model_name);
            
            if (!status.ok) {
                state.SkipWithError("Batch embedding failed");
            }
        }
    }
    
    state.SetItemsProcessed(state.iterations() * node_ids_.size());
    state.counters["batch_size"] = batch_size;
}

BENCHMARK_REGISTER_F(GNNEmbeddingsBenchmarkFixture, BatchEmbeddingGeneration)
    ->Args({1000, 5, 10})    // batch size 10
    ->Args({1000, 5, 50})    // batch size 50
    ->Args({1000, 5, 100})   // batch size 100
    ->Args({1000, 5, 500})   // batch size 500
    ->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmark: Similarity Search
// ============================================================================

BENCHMARK_DEFINE_F(GNNEmbeddingsBenchmarkFixture, SimilaritySearch)(benchmark::State& state) {
    const std::string model_name = "gcn_v1";
    const int k = state.range(2);
    
    // Generate embeddings first
    auto gen_status = gnn_manager_->generateNodeEmbeddings("social", "Person", model_name);
    if (!gen_status.ok) {
        state.SkipWithError("Failed to generate embeddings");
        return;
    }
    
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> node_dist(0, node_ids_.size() - 1);
    
    for (auto _ : state) {
        // Pick random node and find similar
        std::string query_node = node_ids_[node_dist(rng)];
        
        auto [status, similar] = gnn_manager_->findSimilarNodes(query_node, "social", k);
        
        if (!status.ok) {
            state.SkipWithError("Similarity search failed");
        }
        
        benchmark::DoNotOptimize(similar);
    }
    
    state.SetItemsProcessed(state.iterations());
    state.counters["k"] = k;
}

BENCHMARK_REGISTER_F(GNNEmbeddingsBenchmarkFixture, SimilaritySearch)
    ->Args({1000, 5, 10})    // Top-10
    ->Args({1000, 5, 50})    // Top-50
    ->Args({1000, 5, 100})   // Top-100
    ->Unit(benchmark::kMicrosecond);

// ============================================================================
// Benchmark: Incremental Update
// ============================================================================

BENCHMARK_DEFINE_F(GNNEmbeddingsBenchmarkFixture, IncrementalUpdate)(benchmark::State& state) {
    const std::string model_name = "gcn_v1";
    
    // Generate initial embeddings
    auto gen_status = gnn_manager_->generateNodeEmbeddings("social", "Person", model_name);
    if (!gen_status.ok) {
        state.SkipWithError("Failed to generate initial embeddings");
        return;
    }
    
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> node_dist(0, node_ids_.size() - 1);
    
    for (auto _ : state) {
        // Update random node
        std::string node_id = node_ids_[node_dist(rng)];
        
        auto status = gnn_manager_->updateNodeEmbedding(node_id, "social", model_name);
        
        if (!status.ok) {
            state.SkipWithError("Incremental update failed");
        }
    }
    
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(GNNEmbeddingsBenchmarkFixture, IncrementalUpdate)
    ->Args({1000, 5})
    ->Args({10000, 5})
    ->Unit(benchmark::kMicrosecond);

// ============================================================================
// Benchmark: Edge Embedding Generation
// ============================================================================

BENCHMARK_DEFINE_F(GNNEmbeddingsBenchmarkFixture, EdgeEmbeddingGeneration)(benchmark::State& state) {
    const std::string model_name = "gat_v1";
    const std::string edge_type = "FOLLOWS";
    
    for (auto _ : state) {
        auto status = gnn_manager_->generateEdgeEmbeddings("social", edge_type, model_name);
        
        if (!status.ok) {
            state.SkipWithError("Edge embedding generation failed");
        }
        
        benchmark::DoNotOptimize(status);
    }
    
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK_REGISTER_F(GNNEmbeddingsBenchmarkFixture, EdgeEmbeddingGeneration)
    ->Args({1000, 5})
    ->Args({10000, 5})
    ->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmark: Different Embedding Dimensions
// ============================================================================

static void BM_EmbeddingDimensions(benchmark::State& state) {
    // Setup
    std::string test_db_path = "./data/bench_gnn_dim_tmp";
    if (std::filesystem::exists(test_db_path)) {
        std::filesystem::remove_all(test_db_path);
    }
    
    RocksDBWrapper::Config config;
    config.db_path = test_db_path;
    config.memtable_size_mb = 256;
    
    auto db = std::make_unique<RocksDBWrapper>(config);
    db->open();
    
    auto property_graph = std::make_unique<PropertyGraphManager>(*db);
    auto vector_index = std::make_unique<VectorIndexManager>(*db);
    auto gnn_manager = std::make_unique<GNNEmbeddingManager>(*db, *property_graph, *vector_index);
    
    // Build small graph
    const int num_nodes = 1000;
    std::vector<std::string> node_ids;
    
    for (int i = 0; i < num_nodes; i++) {
        std::string node_id = "node_" + std::to_string(i);
        node_ids.push_back(node_id);
        
        BaseEntity node(node_id);
        node.setField("label", "Test");
        property_graph->addVertex("test", node);
    }
    
    const int embedding_dim = state.range(0);
    std::string model_name = "model_dim" + std::to_string(embedding_dim);
    
    for (auto _ : state) {
        auto status = gnn_manager->generateNodeEmbeddings("test", "Test", model_name);
        benchmark::DoNotOptimize(status);
    }
    
    // Cleanup
    gnn_manager.reset();
    vector_index.reset();
    property_graph.reset();
    db->close();
    db.reset();
    
    if (std::filesystem::exists(test_db_path)) {
        std::filesystem::remove_all(test_db_path);
    }
    
    state.SetItemsProcessed(state.iterations() * num_nodes);
    state.counters["dimension"] = embedding_dim;
}

BENCHMARK(BM_EmbeddingDimensions)
    ->Arg(64)
    ->Arg(128)
    ->Arg(256)
    ->Arg(512)
    ->Arg(1024)
    ->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmark: Embedding Generation Throughput
// ============================================================================

BENCHMARK_DEFINE_F(GNNEmbeddingsBenchmarkFixture, EmbeddingThroughput)(benchmark::State& state) {
    const std::string model_name = "gcn_v1";
    
    int64_t total_nodes_processed = 0;
    
    for (auto _ : state) {
        auto status = gnn_manager_->generateNodeEmbeddings("social", "Person", model_name);
        
        if (!status.ok) {
            state.SkipWithError("Embedding generation failed");
        }
        
        total_nodes_processed += node_ids_.size();
    }
    
    state.SetItemsProcessed(total_nodes_processed);
    state.counters["nodes_per_sec"] = benchmark::Counter(
        total_nodes_processed, benchmark::Counter::kIsRate);
}

BENCHMARK_REGISTER_F(GNNEmbeddingsBenchmarkFixture, EmbeddingThroughput)
    ->Args({1000, 5})
    ->Args({10000, 5})
    ->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmark: Model Comparison (Node2Vec vs GraphSAGE)
// ============================================================================

BENCHMARK_DEFINE_F(GNNEmbeddingsBenchmarkFixture, ModelComparison)(benchmark::State& state) {
    std::vector<std::string> models = {"node2vec_v1", "graphsage_v1", "gcn_v1", "gat_v1"};
    int model_idx = state.range(2) % models.size();
    const std::string& model_name = models[model_idx];
    
    for (auto _ : state) {
        auto status = gnn_manager_->generateNodeEmbeddings("social", "Person", model_name);
        
        if (!status.ok) {
            state.SkipWithError("Model generation failed");
        }
    }
    
    state.SetItemsProcessed(state.iterations() * node_ids_.size());
    state.counters["model_type"] = model_idx;
}

BENCHMARK_REGISTER_F(GNNEmbeddingsBenchmarkFixture, ModelComparison)
    ->Args({1000, 5, 0})  // Node2Vec
    ->Args({1000, 5, 1})  // GraphSAGE
    ->Args({1000, 5, 2})  // GCN
    ->Args({1000, 5, 3})  // GAT
    ->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
