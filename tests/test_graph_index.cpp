#include <gtest/gtest.h>
#include "index/graph_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include <filesystem>

namespace fs = std::filesystem;

class GraphIndexTest : public ::testing::Test {
protected:
    void SetUp() override {
    test_db_path_ = "./data/themis_graph_index_test";
        fs::remove_all(test_db_path_);
        
        themis::RocksDBWrapper::Config config;
        config.db_path = test_db_path_;
        config.memtable_size_mb = 64;
        config.block_cache_size_mb = 256;
        config.max_background_jobs = 2;
        config.compression_default = "lz4";
        config.compression_bottommost = "zstd";
        
        db_ = std::make_unique<themis::RocksDBWrapper>(config);
        ASSERT_TRUE(db_->open());
        graph_mgr_ = std::make_unique<themis::GraphIndexManager>(*db_);
    }

    void TearDown() override {
        graph_mgr_.reset();
        db_.reset();
        fs::remove_all(test_db_path_);
    }

    std::string test_db_path_;
    std::unique_ptr<themis::RocksDBWrapper> db_;
    std::unique_ptr<themis::GraphIndexManager> graph_mgr_;
};

TEST_F(GraphIndexTest, AddEdge_CreatesOutdexAndIndex) {
    themis::BaseEntity edge("edge1");
    edge.setField("id", "edge1");
    edge.setField("_from", "user1");
    edge.setField("_to", "user2");

    auto st = graph_mgr_->addEdge(edge);
    ASSERT_TRUE(st.ok) << st.message;

    // Check outdex: graph:out:user1:edge1 -> user2
    auto [st1, outNeighbors] = graph_mgr_->outNeighbors("user1");
    ASSERT_TRUE(st1.ok) << st1.message;
    ASSERT_EQ(outNeighbors.size(), 1u);
    EXPECT_EQ(outNeighbors[0], "user2");

    // Check index: graph:in:user2:edge1 -> user1
    auto [st2, inNeighbors] = graph_mgr_->inNeighbors("user2");
    ASSERT_TRUE(st2.ok) << st2.message;
    ASSERT_EQ(inNeighbors.size(), 1u);
    EXPECT_EQ(inNeighbors[0], "user1");
}

TEST_F(GraphIndexTest, DeleteEdge_RemovesIndices) {
    themis::BaseEntity edge("edge1");
    edge.setField("id", "edge1");
    edge.setField("_from", "user1");
    edge.setField("_to", "user2");
    graph_mgr_->addEdge(edge);

    auto st = graph_mgr_->deleteEdge("edge1");
    ASSERT_TRUE(st.ok) << st.message;

    // Verify indices are removed
    auto [st1, outNeighbors] = graph_mgr_->outNeighbors("user1");
    ASSERT_TRUE(st1.ok);
    EXPECT_EQ(outNeighbors.size(), 0u);

    auto [st2, inNeighbors] = graph_mgr_->inNeighbors("user2");
    ASSERT_TRUE(st2.ok);
    EXPECT_EQ(inNeighbors.size(), 0u);
}

TEST_F(GraphIndexTest, MultipleEdges_OutNeighbors) {
    themis::BaseEntity e1("edge1");
    e1.setField("id", "edge1");
    e1.setField("_from", "user1");
    e1.setField("_to", "user2");
    graph_mgr_->addEdge(e1);

    themis::BaseEntity e2("edge2");
    e2.setField("id", "edge2");
    e2.setField("_from", "user1");
    e2.setField("_to", "user3");
    graph_mgr_->addEdge(e2);

    auto [st, neighbors] = graph_mgr_->outNeighbors("user1");
    ASSERT_TRUE(st.ok);
    ASSERT_EQ(neighbors.size(), 2u);
    EXPECT_TRUE(std::find(neighbors.begin(), neighbors.end(), "user2") != neighbors.end());
    EXPECT_TRUE(std::find(neighbors.begin(), neighbors.end(), "user3") != neighbors.end());
}

TEST_F(GraphIndexTest, BFS_SingleLevel) {
    // user1 -> user2, user3
    themis::BaseEntity e1("edge1");
    e1.setField("id", "edge1");
    e1.setField("_from", "user1");
    e1.setField("_to", "user2");
    graph_mgr_->addEdge(e1);

    themis::BaseEntity e2("edge2");
    e2.setField("id", "edge2");
    e2.setField("_from", "user1");
    e2.setField("_to", "user3");
    graph_mgr_->addEdge(e2);

    auto [st, order] = graph_mgr_->bfs("user1", 1);
    ASSERT_TRUE(st.ok) << st.message;
    ASSERT_EQ(order.size(), 3u); // user1, user2, user3
    EXPECT_EQ(order[0], "user1");
    EXPECT_TRUE(std::find(order.begin(), order.end(), "user2") != order.end());
    EXPECT_TRUE(std::find(order.begin(), order.end(), "user3") != order.end());
}

TEST_F(GraphIndexTest, BFS_TwoLevels) {
    // user1 -> user2 -> user3
    themis::BaseEntity e1("edge1");
    e1.setField("id", "edge1");
    e1.setField("_from", "user1");
    e1.setField("_to", "user2");
    graph_mgr_->addEdge(e1);

    themis::BaseEntity e2("edge2");
    e2.setField("id", "edge2");
    e2.setField("_from", "user2");
    e2.setField("_to", "user3");
    graph_mgr_->addEdge(e2);

    auto [st, order] = graph_mgr_->bfs("user1", 2);
    ASSERT_TRUE(st.ok);
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], "user1");
    EXPECT_EQ(order[1], "user2");
    EXPECT_EQ(order[2], "user3");
}

TEST_F(GraphIndexTest, BFS_CycleHandling) {
    // user1 -> user2 -> user3 -> user1 (cycle)
    themis::BaseEntity e1("edge1");
    e1.setField("id", "edge1");
    e1.setField("_from", "user1");
    e1.setField("_to", "user2");
    graph_mgr_->addEdge(e1);

    themis::BaseEntity e2("edge2");
    e2.setField("id", "edge2");
    e2.setField("_from", "user2");
    e2.setField("_to", "user3");
    graph_mgr_->addEdge(e2);

    themis::BaseEntity e3("edge3");
    e3.setField("id", "edge3");
    e3.setField("_from", "user3");
    e3.setField("_to", "user1");
    graph_mgr_->addEdge(e3);

    auto [st, order] = graph_mgr_->bfs("user1", 5);
    ASSERT_TRUE(st.ok);
    // Should visit each node exactly once despite cycle
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], "user1");
}

// ────────────────────────────────────────────────────────────────────────────
// In-Memory Topology Tests
// ────────────────────────────────────────────────────────────────────────────

TEST_F(GraphIndexTest, RebuildTopology_LoadsFromRocksDB) {
    // Create some edges
    themis::BaseEntity e1("edge1");
    e1.setField("id", "edge1");
    e1.setField("_from", "A");
    e1.setField("_to", "B");
    graph_mgr_->addEdge(e1);

    themis::BaseEntity e2("edge2");
    e2.setField("id", "edge2");
    e2.setField("_from", "A");
    e2.setField("_to", "C");
    graph_mgr_->addEdge(e2);

    themis::BaseEntity e3("edge3");
    e3.setField("id", "edge3");
    e3.setField("_from", "B");
    e3.setField("_to", "C");
    graph_mgr_->addEdge(e3);

    // Rebuild topology from RocksDB
    auto st = graph_mgr_->rebuildTopology();
    ASSERT_TRUE(st.ok) << st.message;

    // Verify topology stats
    EXPECT_EQ(graph_mgr_->getTopologyNodeCount(), 3u); // A, B, C
    EXPECT_EQ(graph_mgr_->getTopologyEdgeCount(), 3u); // edge1, edge2, edge3
}

TEST_F(GraphIndexTest, InMemoryTopology_OutNeighbors) {
    // Add edges
    themis::BaseEntity e1("edge1");
    e1.setField("id", "edge1");
    e1.setField("_from", "A");
    e1.setField("_to", "B");
    graph_mgr_->addEdge(e1);

    themis::BaseEntity e2("edge2");
    e2.setField("id", "edge2");
    e2.setField("_from", "A");
    e2.setField("_to", "C");
    graph_mgr_->addEdge(e2);

    // Rebuild topology
    auto st = graph_mgr_->rebuildTopology();
    ASSERT_TRUE(st.ok);

    // Query neighbors using in-memory topology (should be O(1))
    auto [st1, neighbors] = graph_mgr_->outNeighbors("A");
    ASSERT_TRUE(st1.ok);
    ASSERT_EQ(neighbors.size(), 2u);
    
    // Order may vary, so check both are present
    EXPECT_TRUE(std::find(neighbors.begin(), neighbors.end(), "B") != neighbors.end());
    EXPECT_TRUE(std::find(neighbors.begin(), neighbors.end(), "C") != neighbors.end());
}

TEST_F(GraphIndexTest, InMemoryTopology_InNeighbors) {
    // Add edges
    themis::BaseEntity e1("edge1");
    e1.setField("id", "edge1");
    e1.setField("_from", "A");
    e1.setField("_to", "C");
    graph_mgr_->addEdge(e1);

    themis::BaseEntity e2("edge2");
    e2.setField("id", "edge2");
    e2.setField("_from", "B");
    e2.setField("_to", "C");
    graph_mgr_->addEdge(e2);

    // Rebuild topology
    auto st = graph_mgr_->rebuildTopology();
    ASSERT_TRUE(st.ok);

    // Query incoming neighbors
    auto [st1, inNeighbors] = graph_mgr_->inNeighbors("C");
    ASSERT_TRUE(st1.ok);
    ASSERT_EQ(inNeighbors.size(), 2u);
    
    EXPECT_TRUE(std::find(inNeighbors.begin(), inNeighbors.end(), "A") != inNeighbors.end());
    EXPECT_TRUE(std::find(inNeighbors.begin(), inNeighbors.end(), "B") != inNeighbors.end());
}

TEST_F(GraphIndexTest, InMemoryTopology_BFS_Performance) {
    // Create a larger graph to test BFS with in-memory topology
    // Chain: 1->2->3->4->5 and 1->6
    for (int i = 1; i <= 5; ++i) {
        themis::BaseEntity edge(std::string("edge") + std::to_string(i));
        edge.setField("id", std::string("edge") + std::to_string(i));
        edge.setField("_from", std::string("node") + std::to_string(i));
        edge.setField("_to", std::string("node") + std::to_string(i + 1));
        graph_mgr_->addEdge(edge);
    }
    
    themis::BaseEntity e6("edge6");
    e6.setField("id", "edge6");
    e6.setField("_from", "node1");
    e6.setField("_to", "node6");
    graph_mgr_->addEdge(e6);

    // Rebuild topology
    auto st = graph_mgr_->rebuildTopology();
    ASSERT_TRUE(st.ok);

    // BFS from node1 with max depth 3
    auto [st1, order] = graph_mgr_->bfs("node1", 3);
    ASSERT_TRUE(st1.ok);
    
    // Should visit: node1, node2, node6 (depth 1), node3 (depth 2), node4 (depth 3)
    EXPECT_EQ(order.size(), 5u);
    EXPECT_EQ(order[0], "node1");
}

TEST_F(GraphIndexTest, InMemoryTopology_UpdateAfterDelete) {
    // Add edges
    themis::BaseEntity e1("edge1");
    e1.setField("id", "edge1");
    e1.setField("_from", "A");
    e1.setField("_to", "B");
    graph_mgr_->addEdge(e1);

    themis::BaseEntity e2("edge2");
    e2.setField("id", "edge2");
    e2.setField("_from", "A");
    e2.setField("_to", "C");
    graph_mgr_->addEdge(e2);

    // Rebuild topology
    auto st = graph_mgr_->rebuildTopology();
    ASSERT_TRUE(st.ok);
    EXPECT_EQ(graph_mgr_->getTopologyEdgeCount(), 2u);

    // Delete one edge
    auto st2 = graph_mgr_->deleteEdge("edge1");
    ASSERT_TRUE(st2.ok);

    // Topology should be updated automatically
    EXPECT_EQ(graph_mgr_->getTopologyEdgeCount(), 1u);
    
    auto [st3, neighbors] = graph_mgr_->outNeighbors("A");
    ASSERT_TRUE(st3.ok);
    ASSERT_EQ(neighbors.size(), 1u);
    EXPECT_EQ(neighbors[0], "C");
}

// ────────────────────────────────────────────────────────────────────────────
// Shortest-Path Tests (Dijkstra & A*)
// ────────────────────────────────────────────────────────────────────────────

TEST_F(GraphIndexTest, Dijkstra_SimpleUnweightedPath) {
    // Graph: A -> B -> C -> D
    themis::BaseEntity e1("edge1");
    e1.setField("id", "edge1");
    e1.setField("_from", "A");
    e1.setField("_to", "B");
    graph_mgr_->addEdge(e1);

    themis::BaseEntity e2("edge2");
    e2.setField("id", "edge2");
    e2.setField("_from", "B");
    e2.setField("_to", "C");
    graph_mgr_->addEdge(e2);

    themis::BaseEntity e3("edge3");
    e3.setField("id", "edge3");
    e3.setField("_from", "C");
    e3.setField("_to", "D");
    graph_mgr_->addEdge(e3);

    auto [st, result] = graph_mgr_->dijkstra("A", "D");
    ASSERT_TRUE(st.ok) << st.message;
    
    ASSERT_EQ(result.path.size(), 4u);
    EXPECT_EQ(result.path[0], "A");
    EXPECT_EQ(result.path[1], "B");
    EXPECT_EQ(result.path[2], "C");
    EXPECT_EQ(result.path[3], "D");
    EXPECT_DOUBLE_EQ(result.totalCost, 3.0); // 3 Kanten mit Default-Weight 1.0
}

TEST_F(GraphIndexTest, Dijkstra_WeightedPath) {
    // Graph mit Gewichten:
    // A --(5)--> B --(1)--> D
    // A --(2)--> C --(2)--> D
    // Kürzester Pfad: A -> C -> D (cost = 4)
    
    themis::BaseEntity e1("edge1");
    e1.setField("id", "edge1");
    e1.setField("_from", "A");
    e1.setField("_to", "B");
    e1.setField("_weight", 5.0);
    graph_mgr_->addEdge(e1);

    themis::BaseEntity e2("edge2");
    e2.setField("id", "edge2");
    e2.setField("_from", "B");
    e2.setField("_to", "D");
    e2.setField("_weight", 1.0);
    graph_mgr_->addEdge(e2);

    themis::BaseEntity e3("edge3");
    e3.setField("id", "edge3");
    e3.setField("_from", "A");
    e3.setField("_to", "C");
    e3.setField("_weight", 2.0);
    graph_mgr_->addEdge(e3);

    themis::BaseEntity e4("edge4");
    e4.setField("id", "edge4");
    e4.setField("_from", "C");
    e4.setField("_to", "D");
    e4.setField("_weight", 2.0);
    graph_mgr_->addEdge(e4);

    auto [st, result] = graph_mgr_->dijkstra("A", "D");
    ASSERT_TRUE(st.ok) << st.message;
    
    ASSERT_EQ(result.path.size(), 3u);
    EXPECT_EQ(result.path[0], "A");
    EXPECT_EQ(result.path[1], "C");
    EXPECT_EQ(result.path[2], "D");
    EXPECT_DOUBLE_EQ(result.totalCost, 4.0);
}

TEST_F(GraphIndexTest, Dijkstra_NoPathExists) {
    // Zwei getrennte Komponenten: A -> B, C -> D
    themis::BaseEntity e1("edge1");
    e1.setField("id", "edge1");
    e1.setField("_from", "A");
    e1.setField("_to", "B");
    graph_mgr_->addEdge(e1);

    themis::BaseEntity e2("edge2");
    e2.setField("id", "edge2");
    e2.setField("_from", "C");
    e2.setField("_to", "D");
    graph_mgr_->addEdge(e2);

    auto [st, result] = graph_mgr_->dijkstra("A", "D");
    EXPECT_FALSE(st.ok);
    EXPECT_TRUE(st.message.find("Kein Pfad") != std::string::npos);
}

TEST_F(GraphIndexTest, AStar_WithHeuristic) {
    // Graph: A -> B -> D, A -> C -> D
    // Heuristik bevorzugt B-Pfad
    themis::BaseEntity e1("edge1");
    e1.setField("id", "edge1");
    e1.setField("_from", "A");
    e1.setField("_to", "B");
    e1.setField("_weight", 1.0);
    graph_mgr_->addEdge(e1);

    themis::BaseEntity e2("edge2");
    e2.setField("id", "edge2");
    e2.setField("_from", "B");
    e2.setField("_to", "D");
    e2.setField("_weight", 1.0);
    graph_mgr_->addEdge(e2);

    themis::BaseEntity e3("edge3");
    e3.setField("id", "edge3");
    e3.setField("_from", "A");
    e3.setField("_to", "C");
    e3.setField("_weight", 1.0);
    graph_mgr_->addEdge(e3);

    themis::BaseEntity e4("edge4");
    e4.setField("id", "edge4");
    e4.setField("_from", "C");
    e4.setField("_to", "D");
    e4.setField("_weight", 1.0);
    graph_mgr_->addEdge(e4);

    // Heuristik (admissible): konstant 0 = entspricht Dijkstra
    auto heuristic = [](const std::string&) { return 0.0; };

    auto [st, result] = graph_mgr_->aStar("A", "D", heuristic);
    ASSERT_TRUE(st.ok) << st.message;
    
    ASSERT_EQ(result.path.size(), 3u);
    EXPECT_EQ(result.path[0], "A");
    EXPECT_EQ(result.path[3 - 1], "D");
    EXPECT_DOUBLE_EQ(result.totalCost, 2.0);
}

TEST_F(GraphIndexTest, AStar_WithoutHeuristic_FallsToDijkstra) {
    // Ohne Heuristik sollte A* = Dijkstra sein
    themis::BaseEntity e1("edge1");
    e1.setField("id", "edge1");
    e1.setField("_from", "A");
    e1.setField("_to", "B");
    e1.setField("_weight", 3.0);
    graph_mgr_->addEdge(e1);

    themis::BaseEntity e2("edge2");
    e2.setField("id", "edge2");
    e2.setField("_from", "A");
    e2.setField("_to", "C");
    e2.setField("_weight", 1.0);
    graph_mgr_->addEdge(e2);

    themis::BaseEntity e3("edge3");
    e3.setField("id", "edge3");
    e3.setField("_from", "C");
    e3.setField("_to", "B");
    e3.setField("_weight", 1.0);
    graph_mgr_->addEdge(e3);

    auto [st, result] = graph_mgr_->aStar("A", "B", nullptr);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Kürzester Pfad: A -> C -> B (cost = 2)
    ASSERT_EQ(result.path.size(), 3u);
    EXPECT_EQ(result.path[0], "A");
    EXPECT_EQ(result.path[1], "C");
    EXPECT_EQ(result.path[2], "B");
    EXPECT_DOUBLE_EQ(result.totalCost, 2.0);
}

TEST_F(GraphIndexTest, Dijkstra_WithInMemoryTopology) {
    // Teste Dijkstra mit aktivierter In-Memory-Topologie
    themis::BaseEntity e1("edge1");
    e1.setField("id", "edge1");
    e1.setField("_from", "A");
    e1.setField("_to", "B");
    e1.setField("_weight", 1.0);
    graph_mgr_->addEdge(e1);

    themis::BaseEntity e2("edge2");
    e2.setField("id", "edge2");
    e2.setField("_from", "B");
    e2.setField("_to", "C");
    e2.setField("_weight", 2.0);
    graph_mgr_->addEdge(e2);

    // Activate in-memory topology
    auto st = graph_mgr_->rebuildTopology();
    ASSERT_TRUE(st.ok);

    auto [st1, result] = graph_mgr_->dijkstra("A", "C");
    ASSERT_TRUE(st1.ok) << st1.message;
    
    ASSERT_EQ(result.path.size(), 3u);
    EXPECT_EQ(result.path[0], "A");
    EXPECT_EQ(result.path[1], "B");
    EXPECT_EQ(result.path[2], "C");
    EXPECT_DOUBLE_EQ(result.totalCost, 3.0);
}

