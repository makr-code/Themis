#include <gtest/gtest.h>
#include "index/property_graph.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include <filesystem>

namespace fs = std::filesystem;

class PropertyGraphTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_db_path_ = "./data/themis_property_graph_test";
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
        pgm_ = std::make_unique<themis::PropertyGraphManager>(*db_);
    }

    void TearDown() override {
        pgm_.reset();
        db_.reset();
        fs::remove_all(test_db_path_);
    }

    std::string test_db_path_;
    std::unique_ptr<themis::RocksDBWrapper> db_;
    std::unique_ptr<themis::PropertyGraphManager> pgm_;
};

using namespace themis;

TEST_F(PropertyGraphTest, AddNode_WithLabels) {
    BaseEntity alice("alice");
    alice.setField("id", "alice");
    alice.setField("name", "Alice");
    alice.setField("_labels", "Person,Employee");

    auto st = pgm_->addNode(alice);
    ASSERT_TRUE(st.ok) << st.message;

    // Verify node can be queried by label
    auto [st1, personNodes] = pgm_->getNodesByLabel("Person");
    ASSERT_TRUE(st1.ok);
    ASSERT_EQ(personNodes.size(), 1u);
    EXPECT_EQ(personNodes[0], "alice");

    auto [st2, employeeNodes] = pgm_->getNodesByLabel("Employee");
    ASSERT_TRUE(st2.ok);
    ASSERT_EQ(employeeNodes.size(), 1u);
    EXPECT_EQ(employeeNodes[0], "alice");
}

TEST_F(PropertyGraphTest, AddNodeLabel_UpdatesIndex) {
    BaseEntity bob("bob");
    bob.setField("id", "bob");
    bob.setField("name", "Bob");
    bob.setField("_labels", "Person");
    pgm_->addNode(bob);

    // Add new label
    auto st = pgm_->addNodeLabel("bob", "Manager");
    ASSERT_TRUE(st.ok) << st.message;

    // Verify both labels
    auto [st1, labels] = pgm_->getNodeLabels("bob");
    ASSERT_TRUE(st1.ok);
    ASSERT_EQ(labels.size(), 2u);
    EXPECT_TRUE(std::find(labels.begin(), labels.end(), "Person") != labels.end());
    EXPECT_TRUE(std::find(labels.begin(), labels.end(), "Manager") != labels.end());

    // Verify label index
    auto [st2, managerNodes] = pgm_->getNodesByLabel("Manager");
    ASSERT_TRUE(st2.ok);
    ASSERT_EQ(managerNodes.size(), 1u);
    EXPECT_EQ(managerNodes[0], "bob");
}

TEST_F(PropertyGraphTest, RemoveNodeLabel_UpdatesIndex) {
    BaseEntity charlie("charlie");
    charlie.setField("id", "charlie");
    charlie.setField("_labels", "Person,Employee,Manager");
    pgm_->addNode(charlie);

    // Remove label
    auto st = pgm_->removeNodeLabel("charlie", "Employee");
    ASSERT_TRUE(st.ok) << st.message;

    // Verify labels
    auto [st1, labels] = pgm_->getNodeLabels("charlie");
    ASSERT_TRUE(st1.ok);
    ASSERT_EQ(labels.size(), 2u);
    EXPECT_TRUE(std::find(labels.begin(), labels.end(), "Person") != labels.end());
    EXPECT_TRUE(std::find(labels.begin(), labels.end(), "Manager") != labels.end());
    EXPECT_TRUE(std::find(labels.begin(), labels.end(), "Employee") == labels.end());

    // Verify label index removed
    auto [st2, employeeNodes] = pgm_->getNodesByLabel("Employee");
    ASSERT_TRUE(st2.ok);
    EXPECT_EQ(employeeNodes.size(), 0u);
}

TEST_F(PropertyGraphTest, DeleteNode_RemovesAllLabels) {
    BaseEntity dave("dave");
    dave.setField("id", "dave");
    dave.setField("_labels", "Person,Developer");
    pgm_->addNode(dave);

    auto st = pgm_->deleteNode("dave");
    ASSERT_TRUE(st.ok) << st.message;

    // Verify all label indices removed
    auto [st1, personNodes] = pgm_->getNodesByLabel("Person");
    ASSERT_TRUE(st1.ok);
    EXPECT_EQ(personNodes.size(), 0u);

    auto [st2, devNodes] = pgm_->getNodesByLabel("Developer");
    ASSERT_TRUE(st2.ok);
    EXPECT_EQ(devNodes.size(), 0u);
}

TEST_F(PropertyGraphTest, AddEdge_WithType) {
    BaseEntity follows("follows_1");
    follows.setField("id", "follows_1");
    follows.setField("_from", "alice");
    follows.setField("_to", "bob");
    follows.setField("_type", "FOLLOWS");
    follows.setField("since", static_cast<int64_t>(2020));

    auto st = pgm_->addEdge(follows);
    ASSERT_TRUE(st.ok) << st.message;

    // Verify edge type
    auto [st1, type] = pgm_->getEdgeType("follows_1");
    ASSERT_TRUE(st1.ok);
    EXPECT_EQ(type, "FOLLOWS");

    // Verify type index
    auto [st2, followsEdges] = pgm_->getEdgesByType("FOLLOWS");
    ASSERT_TRUE(st2.ok);
    ASSERT_EQ(followsEdges.size(), 1u);
    EXPECT_EQ(followsEdges[0].edgeId, "follows_1");
    EXPECT_EQ(followsEdges[0].fromPk, "alice");
    EXPECT_EQ(followsEdges[0].toPk, "bob");
    EXPECT_EQ(followsEdges[0].type, "FOLLOWS");
}

TEST_F(PropertyGraphTest, GetEdgesByType_MultipleEdges) {
    BaseEntity e1("e1");
    e1.setField("id", "e1");
    e1.setField("_from", "alice");
    e1.setField("_to", "bob");
    e1.setField("_type", "FOLLOWS");
    pgm_->addEdge(e1);

    BaseEntity e2("e2");
    e2.setField("id", "e2");
    e2.setField("_from", "bob");
    e2.setField("_to", "charlie");
    e2.setField("_type", "FOLLOWS");
    pgm_->addEdge(e2);

    BaseEntity e3("e3");
    e3.setField("id", "e3");
    e3.setField("_from", "alice");
    e3.setField("_to", "charlie");
    e3.setField("_type", "LIKES");
    pgm_->addEdge(e3);

    // Query FOLLOWS edges
    auto [st, followsEdges] = pgm_->getEdgesByType("FOLLOWS");
    ASSERT_TRUE(st.ok);
    ASSERT_EQ(followsEdges.size(), 2u);

    // Query LIKES edges
    auto [st2, likesEdges] = pgm_->getEdgesByType("LIKES");
    ASSERT_TRUE(st2.ok);
    ASSERT_EQ(likesEdges.size(), 1u);
    EXPECT_EQ(likesEdges[0].edgeId, "e3");
}

TEST_F(PropertyGraphTest, GetTypedOutEdges_FiltersByType) {
    BaseEntity e1("e1");
    e1.setField("id", "e1");
    e1.setField("_from", "alice");
    e1.setField("_to", "bob");
    e1.setField("_type", "FOLLOWS");
    pgm_->addEdge(e1);

    BaseEntity e2("e2");
    e2.setField("id", "e2");
    e2.setField("_from", "alice");
    e2.setField("_to", "charlie");
    e2.setField("_type", "LIKES");
    pgm_->addEdge(e2);

    // Query alice's FOLLOWS edges
    auto [st, edges] = pgm_->getTypedOutEdges("alice", "FOLLOWS");
    ASSERT_TRUE(st.ok);
    ASSERT_EQ(edges.size(), 1u);
    EXPECT_EQ(edges[0].edgeId, "e1");
    EXPECT_EQ(edges[0].toPk, "bob");
    EXPECT_EQ(edges[0].type, "FOLLOWS");

    // Query alice's LIKES edges
    auto [st2, edges2] = pgm_->getTypedOutEdges("alice", "LIKES");
    ASSERT_TRUE(st2.ok);
    ASSERT_EQ(edges2.size(), 1u);
    EXPECT_EQ(edges2[0].edgeId, "e2");
    EXPECT_EQ(edges2[0].toPk, "charlie");
}

TEST_F(PropertyGraphTest, MultiGraph_Isolation) {
    // Add nodes to different graphs
    BaseEntity alice1("alice");
    alice1.setField("id", "alice");
    alice1.setField("_labels", "Person");
    pgm_->addNode(alice1, "social");

    BaseEntity alice2("alice");
    alice2.setField("id", "alice");
    alice2.setField("_labels", "Employee");
    pgm_->addNode(alice2, "corporate");

    // Verify graph isolation
    auto [st1, socialPeople] = pgm_->getNodesByLabel("Person", "social");
    ASSERT_TRUE(st1.ok);
    ASSERT_EQ(socialPeople.size(), 1u);

    auto [st2, corpPeople] = pgm_->getNodesByLabel("Person", "corporate");
    ASSERT_TRUE(st2.ok);
    EXPECT_EQ(corpPeople.size(), 0u);  // No Person in corporate graph

    auto [st3, corpEmployees] = pgm_->getNodesByLabel("Employee", "corporate");
    ASSERT_TRUE(st3.ok);
    ASSERT_EQ(corpEmployees.size(), 1u);
}

TEST_F(PropertyGraphTest, ListGraphs_ReturnsAllGraphIds) {
    BaseEntity n1("n1");
    n1.setField("id", "n1");
    pgm_->addNode(n1, "graph1");

    BaseEntity n2("n2");
    n2.setField("id", "n2");
    pgm_->addNode(n2, "graph2");

    BaseEntity n3("n3");
    n3.setField("id", "n3");
    pgm_->addNode(n3, "graph1");

    auto [st, graphs] = pgm_->listGraphs();
    ASSERT_TRUE(st.ok);
    ASSERT_EQ(graphs.size(), 2u);
    EXPECT_TRUE(std::find(graphs.begin(), graphs.end(), "graph1") != graphs.end());
    EXPECT_TRUE(std::find(graphs.begin(), graphs.end(), "graph2") != graphs.end());
}

TEST_F(PropertyGraphTest, GetGraphStats_CountsCorrectly) {
    // Add 3 nodes with 2 labels
    BaseEntity n1("n1");
    n1.setField("id", "n1");
    n1.setField("_labels", "Person");
    pgm_->addNode(n1, "test");

    BaseEntity n2("n2");
    n2.setField("id", "n2");
    n2.setField("_labels", "Person,Employee");
    pgm_->addNode(n2, "test");

    BaseEntity n3("n3");
    n3.setField("id", "n3");
    n3.setField("_labels", "Manager");
    pgm_->addNode(n3, "test");

    // Add 2 edges with 2 types
    BaseEntity e1("e1");
    e1.setField("id", "e1");
    e1.setField("_from", "n1");
    e1.setField("_to", "n2");
    e1.setField("_type", "FOLLOWS");
    pgm_->addEdge(e1, "test");

    BaseEntity e2("e2");
    e2.setField("id", "e2");
    e2.setField("_from", "n2");
    e2.setField("_to", "n3");
    e2.setField("_type", "REPORTS_TO");
    pgm_->addEdge(e2, "test");

    auto [st, stats] = pgm_->getGraphStats("test");
    ASSERT_TRUE(st.ok);
    EXPECT_EQ(stats.graph_id, "test");
    EXPECT_EQ(stats.node_count, 3u);
    EXPECT_EQ(stats.edge_count, 2u);
    EXPECT_EQ(stats.label_count, 3u);  // Person, Employee, Manager
    EXPECT_EQ(stats.type_count, 2u);   // FOLLOWS, REPORTS_TO
}

TEST_F(PropertyGraphTest, FederatedQuery_CrossGraph) {
    // Setup social graph
    BaseEntity alice("alice");
    alice.setField("id", "alice");
    alice.setField("_labels", "Person");
    pgm_->addNode(alice, "social");

    BaseEntity follows("follows1");
    follows.setField("id", "follows1");
    follows.setField("_from", "alice");
    follows.setField("_to", "bob");
    follows.setField("_type", "FOLLOWS");
    pgm_->addEdge(follows, "social");

    // Setup corporate graph
    BaseEntity emp("emp1");
    emp.setField("id", "emp1");
    emp.setField("_labels", "Employee");
    pgm_->addNode(emp, "corporate");

    BaseEntity reports("reports1");
    reports.setField("id", "reports1");
    reports.setField("_from", "emp1");
    reports.setField("_to", "manager1");
    reports.setField("_type", "REPORTS_TO");
    pgm_->addEdge(reports, "corporate");

    // Federated query
    std::vector<PropertyGraphManager::FederationPattern> patterns = {
        {"social", "Person", "node"},
        {"corporate", "Employee", "node"},
        {"social", "FOLLOWS", "edge"},
        {"corporate", "REPORTS_TO", "edge"}
    };

    auto [st, result] = pgm_->federatedQuery(patterns);
    ASSERT_TRUE(st.ok);
    
    // Verify nodes from both graphs
    ASSERT_EQ(result.nodes.size(), 2u);  // alice (Person), emp1 (Employee)
    
    // Verify edges from both graphs
    ASSERT_EQ(result.edges.size(), 2u);  // follows1, reports1
}

TEST_F(PropertyGraphTest, AddNodesBatch_Atomic) {
    std::vector<BaseEntity> nodes;
    
    for (int i = 0; i < 10; ++i) {
        BaseEntity node("node" + std::to_string(i));
        node.setField("id", "node" + std::to_string(i));
        node.setField("_labels", "Person");
        nodes.push_back(node);
    }

    auto st = pgm_->addNodesBatch(nodes);
    ASSERT_TRUE(st.ok) << st.message;

    auto [st2, personNodes] = pgm_->getNodesByLabel("Person");
    ASSERT_TRUE(st2.ok);
    EXPECT_EQ(personNodes.size(), 10u);
}

TEST_F(PropertyGraphTest, AddEdgesBatch_Atomic) {
    std::vector<BaseEntity> edges;
    
    for (int i = 0; i < 5; ++i) {
        BaseEntity edge("edge" + std::to_string(i));
        edge.setField("id", "edge" + std::to_string(i));
        edge.setField("_from", "node" + std::to_string(i));
        edge.setField("_to", "node" + std::to_string(i + 1));
        edge.setField("_type", "CONNECTS");
        edges.push_back(edge);
    }

    auto st = pgm_->addEdgesBatch(edges);
    ASSERT_TRUE(st.ok) << st.message;

    auto [st2, connectsEdges] = pgm_->getEdgesByType("CONNECTS");
    ASSERT_TRUE(st2.ok);
    EXPECT_EQ(connectsEdges.size(), 5u);
}
