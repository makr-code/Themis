#include <gtest/gtest.h>
#include "transaction/transaction_manager.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include "index/secondary_index.h"
#include "index/graph_index.h"
#include "index/vector_index.h"
#include <filesystem>
#include <thread>
#include <chrono>

using namespace themis;

class TransactionManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clean up any existing test database
    test_db_path_ = "./data/themis_transaction_manager_test";
        if (std::filesystem::exists(test_db_path_)) {
            std::filesystem::remove_all(test_db_path_);
        }
        
        // Create RocksDB wrapper
        RocksDBWrapper::Config config;
        config.db_path = test_db_path_;
        config.memtable_size_mb = 64;
        config.block_cache_size_mb = 128;
        
        db_ = std::make_unique<RocksDBWrapper>(config);
        ASSERT_TRUE(db_->open());
        
        // Create index managers
        secondary_index_ = std::make_unique<SecondaryIndexManager>(*db_);
        graph_index_ = std::make_unique<GraphIndexManager>(*db_);
        vector_index_ = std::make_unique<VectorIndexManager>(*db_);
        
        // Create transaction manager
        tx_manager_ = std::make_unique<TransactionManager>(*db_, *secondary_index_, *graph_index_, *vector_index_);
    }
    
    void TearDown() override {
        tx_manager_.reset();
        vector_index_.reset();
        secondary_index_.reset();
        graph_index_.reset();
        db_->close();
        db_.reset();
        
        // Clean up test database
        if (std::filesystem::exists(test_db_path_)) {
            std::filesystem::remove_all(test_db_path_);
        }
    }
    
    std::string test_db_path_;
    std::unique_ptr<RocksDBWrapper> db_;
    std::unique_ptr<SecondaryIndexManager> secondary_index_;
    std::unique_ptr<GraphIndexManager> graph_index_;
    std::unique_ptr<VectorIndexManager> vector_index_;
    std::unique_ptr<TransactionManager> tx_manager_;
};

// ===== Basic Transaction Tests =====

TEST_F(TransactionManagerTest, BeginTransaction) {
    auto txn_id = tx_manager_->beginTransaction();
    EXPECT_GT(txn_id, 0);
    
    auto txn = tx_manager_->getTransaction(txn_id);
    ASSERT_NE(txn, nullptr);
    EXPECT_EQ(txn->getId(), txn_id);
    EXPECT_FALSE(txn->isFinished());
}

TEST_F(TransactionManagerTest, BeginMultipleTransactions) {
    auto txn_id1 = tx_manager_->beginTransaction();
    auto txn_id2 = tx_manager_->beginTransaction();
    auto txn_id3 = tx_manager_->beginTransaction();
    
    EXPECT_NE(txn_id1, txn_id2);
    EXPECT_NE(txn_id2, txn_id3);
    EXPECT_NE(txn_id1, txn_id3);
    
    auto stats = tx_manager_->getStats();
    EXPECT_EQ(stats.total_begun, 3);
    EXPECT_EQ(stats.active_count, 3);
}

TEST_F(TransactionManagerTest, CommitTransaction) {
    auto txn_id = tx_manager_->beginTransaction();
    auto txn = tx_manager_->getTransaction(txn_id);
    ASSERT_NE(txn, nullptr);
    
    // Add entity in transaction
    BaseEntity entity("user1");
    entity.setField("name", std::string("Alice"));
    entity.setField("age", int64_t(30));
    
    auto put_status = txn->putEntity("users", entity);
    EXPECT_TRUE(put_status.ok);
    
    // Commit
    auto commit_status = tx_manager_->commitTransaction(txn_id);
    EXPECT_TRUE(commit_status.ok);
    
    // Verify transaction is finished
    EXPECT_TRUE(txn->isFinished());
    
    // Verify stats
    auto stats = tx_manager_->getStats();
    EXPECT_EQ(stats.total_committed, 1);
    EXPECT_EQ(stats.active_count, 0);
}

TEST_F(TransactionManagerTest, RollbackTransaction) {
    auto txn_id = tx_manager_->beginTransaction();
    auto txn = tx_manager_->getTransaction(txn_id);
    ASSERT_NE(txn, nullptr);
    
    // Add entity in transaction
    BaseEntity entity("user1");
    entity.setField("name", std::string("Bob"));
    
    auto put_status = txn->putEntity("users", entity);
    EXPECT_TRUE(put_status.ok);
    
    // Rollback
    tx_manager_->rollbackTransaction(txn_id);
    
    // Verify transaction is finished
    EXPECT_TRUE(txn->isFinished());
    
    // Verify stats
    auto stats = tx_manager_->getStats();
    EXPECT_EQ(stats.total_aborted, 1);
    EXPECT_EQ(stats.active_count, 0);
}

// ===== Atomicity Tests =====

TEST_F(TransactionManagerTest, AtomicMultiEntityCommit) {
    // Create index for testing
    secondary_index_->createIndex("users", "city");
    
    auto txn_id = tx_manager_->beginTransaction();
    auto txn = tx_manager_->getTransaction(txn_id);
    ASSERT_NE(txn, nullptr);
    
    // Add multiple entities in single transaction
    BaseEntity entity1("user1");
    entity1.setField("name", std::string("Alice"));
    entity1.setField("city", std::string("Berlin"));
    
    BaseEntity entity2("user2");
    entity2.setField("name", std::string("Bob"));
    entity2.setField("city", std::string("Munich"));
    
    BaseEntity entity3("user3");
    entity3.setField("name", std::string("Charlie"));
    entity3.setField("city", std::string("Berlin"));
    
    EXPECT_TRUE(txn->putEntity("users", entity1).ok);
    EXPECT_TRUE(txn->putEntity("users", entity2).ok);
    EXPECT_TRUE(txn->putEntity("users", entity3).ok);
    
    // Commit all at once
    auto commit_status = tx_manager_->commitTransaction(txn_id);
    EXPECT_TRUE(commit_status.ok);
    
    // Verify all entities exist
    auto [status1, keys1] = secondary_index_->scanKeysEqual("users", "city", "Berlin");
    EXPECT_TRUE(status1.ok);
    EXPECT_EQ(keys1.size(), 2);  // user1 and user3
    
    auto [status2, keys2] = secondary_index_->scanKeysEqual("users", "city", "Munich");
    EXPECT_TRUE(status2.ok);
    EXPECT_EQ(keys2.size(), 1);  // user2
}

TEST_F(TransactionManagerTest, AtomicRollbackPreventsPersistence) {
    // Create index
    secondary_index_->createIndex("users", "email");
    
    auto txn_id = tx_manager_->beginTransaction();
    auto txn = tx_manager_->getTransaction(txn_id);
    ASSERT_NE(txn, nullptr);
    
    // Add entities
    BaseEntity entity1("user1");
    entity1.setField("email", std::string("alice@test.com"));
    
    BaseEntity entity2("user2");
    entity2.setField("email", std::string("bob@test.com"));
    
    EXPECT_TRUE(txn->putEntity("users", entity1).ok);
    EXPECT_TRUE(txn->putEntity("users", entity2).ok);
    
    // Rollback instead of commit
    tx_manager_->rollbackTransaction(txn_id);
    
    // Verify NO entities exist in index
    auto [status1, keys1] = secondary_index_->scanKeysEqual("users", "email", "alice@test.com");
    EXPECT_TRUE(status1.ok);
    EXPECT_EQ(keys1.size(), 0);
    
    auto [status2, keys2] = secondary_index_->scanKeysEqual("users", "email", "bob@test.com");
    EXPECT_TRUE(status2.ok);
    EXPECT_EQ(keys2.size(), 0);
}

// ===== Graph Transaction Tests =====

TEST_F(TransactionManagerTest, GraphEdgeTransaction) {
    auto txn_id = tx_manager_->beginTransaction();
    auto txn = tx_manager_->getTransaction(txn_id);
    ASSERT_NE(txn, nullptr);
    
    // Create edge entity
    BaseEntity edge("edge1");
    edge.setField("id", std::string("edge1"));
    edge.setField("_from", std::string("user1"));
    edge.setField("_to", std::string("user2"));
    edge.setField("type", std::string("follows"));
    
    auto add_status = txn->addEdge(edge);
    EXPECT_TRUE(add_status.ok);
    
    // Commit
    auto commit_status = tx_manager_->commitTransaction(txn_id);
    EXPECT_TRUE(commit_status.ok);
    
    // Verify edge exists in graph index
    auto [status, neighbors] = graph_index_->outNeighbors("user1");
    EXPECT_TRUE(status.ok);
    EXPECT_EQ(neighbors.size(), 1);
    EXPECT_EQ(neighbors[0], "user2");
}

TEST_F(TransactionManagerTest, GraphEdgeRollback) {
    auto txn_id = tx_manager_->beginTransaction();
    auto txn = tx_manager_->getTransaction(txn_id);
    ASSERT_NE(txn, nullptr);
    
    // Create edge
    BaseEntity edge("edge1");
    edge.setField("id", std::string("edge1"));
    edge.setField("_from", std::string("user1"));
    edge.setField("_to", std::string("user2"));
    
    EXPECT_TRUE(txn->addEdge(edge).ok);
    
    // Rollback
    tx_manager_->rollbackTransaction(txn_id);
    
    // Verify edge does NOT exist
    auto [status, neighbors] = graph_index_->outNeighbors("user1");
    EXPECT_TRUE(status.ok);
    EXPECT_EQ(neighbors.size(), 0);
}

// ===== Isolation Level Tests =====

TEST_F(TransactionManagerTest, IsolationLevelReadCommitted) {
    auto txn_id = tx_manager_->beginTransaction(IsolationLevel::ReadCommitted);
    auto txn = tx_manager_->getTransaction(txn_id);
    ASSERT_NE(txn, nullptr);
    
    EXPECT_EQ(txn->getIsolationLevel(), IsolationLevel::ReadCommitted);
}

TEST_F(TransactionManagerTest, IsolationLevelSnapshot) {
    auto txn_id = tx_manager_->beginTransaction(IsolationLevel::Snapshot);
    auto txn = tx_manager_->getTransaction(txn_id);
    ASSERT_NE(txn, nullptr);
    
    EXPECT_EQ(txn->getIsolationLevel(), IsolationLevel::Snapshot);
}

// ===== Concurrent Transaction Tests =====

TEST_F(TransactionManagerTest, ConcurrentTransactionsNonConflicting) {
    // Create index
    secondary_index_->createIndex("users", "department");
    
    // Start two transactions concurrently
    auto txn_id1 = tx_manager_->beginTransaction();
    auto txn_id2 = tx_manager_->beginTransaction();
    
    auto txn1 = tx_manager_->getTransaction(txn_id1);
    auto txn2 = tx_manager_->getTransaction(txn_id2);
    
    ASSERT_NE(txn1, nullptr);
    ASSERT_NE(txn2, nullptr);
    
    // Transaction 1: Add user in Engineering
    BaseEntity entity1("user1");
    entity1.setField("name", std::string("Alice"));
    entity1.setField("department", std::string("Engineering"));
    EXPECT_TRUE(txn1->putEntity("users", entity1).ok);
    
    // Transaction 2: Add user in Sales
    BaseEntity entity2("user2");
    entity2.setField("name", std::string("Bob"));
    entity2.setField("department", std::string("Sales"));
    EXPECT_TRUE(txn2->putEntity("users", entity2).ok);
    
    // Commit both
    EXPECT_TRUE(tx_manager_->commitTransaction(txn_id1).ok);
    EXPECT_TRUE(tx_manager_->commitTransaction(txn_id2).ok);
    
    // Verify both exist
    auto [status1, eng_keys] = secondary_index_->scanKeysEqual("users", "department", "Engineering");
    EXPECT_TRUE(status1.ok);
    EXPECT_EQ(eng_keys.size(), 1);
    
    auto [status2, sales_keys] = secondary_index_->scanKeysEqual("users", "department", "Sales");
    EXPECT_TRUE(status2.ok);
    EXPECT_EQ(sales_keys.size(), 1);
}

TEST_F(TransactionManagerTest, TransactionDurationTracking) {
    auto txn_id = tx_manager_->beginTransaction();
    auto txn = tx_manager_->getTransaction(txn_id);
    ASSERT_NE(txn, nullptr);
    
    // Sleep for a short duration
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    auto duration = txn->getDurationMs();
    EXPECT_GE(duration, 50);
    EXPECT_LT(duration, 200);  // Should be less than 200ms
    
    tx_manager_->commitTransaction(txn_id);
}

// ===== Statistics Tests =====

TEST_F(TransactionManagerTest, StatisticsTracking) {
    // Begin 5 transactions
    auto txn_id1 = tx_manager_->beginTransaction();
    auto txn_id2 = tx_manager_->beginTransaction();
    auto txn_id3 = tx_manager_->beginTransaction();
    auto txn_id4 = tx_manager_->beginTransaction();
    auto txn_id5 = tx_manager_->beginTransaction();
    
    // Commit 3, rollback 2
    tx_manager_->commitTransaction(txn_id1);
    tx_manager_->commitTransaction(txn_id2);
    tx_manager_->rollbackTransaction(txn_id3);
    tx_manager_->commitTransaction(txn_id4);
    tx_manager_->rollbackTransaction(txn_id5);
    
    auto stats = tx_manager_->getStats();
    EXPECT_EQ(stats.total_begun, 5);
    EXPECT_EQ(stats.total_committed, 3);
    EXPECT_EQ(stats.total_aborted, 2);
    EXPECT_EQ(stats.active_count, 0);
}

TEST_F(TransactionManagerTest, MaxDurationTracking) {
    // Transaction 1: Short duration
    auto txn_id1 = tx_manager_->beginTransaction();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    tx_manager_->commitTransaction(txn_id1);
    
    // Transaction 2: Longer duration
    auto txn_id2 = tx_manager_->beginTransaction();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    tx_manager_->commitTransaction(txn_id2);
    
    auto stats = tx_manager_->getStats();
    EXPECT_GE(stats.max_duration_ms, 100);
    EXPECT_GE(stats.avg_duration_ms, 50);  // Average should be at least 50ms
}

// ===== Error Handling Tests =====

TEST_F(TransactionManagerTest, CommitNonExistentTransaction) {
    auto status = tx_manager_->commitTransaction(99999);
    EXPECT_FALSE(status.ok);
    EXPECT_NE(status.message.find("not found"), std::string::npos);
}

TEST_F(TransactionManagerTest, DoubleCommit) {
    auto txn_id = tx_manager_->beginTransaction();
    
    // First commit
    auto status1 = tx_manager_->commitTransaction(txn_id);
    EXPECT_TRUE(status1.ok);
    
    // Second commit should fail
    auto status2 = tx_manager_->commitTransaction(txn_id);
    EXPECT_FALSE(status2.ok);
}

TEST_F(TransactionManagerTest, RollbackAfterCommit) {
    auto txn_id = tx_manager_->beginTransaction();
    
    // Commit
    auto status = tx_manager_->commitTransaction(txn_id);
    EXPECT_TRUE(status.ok);
    
    // Rollback should do nothing (transaction already completed)
    tx_manager_->rollbackTransaction(txn_id);
    
    // Stats should show commit, not rollback
    auto stats = tx_manager_->getStats();
    EXPECT_EQ(stats.total_committed, 1);
    EXPECT_EQ(stats.total_aborted, 0);
}

// ===== Cleanup Tests =====

TEST_F(TransactionManagerTest, CleanupOldTransactions) {
    // Create and commit a transaction
    auto txn_id = tx_manager_->beginTransaction();
    tx_manager_->commitTransaction(txn_id);
    
    // Cleanup with 0 seconds max age (should remove all completed)
    tx_manager_->cleanupOldTransactions(std::chrono::seconds(0));
    
    // Transaction should no longer be retrievable
    auto txn = tx_manager_->getTransaction(txn_id);
    EXPECT_EQ(txn, nullptr);
}

TEST_F(TransactionManagerTest, AutoRollbackOnDestruction) {
    secondary_index_->createIndex("users", "status");
    
    {
        auto txn = tx_manager_->begin();  // Legacy API
        
        BaseEntity entity("user1");
        entity.setField("status", std::string("pending"));
        
        txn.putEntity("users", entity);
        
        // Transaction destroyed without commit -> auto-rollback
    }
    
    // Verify entity does NOT exist
    auto [status, keys] = secondary_index_->scanKeysEqual("users", "status", "pending");
    EXPECT_TRUE(status.ok);
    EXPECT_EQ(keys.size(), 0);
}

// ===== Vector Index Transaction Tests =====

TEST_F(TransactionManagerTest, VectorAddTransaction) {
    // Initialize vector index
    vector_index_->init("documents", 3, VectorIndexManager::Metric::COSINE);
    
    auto txn_id = tx_manager_->beginTransaction();
    auto txn = tx_manager_->getTransaction(txn_id);
    ASSERT_NE(txn, nullptr);
    
    // Create entity with embedding
    BaseEntity entity("doc1");
    entity.setField("title", std::string("Test Document"));
    std::vector<float> embedding = {0.1f, 0.2f, 0.3f};
    entity.setField("embedding", embedding);
    
    // Add vector via transaction
    EXPECT_TRUE(txn->addVector(entity, "embedding").ok);
    
    // Commit
    EXPECT_TRUE(tx_manager_->commitTransaction(txn_id).ok);
    
    // Verify vector exists via search
    std::vector<float> query = {0.1f, 0.2f, 0.3f};
    auto [search_status, results] = vector_index_->searchKnn(query, 1);
    EXPECT_TRUE(search_status.ok);
    EXPECT_EQ(results.size(), 1);
    if (!results.empty()) {
        EXPECT_EQ(results[0].pk, "doc1");
    }
}

TEST_F(TransactionManagerTest, VectorRollbackTransaction) {
    // Initialize vector index
    vector_index_->init("documents", 3, VectorIndexManager::Metric::COSINE);
    
    auto txn_id = tx_manager_->beginTransaction();
    auto txn = tx_manager_->getTransaction(txn_id);
    ASSERT_NE(txn, nullptr);
    
    // Create entity with embedding
    BaseEntity entity("doc2");
    entity.setField("title", std::string("Rollback Test"));
    std::vector<float> embedding = {0.4f, 0.5f, 0.6f};
    entity.setField("embedding", embedding);
    
    // Add vector via transaction
    EXPECT_TRUE(txn->addVector(entity, "embedding").ok);
    
    // Rollback
    tx_manager_->rollbackTransaction(txn_id);
    
    // Verify vector does NOT exist (RocksDB should be clean)
    // Note: In-memory cache may still have it (known limitation)
    std::string key = "documents:" + std::string("doc2");
    auto value = db_->get(key);
    EXPECT_FALSE(value.has_value());
}

TEST_F(TransactionManagerTest, VectorUpdateTransaction) {
    // Initialize vector index
    vector_index_->init("documents", 3, VectorIndexManager::Metric::COSINE);
    
    // First add a vector (outside transaction for setup)
    BaseEntity entity1("doc3");
    entity1.setField("title", std::string("Original"));
    std::vector<float> embedding1 = {0.1f, 0.1f, 0.1f};
    entity1.setField("embedding", embedding1);
    vector_index_->addEntity(entity1, "embedding");
    
    // Update via transaction
    auto txn_id = tx_manager_->beginTransaction();
    auto txn = tx_manager_->getTransaction(txn_id);
    ASSERT_NE(txn, nullptr);
    
    BaseEntity entity2("doc3");
    entity2.setField("title", std::string("Updated"));
    std::vector<float> embedding2 = {0.9f, 0.9f, 0.9f};
    entity2.setField("embedding", embedding2);
    
    EXPECT_TRUE(txn->updateVector(entity2, "embedding").ok);
    EXPECT_TRUE(tx_manager_->commitTransaction(txn_id).ok);
    
    // Verify updated vector via search
    std::vector<float> query = {1.0f, 1.0f, 1.0f};
    auto [search_status, results] = vector_index_->searchKnn(query, 1);
    EXPECT_TRUE(search_status.ok);
    EXPECT_EQ(results.size(), 1);
    if (!results.empty()) {
        EXPECT_EQ(results[0].pk, "doc3");
        // Should be closer to updated embedding
        EXPECT_LT(results[0].distance, 0.2f);  // Cosine distance should be small
    }
}

TEST_F(TransactionManagerTest, VectorRemoveTransaction) {
    // Initialize vector index
    vector_index_->init("documents", 3, VectorIndexManager::Metric::COSINE);
    
    // Add a vector (outside transaction for setup)
    BaseEntity entity("doc4");
    entity.setField("title", std::string("To Delete"));
    std::vector<float> embedding = {0.7f, 0.8f, 0.9f};
    entity.setField("embedding", embedding);
    vector_index_->addEntity(entity, "embedding");
    
    // Remove via transaction
    auto txn_id = tx_manager_->beginTransaction();
    auto txn = tx_manager_->getTransaction(txn_id);
    ASSERT_NE(txn, nullptr);
    
    EXPECT_TRUE(txn->removeVector("doc4").ok);
    EXPECT_TRUE(tx_manager_->commitTransaction(txn_id).ok);
    
    // Verify vector removed from RocksDB
    std::string key = "documents:" + std::string("doc4");
    auto value = db_->get(key);
    EXPECT_FALSE(value.has_value());
}
