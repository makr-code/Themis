#include <gtest/gtest.h>
#include "index/graph_analytics.h"
#include "index/graph_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include <filesystem>
#include <cmath>

using namespace themis;

class GraphAnalyticsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary database directory
        testDbPath_ = std::filesystem::temp_directory_path() / "test_graph_analytics_db";
        std::filesystem::remove_all(testDbPath_);
        std::filesystem::create_directories(testDbPath_);

        // Initialize database
        RocksDBWrapper::Config cfg;
        cfg.db_path = testDbPath_.string();
        cfg.memtable_size_mb = 64;
        cfg.block_cache_size_mb = 256;
        
        db_ = std::make_unique<RocksDBWrapper>(cfg);
        ASSERT_TRUE(db_->open());

        graphMgr_ = std::make_unique<GraphIndexManager>(*db_);
        analytics_ = std::make_unique<GraphAnalytics>(*graphMgr_);
    }

    void TearDown() override {
        analytics_.reset();
        graphMgr_.reset();
        db_.reset();
        std::filesystem::remove_all(testDbPath_);
    }

    // Helper: Create edge entity
    BaseEntity createEdge(const std::string& id, const std::string& from, const std::string& to) {
        BaseEntity edge;
        edge.setPrimaryKey(id);
        edge.setField("id", Value(id));
        edge.setField("_from", Value(from));
        edge.setField("_to", Value(to));
        return edge;
    }

    // Helper: Build a simple directed graph
    void buildSimpleGraph() {
        // Graph structure:
        //   A -> B -> C
        //   A -> C
        //   B -> D
        //   C -> D
        //
        // Nodes: A, B, C, D
        // Expected PageRank (approximate): A > C > B > D
        
        auto e1 = createEdge("e1", "A", "B");
        auto e2 = createEdge("e2", "A", "C");
        auto e3 = createEdge("e3", "B", "C");
        auto e4 = createEdge("e4", "B", "D");
        auto e5 = createEdge("e5", "C", "D");

        ASSERT_TRUE(graphMgr_->addEdge(e1).ok);
        ASSERT_TRUE(graphMgr_->addEdge(e2).ok);
        ASSERT_TRUE(graphMgr_->addEdge(e3).ok);
        ASSERT_TRUE(graphMgr_->addEdge(e4).ok);
        ASSERT_TRUE(graphMgr_->addEdge(e5).ok);

        nodes_ = {"A", "B", "C", "D"};
    }

    // Helper: Build a hub-and-spoke graph
    void buildHubGraph() {
        // Graph structure:
        //   A -> Hub
        //   B -> Hub
        //   C -> Hub
        //   D -> Hub
        //   Hub -> E
        //   Hub -> F
        //
        // Nodes: A, B, C, D, Hub, E, F
        // Expected: Hub has highest PageRank (all incoming, few outgoing)
        
        auto e1 = createEdge("e1", "A", "Hub");
        auto e2 = createEdge("e2", "B", "Hub");
        auto e3 = createEdge("e3", "C", "Hub");
        auto e4 = createEdge("e4", "D", "Hub");
        auto e5 = createEdge("e5", "Hub", "E");
        auto e6 = createEdge("e6", "Hub", "F");

        ASSERT_TRUE(graphMgr_->addEdge(e1).ok);
        ASSERT_TRUE(graphMgr_->addEdge(e2).ok);
        ASSERT_TRUE(graphMgr_->addEdge(e3).ok);
        ASSERT_TRUE(graphMgr_->addEdge(e4).ok);
        ASSERT_TRUE(graphMgr_->addEdge(e5).ok);
        ASSERT_TRUE(graphMgr_->addEdge(e6).ok);

        nodes_ = {"A", "B", "C", "D", "Hub", "E", "F"};
    }

    std::filesystem::path testDbPath_;
    std::unique_ptr<RocksDBWrapper> db_;
    std::unique_ptr<GraphIndexManager> graphMgr_;
    std::unique_ptr<GraphAnalytics> analytics_;
    std::vector<std::string> nodes_;
};

// ============================================================================
// Degree Centrality Tests
// ============================================================================

TEST_F(GraphAnalyticsTest, DegreeCentrality_SimpleGraph) {
    buildSimpleGraph();

    auto [st, results] = analytics_->degreeCentrality(nodes_);
    ASSERT_TRUE(st.ok) << st.message;
    ASSERT_EQ(results.size(), 4);

    // Expected degrees:
    // A: out=2, in=0, total=2
    // B: out=2, in=1, total=3
    // C: out=1, in=2, total=3
    // D: out=0, in=2, total=2

    EXPECT_EQ(results["A"].out_degree, 2);
    EXPECT_EQ(results["A"].in_degree, 0);
    EXPECT_EQ(results["A"].total_degree, 2);

    EXPECT_EQ(results["B"].out_degree, 2);
    EXPECT_EQ(results["B"].in_degree, 1);
    EXPECT_EQ(results["B"].total_degree, 3);

    EXPECT_EQ(results["C"].out_degree, 1);
    EXPECT_EQ(results["C"].in_degree, 2);
    EXPECT_EQ(results["C"].total_degree, 3);

    EXPECT_EQ(results["D"].out_degree, 0);
    EXPECT_EQ(results["D"].in_degree, 2);
    EXPECT_EQ(results["D"].total_degree, 2);
}

TEST_F(GraphAnalyticsTest, DegreeCentrality_HubGraph) {
    buildHubGraph();

    auto [st, results] = analytics_->degreeCentrality(nodes_);
    ASSERT_TRUE(st.ok) << st.message;
    ASSERT_EQ(results.size(), 7);

    // Hub should have highest in-degree
    EXPECT_EQ(results["Hub"].in_degree, 4);
    EXPECT_EQ(results["Hub"].out_degree, 2);
    EXPECT_EQ(results["Hub"].total_degree, 6);

    // Source nodes should have out-degree 1, in-degree 0
    EXPECT_EQ(results["A"].out_degree, 1);
    EXPECT_EQ(results["A"].in_degree, 0);
    EXPECT_EQ(results["B"].out_degree, 1);
    EXPECT_EQ(results["B"].in_degree, 0);

    // Sink nodes should have in-degree 1, out-degree 0
    EXPECT_EQ(results["E"].in_degree, 1);
    EXPECT_EQ(results["E"].out_degree, 0);
    EXPECT_EQ(results["F"].in_degree, 1);
    EXPECT_EQ(results["F"].out_degree, 0);
}

TEST_F(GraphAnalyticsTest, DegreeCentrality_EmptyNodeList) {
    auto [st, results] = analytics_->degreeCentrality({});
    EXPECT_FALSE(st.ok);
    EXPECT_TRUE(st.message.find("Empty") != std::string::npos);
}

// ============================================================================
// PageRank Tests
// ============================================================================

TEST_F(GraphAnalyticsTest, PageRank_SimpleGraph) {
    buildSimpleGraph();

    auto [st, ranks] = analytics_->pageRank(nodes_, 0.85, 100, 1e-6);
    ASSERT_TRUE(st.ok) << st.message;
    ASSERT_EQ(ranks.size(), 4);

    // Verify ranks sum to ~1.0
    double sum = 0.0;
    for (const auto& [pk, rank] : ranks) {
        sum += rank;
        EXPECT_GT(rank, 0.0) << "Node " << pk << " has non-positive rank";
    }
    EXPECT_NEAR(sum, 1.0, 0.01);

    // D should have highest rank (most incoming edges, no outgoing)
    EXPECT_GT(ranks["D"], ranks["A"]);
    EXPECT_GT(ranks["D"], ranks["B"]);
    
    // C should have higher rank than B (more incoming)
    EXPECT_GT(ranks["C"], ranks["B"]);

    // A should have lowest rank (no incoming edges)
    EXPECT_LT(ranks["A"], ranks["B"]);
    EXPECT_LT(ranks["A"], ranks["C"]);
    EXPECT_LT(ranks["A"], ranks["D"]);
}

TEST_F(GraphAnalyticsTest, PageRank_HubGraph) {
    buildHubGraph();

    auto [st, ranks] = analytics_->pageRank(nodes_, 0.85, 100, 1e-6);
    ASSERT_TRUE(st.ok) << st.message;
    ASSERT_EQ(ranks.size(), 7);

    // Verify ranks sum to ~1.0
    double sum = 0.0;
    for (const auto& [pk, rank] : ranks) {
        sum += rank;
    }
    EXPECT_NEAR(sum, 1.0, 0.01);

    // Hub should have highest rank (central node with many incoming edges)
    for (const auto& [pk, rank] : ranks) {
        if (pk != "Hub") {
            EXPECT_GT(ranks["Hub"], rank) << "Hub should have highest rank, but " << pk << " has " << rank;
        }
    }

    // E and F should have higher rank than source nodes A,B,C,D (they receive from Hub)
    EXPECT_GT(ranks["E"], ranks["A"]);
    EXPECT_GT(ranks["F"], ranks["A"]);
}

TEST_F(GraphAnalyticsTest, PageRank_UniformInitialization) {
    buildSimpleGraph();

    // Test with different damping factors
    auto [st1, ranks1] = analytics_->pageRank(nodes_, 0.5, 100, 1e-6);
    auto [st2, ranks2] = analytics_->pageRank(nodes_, 0.99, 100, 1e-6);

    ASSERT_TRUE(st1.ok);
    ASSERT_TRUE(st2.ok);

    // Different damping should give different results
    bool different = false;
    for (const auto& pk : nodes_) {
        if (std::abs(ranks1[pk] - ranks2[pk]) > 0.01) {
            different = true;
            break;
        }
    }
    EXPECT_TRUE(different) << "Different damping factors should yield different PageRank values";
}

TEST_F(GraphAnalyticsTest, PageRank_Convergence) {
    buildSimpleGraph();

    // Test convergence with tight tolerance
    auto [st, ranks] = analytics_->pageRank(nodes_, 0.85, 1000, 1e-9);
    ASSERT_TRUE(st.ok);

    // Should converge within 1000 iterations
    double sum = 0.0;
    for (const auto& [pk, rank] : ranks) {
        sum += rank;
    }
    EXPECT_NEAR(sum, 1.0, 1e-6);
}

TEST_F(GraphAnalyticsTest, PageRank_InvalidDamping) {
    buildSimpleGraph();

    auto [st1, ranks1] = analytics_->pageRank(nodes_, -0.1, 100, 1e-6);
    EXPECT_FALSE(st1.ok);
    EXPECT_TRUE(st1.message.find("Damping") != std::string::npos);

    auto [st2, ranks2] = analytics_->pageRank(nodes_, 1.5, 100, 1e-6);
    EXPECT_FALSE(st2.ok);
    EXPECT_TRUE(st2.message.find("Damping") != std::string::npos);
}

TEST_F(GraphAnalyticsTest, PageRank_InvalidIterations) {
    buildSimpleGraph();

    auto [st, ranks] = analytics_->pageRank(nodes_, 0.85, 0, 1e-6);
    EXPECT_FALSE(st.ok);
    EXPECT_TRUE(st.message.find("iterations") != std::string::npos);
}

TEST_F(GraphAnalyticsTest, PageRank_EmptyNodeList) {
    auto [st, ranks] = analytics_->pageRank({}, 0.85, 100, 1e-6);
    EXPECT_FALSE(st.ok);
    EXPECT_TRUE(st.message.find("Empty") != std::string::npos);
}

// ============================================================================
// Integration Test: Degree + PageRank
// ============================================================================

TEST_F(GraphAnalyticsTest, Integration_DegreeAndPageRank) {
    buildHubGraph();

    // Compute both degree and PageRank
    auto [deg_st, degrees] = analytics_->degreeCentrality(nodes_);
    auto [pr_st, ranks] = analytics_->pageRank(nodes_, 0.85, 100, 1e-6);

    ASSERT_TRUE(deg_st.ok);
    ASSERT_TRUE(pr_st.ok);

    // Hub should have highest degree and highest PageRank
    int max_degree = 0;
    double max_rank = 0.0;
    std::string max_degree_node;
    std::string max_rank_node;

    for (const auto& [pk, deg] : degrees) {
        if (deg.total_degree > max_degree) {
            max_degree = deg.total_degree;
            max_degree_node = pk;
        }
    }

    for (const auto& [pk, rank] : ranks) {
        if (rank > max_rank) {
            max_rank = rank;
            max_rank_node = pk;
        }
    }

    EXPECT_EQ(max_degree_node, "Hub");
    EXPECT_EQ(max_rank_node, "Hub");
    EXPECT_EQ(max_degree, 6);
    EXPECT_GT(max_rank, 0.2);  // Hub should have significant portion of total rank
}

// ============================================================================
// Betweenness Centrality Tests
// ============================================================================

TEST_F(GraphAnalyticsTest, BetweennessCentrality_SimpleGraph) {
    buildSimpleGraph();

    auto [st, betweenness] = analytics_->betweennessCentrality(nodes_);
    ASSERT_TRUE(st.ok) << st.message;
    ASSERT_EQ(betweenness.size(), 4);

    // In simple graph:
    //   A -> B -> C
    //   A -> C
    //   B -> D
    //   C -> D
    // B and C lie on paths between other nodes, so they should have non-zero betweenness
    EXPECT_GT(betweenness["B"], 0.0);
    EXPECT_GT(betweenness["C"], 0.0);
    
    // A is source-only, D is sink-only (in directed graph), lower betweenness
    EXPECT_GE(betweenness["A"], 0.0);
    EXPECT_GE(betweenness["D"], 0.0);
}

TEST_F(GraphAnalyticsTest, BetweennessCentrality_HubGraph) {
    buildHubGraph();

    auto [st, betweenness] = analytics_->betweennessCentrality(nodes_);
    ASSERT_TRUE(st.ok) << st.message;
    ASSERT_EQ(betweenness.size(), 7);

    // Hub should have highest betweenness (all paths go through it)
    double max_betweenness = 0.0;
    std::string max_node;
    for (const auto& [pk, bc] : betweenness) {
        if (bc > max_betweenness) {
            max_betweenness = bc;
            max_node = pk;
        }
    }
    
    EXPECT_EQ(max_node, "Hub");
    EXPECT_GT(max_betweenness, 0.0);
}

TEST_F(GraphAnalyticsTest, BetweennessCentrality_EmptyNodeList) {
    auto [st, betweenness] = analytics_->betweennessCentrality({});
    EXPECT_FALSE(st.ok);
    EXPECT_TRUE(st.message.find("Empty") != std::string::npos);
}

// ============================================================================
// Closeness Centrality Tests
// ============================================================================

TEST_F(GraphAnalyticsTest, ClosenessCentrality_SimpleGraph) {
    buildSimpleGraph();

    auto [st, closeness] = analytics_->closenessCentrality(nodes_);
    ASSERT_TRUE(st.ok) << st.message;
    ASSERT_EQ(closeness.size(), 4);

    // In directed graph, closeness depends on outgoing paths
    // A should have high closeness (can reach B, C, D)
    EXPECT_GT(closeness["A"], 0.0);
    EXPECT_GT(closeness["B"], 0.0);
    EXPECT_GT(closeness["C"], 0.0);
    
    // D is a sink (no outgoing edges), so closeness is 0 in directed graph
    EXPECT_EQ(closeness["D"], 0.0);
}

TEST_F(GraphAnalyticsTest, ClosenessCentrality_HubGraph) {
    buildHubGraph();

    auto [st, closeness] = analytics_->closenessCentrality(nodes_);
    ASSERT_TRUE(st.ok) << st.message;
    ASSERT_EQ(closeness.size(), 7);

    // Source nodes (A, B, C, D) should have high closeness (can reach Hub and beyond)
    EXPECT_GT(closeness["A"], 0.0);
    EXPECT_GT(closeness["B"], 0.0);
    
    // Hub should have moderate closeness (can reach E, F but not incoming nodes in directed graph)
    EXPECT_GE(closeness["Hub"], 0.0);
}

TEST_F(GraphAnalyticsTest, ClosenessCentrality_EmptyNodeList) {
    auto [st, closeness] = analytics_->closenessCentrality({});
    EXPECT_FALSE(st.ok);
    EXPECT_TRUE(st.message.find("Empty") != std::string::npos);
}

// ============================================================================
// Integration Test: All Centrality Measures
// ============================================================================

TEST_F(GraphAnalyticsTest, Integration_AllCentralityMeasures) {
    buildHubGraph();

    // Compute all centrality measures
    auto [deg_st, degrees] = analytics_->degreeCentrality(nodes_);
    auto [pr_st, ranks] = analytics_->pageRank(nodes_, 0.85, 100, 1e-6);
    auto [bc_st, betweenness] = analytics_->betweennessCentrality(nodes_);
    auto [cc_st, closeness] = analytics_->closenessCentrality(nodes_);

    ASSERT_TRUE(deg_st.ok);
    ASSERT_TRUE(pr_st.ok);
    ASSERT_TRUE(bc_st.ok);
    ASSERT_TRUE(cc_st.ok);

    // Hub should rank high in most measures
    EXPECT_EQ(degrees["Hub"].total_degree, 6);  // Highest degree
    EXPECT_GT(ranks["Hub"], 0.1);  // Significant PageRank
    
    // All measures should produce results for all nodes
    EXPECT_EQ(degrees.size(), 7);
    EXPECT_EQ(ranks.size(), 7);
    EXPECT_EQ(betweenness.size(), 7);
    EXPECT_EQ(closeness.size(), 7);
}

// ============================================================================
// Betweenness Centrality (Previously Placeholder)
// ============================================================================

TEST_F(GraphAnalyticsTest, BetweennessCentrality_NotImplemented) {
    buildSimpleGraph();

    auto [st, results] = analytics_->betweennessCentrality(nodes_);
    // This test used to expect "not yet implemented", but now it should work
    EXPECT_TRUE(st.ok);
    EXPECT_EQ(results.size(), 4);
}

// ============================================================================
// Community Detection - Louvain
// ============================================================================

TEST_F(GraphAnalyticsTest, LouvainCommunities_TwoClusters) {
    // Build a graph with two clear clusters:
    // Cluster 1: A <-> B <-> C (triangle)
    // Cluster 2: D <-> E <-> F (triangle)
    // Bridge: C -> D (weak connection)
    
    std::vector<std::string> nodes = {"A", "B", "C", "D", "E", "F"};
    
    // Cluster 1 (triangle)
    graphMgr_->addEdge(createEdge("e1", "A", "B"));
    graphMgr_->addEdge(createEdge("e2", "B", "A"));
    graphMgr_->addEdge(createEdge("e3", "B", "C"));
    graphMgr_->addEdge(createEdge("e4", "C", "B"));
    graphMgr_->addEdge(createEdge("e5", "C", "A"));
    graphMgr_->addEdge(createEdge("e6", "A", "C"));
    
    // Cluster 2 (triangle)
    graphMgr_->addEdge(createEdge("e7", "D", "E"));
    graphMgr_->addEdge(createEdge("e8", "E", "D"));
    graphMgr_->addEdge(createEdge("e9", "E", "F"));
    graphMgr_->addEdge(createEdge("e10", "F", "E"));
    graphMgr_->addEdge(createEdge("e11", "F", "D"));
    graphMgr_->addEdge(createEdge("e12", "D", "F"));
    
    // Bridge (weak)
    graphMgr_->addEdge(createEdge("e13", "C", "D"));
    
    auto [st, communities] = analytics_->louvainCommunities(nodes);
    ASSERT_TRUE(st.ok);
    EXPECT_EQ(communities.size(), 6);
    
    // Extract community IDs
    int comm_A = communities["A"];
    int comm_B = communities["B"];
    int comm_C = communities["C"];
    int comm_D = communities["D"];
    int comm_E = communities["E"];
    int comm_F = communities["F"];
    
    // Count unique communities
    std::set<int> unique_comms = {comm_A, comm_B, comm_C, comm_D, comm_E, comm_F};
    
    // Should detect at least 1 community (all merged) or at most 6 (no merging)
    // Typically 2-3 communities for this structure
    EXPECT_GE(unique_comms.size(), 1);
    EXPECT_LE(unique_comms.size(), 6);
    
    // At minimum: strongly connected triangles should group together
    // Check if any grouping occurred
    bool some_grouping = unique_comms.size() < 6;
    EXPECT_TRUE(some_grouping);
}

TEST_F(GraphAnalyticsTest, LouvainCommunities_SingleNode) {
    std::vector<std::string> nodes = {"A"};
    
    auto [st, communities] = analytics_->louvainCommunities(nodes);
    ASSERT_TRUE(st.ok);
    EXPECT_EQ(communities.size(), 1);
    EXPECT_EQ(communities["A"], 0);
}

TEST_F(GraphAnalyticsTest, LouvainCommunities_EmptyList) {
    auto [st, communities] = analytics_->louvainCommunities({});
    EXPECT_TRUE(st.ok);
    EXPECT_TRUE(communities.empty());
}

// ============================================================================
// Community Detection - Label Propagation
// ============================================================================

TEST_F(GraphAnalyticsTest, LabelPropagation_TwoClusters) {
    // Same graph structure as Louvain test
    std::vector<std::string> nodes = {"A", "B", "C", "D", "E", "F"};
    
    // Cluster 1
    graphMgr_->addEdge(createEdge("e1", "A", "B"));
    graphMgr_->addEdge(createEdge("e2", "B", "A"));
    graphMgr_->addEdge(createEdge("e3", "B", "C"));
    graphMgr_->addEdge(createEdge("e4", "C", "B"));
    graphMgr_->addEdge(createEdge("e5", "C", "A"));
    graphMgr_->addEdge(createEdge("e6", "A", "C"));
    
    // Cluster 2
    graphMgr_->addEdge(createEdge("e7", "D", "E"));
    graphMgr_->addEdge(createEdge("e8", "E", "D"));
    graphMgr_->addEdge(createEdge("e9", "E", "F"));
    graphMgr_->addEdge(createEdge("e10", "F", "E"));
    graphMgr_->addEdge(createEdge("e11", "F", "D"));
    graphMgr_->addEdge(createEdge("e12", "D", "F"));
    
    // Bridge
    graphMgr_->addEdge(createEdge("e13", "C", "D"));
    
    auto [st, communities] = analytics_->labelPropagationCommunities(nodes);
    ASSERT_TRUE(st.ok);
    EXPECT_EQ(communities.size(), 6);
    
    // Check cluster consistency
    int comm_A = communities["A"];
    int comm_B = communities["B"];
    int comm_C = communities["C"];
    int comm_D = communities["D"];
    int comm_E = communities["E"];
    int comm_F = communities["F"];
    
    // Count unique communities
    std::set<int> unique_comms = {comm_A, comm_B, comm_C, comm_D, comm_E, comm_F};
    
    // Label propagation should find some community structure
    EXPECT_GE(unique_comms.size(), 1);
    EXPECT_LE(unique_comms.size(), 6);
    
    // Expect at least some nodes to group together
    bool some_grouping = unique_comms.size() < 6;
    EXPECT_TRUE(some_grouping);
}

TEST_F(GraphAnalyticsTest, LabelPropagation_ChainGraph) {
    // Linear chain: A -> B -> C -> D
    // Should eventually converge (possibly to single community)
    std::vector<std::string> nodes = {"A", "B", "C", "D"};
    
    graphMgr_->addEdge(createEdge("e1", "A", "B"));
    graphMgr_->addEdge(createEdge("e2", "B", "C"));
    graphMgr_->addEdge(createEdge("e3", "C", "D"));
    
    auto [st, communities] = analytics_->labelPropagationCommunities(nodes, 50);
    ASSERT_TRUE(st.ok);
    EXPECT_EQ(communities.size(), 4);
    
    // All nodes should have community assignments
    EXPECT_TRUE(communities.count("A"));
    EXPECT_TRUE(communities.count("B"));
    EXPECT_TRUE(communities.count("C"));
    EXPECT_TRUE(communities.count("D"));
}

TEST_F(GraphAnalyticsTest, LabelPropagation_EmptyList) {
    auto [st, communities] = analytics_->labelPropagationCommunities({});
    EXPECT_TRUE(st.ok);
    EXPECT_TRUE(communities.empty());
}

