// Tests for GNN Embedding Manager

#include <gtest/gtest.h>
#include "index/gnn_embeddings.h"
#include "index/property_graph.h"
#include "index/vector_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include <filesystem>

using namespace themis;

class GNNEmbeddingTest : public ::testing::Test {
protected:
    std::unique_ptr<RocksDBWrapper> db;
    std::unique_ptr<PropertyGraphManager> pgm;
    std::unique_ptr<VectorIndexManager> vim;
    std::unique_ptr<GNNEmbeddingManager> gem;
    std::string testDbPath = "data/themis_gnn_test";
    
    void SetUp() override {
        // Clean up existing test database
        if (std::filesystem::exists(testDbPath)) {
            std::filesystem::remove_all(testDbPath);
        }
        
        // Initialize components
        RocksDBWrapper::Config config;
        config.db_path = testDbPath;
        db = std::make_unique<RocksDBWrapper>(config);
        db->open();
        
        pgm = std::make_unique<PropertyGraphManager>(*db);
        vim = std::make_unique<VectorIndexManager>(*db);
        vim->init("embedding", 64);  // 64-dim embeddings
        
        gem = std::make_unique<GNNEmbeddingManager>(*db, *pgm, *vim);
        
        // Register default model
        gem->registerModel("test_model", "feature_based", 64);
    }
    
    void TearDown() override {
        gem.reset();
        vim.reset();
        pgm.reset();
        db.reset();
        
        if (std::filesystem::exists(testDbPath)) {
            std::filesystem::remove_all(testDbPath);
        }
    }
    
    void createTestGraph() {
        // Create Person nodes with features
        BaseEntity person1("person1");
        person1.setField("id", std::string("person1"));  // Required by PropertyGraphManager
        person1.setField("age", 30);
        person1.setField("score", 85.5);
        
        BaseEntity person2("person2");
        person2.setField("id", std::string("person2"));
        person2.setField("age", 25);
        person2.setField("score", 90.0);
        
        BaseEntity person3("person3");
        person3.setField("id", std::string("person3"));
        person3.setField("age", 35);
        person3.setField("score", 78.0);
        
        // Add nodes to property graph
        pgm->addNode(person1, "g1");
        pgm->addNodeLabel("person1", "Person", "g1");
        
        pgm->addNode(person2, "g1");
        pgm->addNodeLabel("person2", "Person", "g1");
        
        pgm->addNode(person3, "g1");
        pgm->addNodeLabel("person3", "Person", "g1");
        
        // Create relationship edges (with _from/_to fields)
        BaseEntity edge1("edge1");
        edge1.setField("id", std::string("edge1"));  // Required
        edge1.setField("_from", std::string("person1"));
        edge1.setField("_to", std::string("person2"));
        edge1.setField("_type", std::string("knows"));
        
        BaseEntity edge2("edge2");
        edge2.setField("id", std::string("edge2"));
        edge2.setField("_from", std::string("person2"));
        edge2.setField("_to", std::string("person3"));
        edge2.setField("_type", std::string("knows"));
        
        pgm->addEdge(edge1, "g1");
        pgm->addEdge(edge2, "g1");
    }
};

TEST_F(GNNEmbeddingTest, RegisterModel) {
    auto st = gem->registerModel("gnn_model", "GraphSAGE", 128, "{\"layers\": 2}");
    EXPECT_TRUE(st.ok);
    
    auto [st2, models] = gem->listModels();
    EXPECT_TRUE(st2.ok);
    EXPECT_GE(models.size(), 2);  // test_model + gnn_model
    
    auto [st3, modelInfo] = gem->getModelInfo("gnn_model");
    EXPECT_TRUE(st3.ok);
    EXPECT_EQ(modelInfo.name, "gnn_model");
    EXPECT_EQ(modelInfo.type, "GraphSAGE");
    EXPECT_EQ(modelInfo.embedding_dim, 128);
}

TEST_F(GNNEmbeddingTest, GenerateNodeEmbeddings) {
    createTestGraph();
    
    // Generate embeddings for all Person nodes
    auto st = gem->generateNodeEmbeddings("g1", "Person", "test_model");
    EXPECT_TRUE(st.ok);
    
    // Verify embeddings were created
    auto [st2, embInfo] = gem->getNodeEmbedding("person1", "g1", "test_model");
    EXPECT_TRUE(st2.ok);
    EXPECT_EQ(embInfo.entity_id, "person1");
    EXPECT_EQ(embInfo.entity_type, "node");
    EXPECT_EQ(embInfo.model_name, "test_model");
    EXPECT_EQ(embInfo.embedding.size(), 64);  // test_model dim
}

TEST_F(GNNEmbeddingTest, UpdateNodeEmbedding) {
    createTestGraph();
    
    // Update single node embedding
    auto st = gem->updateNodeEmbedding("person1", "g1", "test_model");
    EXPECT_TRUE(st.ok);
    
    // Verify embedding
    auto [st2, embInfo] = gem->getNodeEmbedding("person1", "g1", "test_model");
    EXPECT_TRUE(st2.ok);
    EXPECT_FALSE(embInfo.embedding.empty());
    
    // Verify embedding is normalized
    float norm = 0.0f;
    for (float val : embInfo.embedding) {
        norm += val * val;
    }
    EXPECT_NEAR(std::sqrt(norm), 1.0f, 0.01f);
}

TEST_F(GNNEmbeddingTest, GenerateEdgeEmbeddings) {
    createTestGraph();
    
    // Generate embeddings for all "knows" edges
    auto st = gem->generateEdgeEmbeddings("g1", "knows", "test_model");
    EXPECT_TRUE(st.ok);
    
    // Verify edge embeddings
    auto [st2, edges] = pgm->getEdgesByType("knows", "g1");
    EXPECT_TRUE(st2.ok);
    EXPECT_GE(edges.size(), 2);
    
    if (!edges.empty()) {
        auto [st3, embInfo] = gem->getEdgeEmbedding(edges[0].edgeId, "g1", "test_model");
        EXPECT_TRUE(st3.ok);
        EXPECT_EQ(embInfo.entity_type, "edge");
    }
}

TEST_F(GNNEmbeddingTest, FindSimilarNodes) {
    createTestGraph();
    
    // Generate embeddings
    gem->generateNodeEmbeddings("g1", "Person", "test_model");
    
    // Find similar nodes to person1
    auto [st, similar] = gem->findSimilarNodes("person1", "g1", 2, "test_model");
    EXPECT_TRUE(st.ok);
    
    // Should find person2 and person3 (excluding person1 itself)
    EXPECT_LE(similar.size(), 2);
    
    // Verify similarity scores are in valid range
    for (const auto& res : similar) {
        EXPECT_GE(res.similarity, 0.0f);
        EXPECT_LE(res.similarity, 1.0f);
        EXPECT_NE(res.entity_id, "person1");  // Should not include query node
    }
}

TEST_F(GNNEmbeddingTest, FindSimilarEdges) {
    createTestGraph();
    
    // Generate edge embeddings
    gem->generateEdgeEmbeddings("g1", "knows", "test_model");
    
    // Get first edge
    auto [st1, edges] = pgm->getEdgesByType("knows", "g1");
    ASSERT_TRUE(st1.ok);
    ASSERT_GE(edges.size(), 1);
    
    std::string queryEdgeId = edges[0].edgeId;
    
    // Find similar edges
    auto [st2, similar] = gem->findSimilarEdges(queryEdgeId, "g1", 1, "test_model");
    EXPECT_TRUE(st2.ok);
    
    // Should find other edges (excluding query edge)
    for (const auto& res : similar) {
        EXPECT_NE(res.entity_id, queryEdgeId);
    }
}

TEST_F(GNNEmbeddingTest, GenerateGraphEmbedding) {
    createTestGraph();
    
    // Generate node embeddings first
    gem->generateNodeEmbeddings("g1", "Person", "test_model");
    
    // Generate graph-level embedding with mean pooling
    auto [st, graphEmb] = gem->generateGraphEmbedding("g1", "test_model", "mean");
    EXPECT_TRUE(st.ok);
    EXPECT_EQ(graphEmb.size(), 64);
    
    // Test sum pooling
    auto [st2, graphEmbSum] = gem->generateGraphEmbedding("g1", "test_model", "sum");
    EXPECT_TRUE(st2.ok);
    
    // Sum should be larger than mean (3 nodes)
    float sumNorm = 0.0f, meanNorm = 0.0f;
    for (size_t i = 0; i < graphEmb.size(); ++i) {
        sumNorm += graphEmbSum[i] * graphEmbSum[i];
        meanNorm += graphEmb[i] * graphEmb[i];
    }
    EXPECT_GT(std::sqrt(sumNorm), std::sqrt(meanNorm));
}

TEST_F(GNNEmbeddingTest, BatchOperations) {
    createTestGraph();
    
    // Generate embeddings in batch
    std::vector<std::string> node_pks = {"person1", "person2", "person3"};
    auto st = gem->generateNodeEmbeddingsBatch(node_pks, "g1", "test_model", 2);
    EXPECT_TRUE(st.ok);
    
    // Verify all embeddings created
    for (const auto& pk : node_pks) {
        auto [st2, embInfo] = gem->getNodeEmbedding(pk, "g1", "test_model");
        EXPECT_TRUE(st2.ok) << "Failed for node: " << pk;
    }
}

TEST_F(GNNEmbeddingTest, GetStats) {
    createTestGraph();
    
    // Generate embeddings
    gem->generateNodeEmbeddings("g1", "Person", "test_model");
    gem->generateEdgeEmbeddings("g1", "knows", "test_model");
    
    // Get stats
    auto [st, stats] = gem->getStats();
    EXPECT_TRUE(st.ok);
    EXPECT_EQ(stats.total_node_embeddings, 3);
    EXPECT_GE(stats.total_edge_embeddings, 2);
    EXPECT_GT(stats.embeddings_per_model["test_model"], 0);
    EXPECT_GT(stats.embeddings_per_graph["g1"], 0);
}

TEST_F(GNNEmbeddingTest, MultiGraphIsolation) {
    // Create nodes in two different graphs
    BaseEntity node1("node1");
    node1.setField("id", std::string("node1"));
    node1.setField("value", 100);
    BaseEntity node2("node2");
    node2.setField("id", std::string("node2"));
    node2.setField("value", 200);
    
    pgm->addNode(node1, "g1");
    pgm->addNodeLabel("node1", "Type1", "g1");
    
    pgm->addNode(node2, "g2");
    pgm->addNodeLabel("node2", "Type1", "g2");
    
    // Generate embeddings for both graphs
    gem->updateNodeEmbedding("node1", "g1", "test_model");
    gem->updateNodeEmbedding("node2", "g2", "test_model");
    
    // Verify isolation: similar search in g1 should not find node2
    auto [st, similar] = gem->findSimilarNodes("node1", "g1", 10, "test_model");
    EXPECT_TRUE(st.ok);
    
    for (const auto& res : similar) {
        EXPECT_EQ(res.graph_id, "g1");
        EXPECT_NE(res.entity_id, "node2");  // node2 is in g2, not g1
    }
}

TEST_F(GNNEmbeddingTest, FeatureExtraction) {
    // Create node with various field types
    BaseEntity node("test_node");
    node.setField("id", std::string("test_node"));
    node.setField("int_field", 42);
    node.setField("double_field", 3.14);
    node.setField("string_field", "test");
    
    pgm->addNode(node, "g1");
    pgm->addNodeLabel("test_node", "Test", "g1");
    
    // Generate embedding with specific feature fields
    std::vector<std::string> features = {"int_field", "double_field"};
    auto st = gem->updateNodeEmbedding("test_node", "g1", "test_model", features);
    EXPECT_TRUE(st.ok);
    
    // Verify embedding was created
    auto [st2, embInfo] = gem->getNodeEmbedding("test_node", "g1", "test_model");
    EXPECT_TRUE(st2.ok);
    EXPECT_FALSE(embInfo.embedding.empty());
}

TEST_F(GNNEmbeddingTest, MultiModelSupport) {
    createTestGraph();
    
    // Register multiple models
    gem->registerModel("model_64", "feature", 64);
    gem->registerModel("model_128", "feature", 128);
    
    // Generate embeddings with different models
    gem->updateNodeEmbedding("person1", "g1", "model_64");
    gem->updateNodeEmbedding("person1", "g1", "model_128");
    
    // Verify different embeddings exist
    auto [st1, emb64] = gem->getNodeEmbedding("person1", "g1", "model_64");
    auto [st2, emb128] = gem->getNodeEmbedding("person1", "g1", "model_128");
    
    EXPECT_TRUE(st1.ok);
    EXPECT_TRUE(st2.ok);
    EXPECT_EQ(emb64.embedding.size(), 64);
    EXPECT_EQ(emb128.embedding.size(), 128);
}

TEST_F(GNNEmbeddingTest, ErrorHandling) {
    // Test non-existent node
    auto st1 = gem->updateNodeEmbedding("nonexistent", "g1", "test_model");
    EXPECT_FALSE(st1.ok);
    
    // Test non-registered model
    createTestGraph();
    auto st2 = gem->updateNodeEmbedding("person1", "g1", "nonexistent_model");
    EXPECT_FALSE(st2.ok);
    
    // Test getting non-existent embedding
    auto [st3, embInfo] = gem->getNodeEmbedding("person1", "g1", "test_model");
    EXPECT_FALSE(st3.ok);  // No embedding generated yet
}
