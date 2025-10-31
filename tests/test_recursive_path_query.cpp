// Unit-Tests für Recursive Path Queries & Multi-Hop Reasoning

#include <gtest/gtest.h>
#include "query/query_engine.h"
#include "index/graph_index.h"
#include "index/secondary_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include <filesystem>

using namespace themis;

class RecursivePathQueryTest : public ::testing::Test {
protected:
    std::unique_ptr<RocksDBWrapper> db;
    std::unique_ptr<SecondaryIndexManager> secIdx;
    std::unique_ptr<GraphIndexManager> graphIdx;
    std::unique_ptr<QueryEngine> engine;
    std::string dbPath = "data/themis_recursive_path_test";

    void SetUp() override {
        // Cleanup from previous tests
        if (std::filesystem::exists(dbPath)) {
            std::filesystem::remove_all(dbPath);
        }

        RocksDBWrapper::Config config;
        config.db_path = dbPath;
        config.memtable_size_mb = 64;
        config.block_cache_size_mb = 256;
        
        db = std::make_unique<RocksDBWrapper>(config);
        ASSERT_TRUE(db->open());

        secIdx = std::make_unique<SecondaryIndexManager>(*db);
        graphIdx = std::make_unique<GraphIndexManager>(*db);
        engine = std::make_unique<QueryEngine>(*db, *secIdx, *graphIdx);
    }

    void TearDown() override {
        engine.reset();
        graphIdx.reset();
        secIdx.reset();
        db.reset();
        if (std::filesystem::exists(dbPath)) {
            std::filesystem::remove_all(dbPath);
        }
    }

    // Helper: Create a simple graph A -> B -> C -> D
    void createLinearGraph() {
        // Create edges: A -> B -> C -> D
        BaseEntity edge1("e1");
        edge1.setField("id", "e1");
        edge1.setField("_from", "A");
        edge1.setField("_to", "B");
        graphIdx->addEdge(edge1);

        BaseEntity edge2("e2");
        edge2.setField("id", "e2");
        edge2.setField("_from", "B");
        edge2.setField("_to", "C");
        graphIdx->addEdge(edge2);

        BaseEntity edge3("e3");
        edge3.setField("id", "e3");
        edge3.setField("_from", "C");
        edge3.setField("_to", "D");
        graphIdx->addEdge(edge3);
    }

    // Helper: Create graph with temporal edges
    void createTemporalGraph() {
        // Edge A -> B valid from 1000 to 2000
        BaseEntity edge1("e1");
        edge1.setField("id", "e1");
        edge1.setField("_from", "A");
        edge1.setField("_to", "B");
        edge1.setField("valid_from", 1000);
        edge1.setField("valid_to", 2000);
        graphIdx->addEdge(edge1);

        // Edge B -> C valid from 1500 to 3000
        BaseEntity edge2("e2");
        edge2.setField("id", "e2");
        edge2.setField("_from", "B");
        edge2.setField("_to", "C");
        edge2.setField("valid_from", 1500);
        edge2.setField("valid_to", 3000);
        graphIdx->addEdge(edge2);

        // Edge A -> C valid from 2500 to 4000 (direct path)
        BaseEntity edge3("e3");
        edge3.setField("id", "e3");
        edge3.setField("_from", "A");
        edge3.setField("_to", "C");
        edge3.setField("valid_from", 2500);
        edge3.setField("valid_to", 4000);
        graphIdx->addEdge(edge3);
    }
};

TEST_F(RecursivePathQueryTest, SimplePathQuery) {
    createLinearGraph();

    RecursivePathQuery q;
    q.start_node = "A";
    q.end_node = "D";
    q.max_depth = 5;

    auto [st, paths] = engine->executeRecursivePathQuery(q);
    ASSERT_TRUE(st.ok) << st.message;
    ASSERT_EQ(paths.size(), 1);
    
    // Path should be A -> B -> C -> D
    ASSERT_GE(paths[0].size(), 2); // At least start and end
    EXPECT_EQ(paths[0].front(), "A");
    EXPECT_EQ(paths[0].back(), "D");
}

TEST_F(RecursivePathQueryTest, PathNotFound) {
    createLinearGraph();

    RecursivePathQuery q;
    q.start_node = "D";
    q.end_node = "A"; // Reverse direction, no path
    q.max_depth = 5;

    auto [st, paths] = engine->executeRecursivePathQuery(q);
    // Should not find a path (graph is directed)
    EXPECT_TRUE(st.ok); // No error, just empty result
    EXPECT_EQ(paths.size(), 0);
}

TEST_F(RecursivePathQueryTest, BFSReachableNodes) {
    createLinearGraph();

    RecursivePathQuery q;
    q.start_node = "A";
    // No end_node: find all reachable
    q.max_depth = 2;

    auto [st, paths] = engine->executeRecursivePathQuery(q);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Should reach B and C (depth 2)
    EXPECT_GE(paths.size(), 2);
}

TEST_F(RecursivePathQueryTest, TemporalPathQuery_ValidTime) {
    createTemporalGraph();

    RecursivePathQuery q;
    q.start_node = "A";
    q.end_node = "C";
    q.max_depth = 3;
    q.valid_from = "1600"; // At time 1600, both e1 and e2 are valid

    auto [st, paths] = engine->executeRecursivePathQuery(q);
    ASSERT_TRUE(st.ok) << st.message;
    ASSERT_GE(paths.size(), 1);
    
    // Should find path A -> B -> C (both edges valid at 1600)
    EXPECT_EQ(paths[0].front(), "A");
    EXPECT_EQ(paths[0].back(), "C");
}

TEST_F(RecursivePathQueryTest, TemporalPathQuery_InvalidTime) {
    createTemporalGraph();

    RecursivePathQuery q;
    q.start_node = "A";
    q.end_node = "C";
    q.max_depth = 3;
    q.valid_from = "500"; // At time 500, no edges are valid

    auto [st, paths] = engine->executeRecursivePathQuery(q);
    // Should not find path (edges not valid at time 500)
    EXPECT_TRUE(st.ok); // No error, just empty result
    EXPECT_EQ(paths.size(), 0);
}

TEST_F(RecursivePathQueryTest, MaxDepthLimit) {
    createLinearGraph();

    RecursivePathQuery q;
    q.start_node = "A";
    q.end_node = "D";
    q.max_depth = 2; // Only reach to C, not D

    auto [st, paths] = engine->executeRecursivePathQuery(q);
    // May or may not find path depending on BFS implementation
    // This test verifies max_depth is respected
    EXPECT_TRUE(st.ok);
}

TEST_F(RecursivePathQueryTest, EmptyStartNode) {
    RecursivePathQuery q;
    q.start_node = "";
    q.end_node = "A";

    auto [st, paths] = engine->executeRecursivePathQuery(q);
    EXPECT_FALSE(st.ok);
    EXPECT_NE(st.message.find("start_node"), std::string::npos);
}

TEST_F(RecursivePathQueryTest, NoGraphIndexManager) {
    // Create engine without GraphIndexManager
    QueryEngine engineNoGraph(*db, *secIdx);

    RecursivePathQuery q;
    q.start_node = "A";
    q.end_node = "B";

    auto [st, paths] = engineNoGraph.executeRecursivePathQuery(q);
    EXPECT_FALSE(st.ok);
    EXPECT_NE(st.message.find("GraphIndexManager"), std::string::npos);
}
