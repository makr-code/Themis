#include <gtest/gtest.h>
#include "storage/rocksdb_wrapper.h"
#include "index/graph_index.h"
#include "index/property_graph.h"
#include "index/secondary_index.h"
#include "query/query_engine.h"
#include "storage/base_entity.h"
#include <filesystem>

using namespace themis;

class GraphTypeFilteringTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::remove_all(test_db_path_);
        
        RocksDBWrapper::Config config;
        config.db_path = test_db_path_;
        config.memtable_size_mb = 64;
        config.block_cache_size_mb = 256;
        config.max_background_jobs = 2;
        config.compression_default = "lz4";
        config.compression_bottommost = "zstd";
        
        db_ = std::make_unique<RocksDBWrapper>(config);
        ASSERT_TRUE(db_->open());
        
        graphIdx_ = std::make_unique<GraphIndexManager>(*db_);
        pgm_ = std::make_unique<PropertyGraphManager>(*db_);
        
        // Create a dummy SecondaryIndexManager for QueryEngine
        secIdx_ = std::make_unique<SecondaryIndexManager>(*db_);
        queryEngine_ = std::make_unique<QueryEngine>(*db_, *secIdx_, *graphIdx_);
    }

    void TearDown() override {
        queryEngine_.reset();
        secIdx_.reset();
        pgm_.reset();
        graphIdx_.reset();
        db_.reset();
        std::filesystem::remove_all(test_db_path_);
    }

    const std::string test_db_path_ = "./__test_graph_type_filtering__";
    std::unique_ptr<RocksDBWrapper> db_;
    std::unique_ptr<GraphIndexManager> graphIdx_;
    std::unique_ptr<PropertyGraphManager> pgm_;
    std::unique_ptr<SecondaryIndexManager> secIdx_;
    std::unique_ptr<QueryEngine> queryEngine_;
};

TEST_F(GraphTypeFilteringTest, BFS_WithTypeFilter_OnlyTraversesMatchingEdges) {
    // Create a social graph with mixed edge types
    // alice -FOLLOWS-> bob -LIKES-> charlie
    // alice -LIKES-> dave
    // bob -FOLLOWS-> dave

    // Add nodes
    BaseEntity alice("alice");
    alice.setField("id", "alice");
    alice.setField("name", "Alice");
    alice.setField("_labels", "Person");
    ASSERT_TRUE(pgm_->addNode(alice, "social").ok);

    BaseEntity bob("bob");
    bob.setField("id", "bob");
    bob.setField("name", "Bob");
    bob.setField("_labels", "Person");
    ASSERT_TRUE(pgm_->addNode(bob, "social").ok);

    BaseEntity charlie("charlie");
    charlie.setField("id", "charlie");
    charlie.setField("name", "Charlie");
    charlie.setField("_labels", "Person");
    ASSERT_TRUE(pgm_->addNode(charlie, "social").ok);

    BaseEntity dave("dave");
    dave.setField("id", "dave");
    dave.setField("name", "Dave");
    dave.setField("_labels", "Person");
    ASSERT_TRUE(pgm_->addNode(dave, "social").ok);

    // Add edges with types
    BaseEntity follows1("follows1");
    follows1.setField("id", "follows1");
    follows1.setField("_from", "alice");
    follows1.setField("_to", "bob");
    follows1.setField("_type", "FOLLOWS");
    ASSERT_TRUE(pgm_->addEdge(follows1, "social").ok);

    BaseEntity likes1("likes1");
    likes1.setField("id", "likes1");
    likes1.setField("_from", "bob");
    likes1.setField("_to", "charlie");
    likes1.setField("_type", "LIKES");
    ASSERT_TRUE(pgm_->addEdge(likes1, "social").ok);

    BaseEntity likes2("likes2");
    likes2.setField("id", "likes2");
    likes2.setField("_from", "alice");
    likes2.setField("_to", "dave");
    likes2.setField("_type", "LIKES");
    ASSERT_TRUE(pgm_->addEdge(likes2, "social").ok);

    BaseEntity follows2("follows2");
    follows2.setField("id", "follows2");
    follows2.setField("_from", "bob");
    follows2.setField("_to", "dave");
    follows2.setField("_type", "FOLLOWS");
    ASSERT_TRUE(pgm_->addEdge(follows2, "social").ok);

    // Rebuild topology to load edges into memory
    ASSERT_TRUE(graphIdx_->rebuildTopology().ok);

    // BFS from alice with FOLLOWS filter (should reach bob and dave via bob)
    auto [st1, followsNodes] = graphIdx_->bfs("alice", 3, "FOLLOWS", "social");
    ASSERT_TRUE(st1.ok);
    EXPECT_EQ(followsNodes.size(), 3); // alice, bob, dave
    EXPECT_TRUE(std::find(followsNodes.begin(), followsNodes.end(), "alice") != followsNodes.end());
    EXPECT_TRUE(std::find(followsNodes.begin(), followsNodes.end(), "bob") != followsNodes.end());
    EXPECT_TRUE(std::find(followsNodes.begin(), followsNodes.end(), "dave") != followsNodes.end());
    EXPECT_FALSE(std::find(followsNodes.begin(), followsNodes.end(), "charlie") != followsNodes.end());

    // BFS from alice with LIKES filter (should reach dave only)
    auto [st2, likesNodes] = graphIdx_->bfs("alice", 3, "LIKES", "social");
    ASSERT_TRUE(st2.ok);
    EXPECT_EQ(likesNodes.size(), 2); // alice, dave
    EXPECT_TRUE(std::find(likesNodes.begin(), likesNodes.end(), "alice") != likesNodes.end());
    EXPECT_TRUE(std::find(likesNodes.begin(), likesNodes.end(), "dave") != likesNodes.end());
    EXPECT_FALSE(std::find(likesNodes.begin(), likesNodes.end(), "bob") != likesNodes.end());
    EXPECT_FALSE(std::find(likesNodes.begin(), likesNodes.end(), "charlie") != likesNodes.end());

    // BFS from alice without filter (should reach all)
    auto [st3, allNodes] = graphIdx_->bfs("alice", 3);
    ASSERT_TRUE(st3.ok);
    EXPECT_EQ(allNodes.size(), 4); // alice, bob, charlie, dave
}

TEST_F(GraphTypeFilteringTest, Dijkstra_WithTypeFilter_FindsShortestPathOfType) {
    // Create a graph with multiple paths of different types
    // alice -FOLLOWS-> bob -FOLLOWS-> charlie
    // alice -LIKES-> dave -LIKES-> charlie

    // Add nodes
    for (const auto& pk : {"alice", "bob", "charlie", "dave"}) {
        BaseEntity node(pk);
        node.setField("id", std::string(pk));
        node.setField("name", std::string(pk));
        ASSERT_TRUE(pgm_->addNode(node, "social").ok);
    }

    // Add FOLLOWS path
    BaseEntity follows1("follows1");
    follows1.setField("id", "follows1");
    follows1.setField("_from", "alice");
    follows1.setField("_to", "bob");
    follows1.setField("_type", "FOLLOWS");
    ASSERT_TRUE(pgm_->addEdge(follows1, "social").ok);

    BaseEntity follows2("follows2");
    follows2.setField("id", "follows2");
    follows2.setField("_from", "bob");
    follows2.setField("_to", "charlie");
    follows2.setField("_type", "FOLLOWS");
    ASSERT_TRUE(pgm_->addEdge(follows2, "social").ok);

    // Add LIKES path
    BaseEntity likes1("likes1");
    likes1.setField("id", "likes1");
    likes1.setField("_from", "alice");
    likes1.setField("_to", "dave");
    likes1.setField("_type", "LIKES");
    ASSERT_TRUE(pgm_->addEdge(likes1, "social").ok);

    BaseEntity likes2("likes2");
    likes2.setField("id", "likes2");
    likes2.setField("_from", "dave");
    likes2.setField("_to", "charlie");
    likes2.setField("_type", "LIKES");
    ASSERT_TRUE(pgm_->addEdge(likes2, "social").ok);

    // Rebuild topology to load edges into memory
    ASSERT_TRUE(graphIdx_->rebuildTopology().ok);

    // Find shortest path with FOLLOWS filter
    auto [st1, followsPath] = graphIdx_->dijkstra("alice", "charlie", "FOLLOWS", "social");
    ASSERT_TRUE(st1.ok);
    ASSERT_EQ(followsPath.path.size(), 3); // alice -> bob -> charlie
    EXPECT_EQ(followsPath.path[0], "alice");
    EXPECT_EQ(followsPath.path[1], "bob");
    EXPECT_EQ(followsPath.path[2], "charlie");

    // Find shortest path with LIKES filter
    auto [st2, likesPath] = graphIdx_->dijkstra("alice", "charlie", "LIKES", "social");
    ASSERT_TRUE(st2.ok);
    ASSERT_EQ(likesPath.path.size(), 3); // alice -> dave -> charlie
    EXPECT_EQ(likesPath.path[0], "alice");
    EXPECT_EQ(likesPath.path[1], "dave");
    EXPECT_EQ(likesPath.path[2], "charlie");

    // Find shortest path without filter (should use any edge type)
    auto [st3, anyPath] = graphIdx_->dijkstra("alice", "charlie");
    ASSERT_TRUE(st3.ok);
    EXPECT_EQ(anyPath.path.size(), 3); // Could be either path
}

TEST_F(GraphTypeFilteringTest, RecursivePathQuery_WithTypeFilter_UsesServerSideFiltering) {
    // Create test graph
    for (const auto& pk : {"alice", "bob", "charlie"}) {
        BaseEntity node(pk);
        node.setField("id", std::string(pk));
        node.setField("name", std::string(pk));
        ASSERT_TRUE(pgm_->addNode(node, "social").ok);
    }

    // alice -FOLLOWS-> bob -LIKES-> charlie
    BaseEntity follows1("follows1");
    follows1.setField("id", "follows1");
    follows1.setField("_from", "alice");
    follows1.setField("_to", "bob");
    follows1.setField("_type", "FOLLOWS");
    ASSERT_TRUE(pgm_->addEdge(follows1, "social").ok);

    BaseEntity likes1("likes1");
    likes1.setField("id", "likes1");
    likes1.setField("_from", "bob");
    likes1.setField("_to", "charlie");
    likes1.setField("_type", "LIKES");
    ASSERT_TRUE(pgm_->addEdge(likes1, "social").ok);

    // Rebuild topology to load edges into memory
    ASSERT_TRUE(graphIdx_->rebuildTopology().ok);

    // Query with FOLLOWS filter (should find bob but not charlie)
    RecursivePathQuery q1;
    q1.start_node = "alice";
    q1.edge_type = "FOLLOWS";
    q1.graph_id = "social";
    q1.max_depth = 3;

    auto [st1, paths1] = queryEngine_->executeRecursivePathQuery(q1);
    ASSERT_TRUE(st1.ok);
    // Should reach bob (via FOLLOWS) but not charlie (blocked by LIKES edge)
    EXPECT_EQ(paths1.size(), 1);
    EXPECT_EQ(paths1[0].size(), 2); // alice -> bob
    EXPECT_EQ(paths1[0][0], "alice");
    EXPECT_EQ(paths1[0][1], "bob");

    // Query with LIKES filter (should find charlie via bob)
    RecursivePathQuery q2;
    q2.start_node = "bob";
    q2.edge_type = "LIKES";
    q2.graph_id = "social";
    q2.max_depth = 3;

    auto [st2, paths2] = queryEngine_->executeRecursivePathQuery(q2);
    ASSERT_TRUE(st2.ok);
    EXPECT_EQ(paths2.size(), 1);
    EXPECT_EQ(paths2[0].size(), 2); // bob -> charlie
    EXPECT_EQ(paths2[0][0], "bob");
    EXPECT_EQ(paths2[0][1], "charlie");

    // Query with shortest path and type filter
    RecursivePathQuery q3;
    q3.start_node = "alice";
    q3.end_node = "charlie";
    q3.edge_type = "LIKES";
    q3.graph_id = "social";
    q3.max_depth = 3;

    auto [st3, paths3] = queryEngine_->executeRecursivePathQuery(q3);
    ASSERT_TRUE(st3.ok);
    // Should fail because alice -> charlie requires FOLLOWS then LIKES
    EXPECT_EQ(paths3.size(), 0);

    // Query without type filter (should find charlie)
    RecursivePathQuery q4;
    q4.start_node = "alice";
    q4.end_node = "charlie";
    q4.graph_id = "social";
    q4.max_depth = 3;

    auto [st4, paths4] = queryEngine_->executeRecursivePathQuery(q4);
    ASSERT_TRUE(st4.ok);
    EXPECT_EQ(paths4.size(), 1);
    EXPECT_EQ(paths4[0].size(), 3); // alice -> bob -> charlie
}

TEST_F(GraphTypeFilteringTest, TypeFilter_WithNonexistentType_ReturnsEmpty) {
    // Create simple graph
    BaseEntity alice("alice");
    alice.setField("id", "alice");
    alice.setField("name", "Alice");
    ASSERT_TRUE(pgm_->addNode(alice, "social").ok);

    BaseEntity bob("bob");
    bob.setField("id", "bob");
    bob.setField("name", "Bob");
    ASSERT_TRUE(pgm_->addNode(bob, "social").ok);

    BaseEntity follows1("follows1");
    follows1.setField("id", "follows1");
    follows1.setField("_from", "alice");
    follows1.setField("_to", "bob");
    follows1.setField("_type", "FOLLOWS");
    ASSERT_TRUE(pgm_->addEdge(follows1, "social").ok);

    // Rebuild topology to load edges into memory
    ASSERT_TRUE(graphIdx_->rebuildTopology().ok);

    // Query with nonexistent type
    auto [st, nodes] = graphIdx_->bfs("alice", 3, "NONEXISTENT", "social");
    ASSERT_TRUE(st.ok);
    EXPECT_EQ(nodes.size(), 1); // Only alice (start node)
    EXPECT_EQ(nodes[0], "alice");
}
