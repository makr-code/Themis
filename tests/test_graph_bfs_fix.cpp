#include <gtest/gtest.h>
#include "storage/rocksdb_wrapper.h"
#include "index/graph_index.h"
#include "storage/base_entity.h"
#include <filesystem>

using namespace themis;

class GraphBFSFixTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() / "themis_bfs_fix";
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_);
        RocksDBWrapper::Config cfg; cfg.db_path = dir_.string();
        db_ = std::make_unique<RocksDBWrapper>(cfg); db_->open();
        graph_ = std::make_unique<GraphIndexManager>(*db_);
    }
    void TearDown() override {
        graph_.reset(); db_->close(); db_.reset(); std::filesystem::remove_all(dir_);
    }

    BaseEntity mkEdge(const std::string& id, const std::string& from, const std::string& to) {
        BaseEntity::FieldMap f; f["id"] = id; f["_from"] = from; f["_to"] = to; return BaseEntity::fromFields(id, f);
    }

    std::filesystem::path dir_;
    std::unique_ptr<RocksDBWrapper> db_;
    std::unique_ptr<GraphIndexManager> graph_;
};

TEST_F(GraphBFSFixTest, BFSAfterRebuildIncludesAllReachable) {
    // Graph:
    // A -> B, A -> C, B -> D, C -> D, D -> E
    graph_->addEdge(mkEdge("e1", "A", "B"));
    graph_->addEdge(mkEdge("e2", "A", "C"));
    graph_->addEdge(mkEdge("e3", "B", "D"));
    graph_->addEdge(mkEdge("e4", "C", "D"));
    graph_->addEdge(mkEdge("e5", "D", "E"));

    // Rebuild topology (previously caused BFS to only return start node)
    auto st = graph_->rebuildTopology();
    ASSERT_TRUE(st.ok);

    auto [bfsStatus, order] = graph_->bfs("A", 10);
    ASSERT_TRUE(bfsStatus.ok);

    // Expect traversal order contains all nodes reachable
    // Order specifics: BFS layering -> A, B, C, D, E
    std::vector<std::string> expected = {"A", "B", "C", "D", "E"};
    EXPECT_EQ(order.size(), expected.size());
    for (const auto& node : expected) {
        EXPECT_NE(std::find(order.begin(), order.end(), node), order.end()) << "Missing node " << node;
    }
    // Check first element is start
    EXPECT_EQ(order.front(), "A");
}

TEST_F(GraphBFSFixTest, FallbackScanBFSWithoutRebuildWorks) {
    // Create graph but do NOT rebuildTopology()
    graph_->addEdge(mkEdge("e1", "X", "Y"));
    graph_->addEdge(mkEdge("e2", "Y", "Z"));

    auto [bfsStatus, order] = graph_->bfs("X", 5);
    ASSERT_TRUE(bfsStatus.ok);
    // Should discover X, Y, Z even without topologyLoaded_
    EXPECT_EQ(order.front(), "X");
    EXPECT_NE(std::find(order.begin(), order.end(), "Y"), order.end());
    EXPECT_NE(std::find(order.begin(), order.end(), "Z"), order.end());
}
