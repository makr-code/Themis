#include <gtest/gtest.h>
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include "transaction/transaction_manager.h"
#include "index/secondary_index.h"
#include "index/graph_index.h"
#include "index/vector_index.h"
#include <filesystem>
#include <thread>
#include <chrono>

using namespace themis;

class MVCCTest : public ::testing::Test {
protected:
    void SetUp() override {
    test_db_path_ = "./data/themis_mvcc_test";
        if (std::filesystem::exists(test_db_path_)) {
            std::filesystem::remove_all(test_db_path_);
        }
        
        RocksDBWrapper::Config config;
        config.db_path = test_db_path_;
        config.enable_wal = true;
        db_ = std::make_unique<RocksDBWrapper>(config);
        ASSERT_TRUE(db_->open());
    }
    
    void TearDown() override {
        db_.reset();
        if (std::filesystem::exists(test_db_path_)) {
            std::filesystem::remove_all(test_db_path_);
        }
    }
    
    std::string test_db_path_;
    std::unique_ptr<RocksDBWrapper> db_;
};

// Test 1: Basic Transaction Commit
TEST_F(MVCCTest, BasicTransactionCommit) {
    auto txn = db_->beginTransaction();
    ASSERT_NE(txn, nullptr);
    
    std::vector<uint8_t> value = {1, 2, 3, 4, 5};
    ASSERT_TRUE(txn->put("key1", value));
    ASSERT_TRUE(txn->commit());
    
    // Verify data persisted
    auto result = db_->get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, value);
}

// Test 2: Transaction Rollback
TEST_F(MVCCTest, TransactionRollback) {
    auto txn = db_->beginTransaction();
    
    std::vector<uint8_t> value = {1, 2, 3};
    ASSERT_TRUE(txn->put("key1", value));
    txn->rollback();
    
    // Verify data not persisted
    auto result = db_->get("key1");
    EXPECT_FALSE(result.has_value());
}

// Test 3: Snapshot Isolation - Concurrent Reads
TEST_F(MVCCTest, SnapshotIsolation) {
    // Initial write
    std::vector<uint8_t> value1 = {1, 2, 3};
    db_->put("key1", value1);
    
    // Start transaction 1 (gets snapshot)
    auto txn1 = db_->beginTransaction();
    auto read1 = txn1->get("key1");
    ASSERT_TRUE(read1.has_value());
    EXPECT_EQ(*read1, value1);
    
    // Another transaction modifies the key
    std::vector<uint8_t> value2 = {4, 5, 6};
    auto txn2 = db_->beginTransaction();
    ASSERT_TRUE(txn2->put("key1", value2));
    ASSERT_TRUE(txn2->commit());
    
    // Transaction 1 should still see old value (snapshot isolation)
    auto read1_again = txn1->get("key1");
    ASSERT_TRUE(read1_again.has_value());
    EXPECT_EQ(*read1_again, value1);
    
    txn1->rollback();
    
    // New transaction should see new value
    auto txn3 = db_->beginTransaction();
    auto read3 = txn3->get("key1");
    ASSERT_TRUE(read3.has_value());
    EXPECT_EQ(*read3, value2);
    txn3->rollback();
}

// Test 4: Write-Write Conflict Detection
TEST_F(MVCCTest, WriteWriteConflictDetection) {
    // Initial value
    std::vector<uint8_t> initial = {1, 2, 3};
    db_->put("key1", initial);
    
    // Start two transactions
    auto txn1 = db_->beginTransaction();
    auto txn2 = db_->beginTransaction();
    
    // Both read the same key
    auto read1 = txn1->get("key1");
    auto read2 = txn2->get("key1");
    ASSERT_TRUE(read1.has_value());
    ASSERT_TRUE(read2.has_value());
    
    // Both try to write
    std::vector<uint8_t> value1 = {4, 5, 6};
    std::vector<uint8_t> value2 = {7, 8, 9};
    ASSERT_TRUE(txn1->put("key1", value1));
    
    // First commit
    ASSERT_TRUE(txn1->commit());
    
    // Second put should fail (key is locked by txn1)
    // RocksDB pessimistic locking detects conflict at put() time
    EXPECT_FALSE(txn2->put("key1", value2));
    
    // Rollback txn2
    txn2->rollback();
    
    // Verify first transaction's value persisted
    auto final_value = db_->get("key1");
    ASSERT_TRUE(final_value.has_value());
    EXPECT_EQ(*final_value, value1);
}

// Test 5: Multiple Key Updates in Transaction
TEST_F(MVCCTest, MultipleKeyUpdates) {
    auto txn = db_->beginTransaction();
    
    std::vector<uint8_t> value1 = {1, 2, 3};
    std::vector<uint8_t> value2 = {4, 5, 6};
    std::vector<uint8_t> value3 = {7, 8, 9};
    
    ASSERT_TRUE(txn->put("key1", value1));
    ASSERT_TRUE(txn->put("key2", value2));
    ASSERT_TRUE(txn->put("key3", value3));
    
    ASSERT_TRUE(txn->commit());
    
    // Verify all keys
    EXPECT_EQ(*db_->get("key1"), value1);
    EXPECT_EQ(*db_->get("key2"), value2);
    EXPECT_EQ(*db_->get("key3"), value3);
}

// Test 6: Delete in Transaction
TEST_F(MVCCTest, DeleteInTransaction) {
    // Setup initial data
    std::vector<uint8_t> value = {1, 2, 3};
    db_->put("key1", value);
    
    auto txn = db_->beginTransaction();
    ASSERT_TRUE(txn->del("key1"));
    ASSERT_TRUE(txn->commit());
    
    // Verify deletion
    EXPECT_FALSE(db_->get("key1").has_value());
}

// Test 7: Repeatable Read within Transaction
TEST_F(MVCCTest, RepeatableRead) {
    std::vector<uint8_t> initial = {1, 2, 3};
    db_->put("key1", initial);
    
    auto txn = db_->beginTransaction();
    
    // First read
    auto read1 = txn->get("key1");
    ASSERT_TRUE(read1.has_value());
    EXPECT_EQ(*read1, initial);
    
    // External modification
    std::vector<uint8_t> modified = {4, 5, 6};
    db_->put("key1", modified);
    
    // Second read in same transaction should see same value
    auto read2 = txn->get("key1");
    ASSERT_TRUE(read2.has_value());
    EXPECT_EQ(*read2, initial);
    
    txn->rollback();
}

// Test 8: Conflict on Delete
TEST_F(MVCCTest, ConflictOnDelete) {
    std::vector<uint8_t> value = {1, 2, 3};
    db_->put("key1", value);
    
    auto txn1 = db_->beginTransaction();
    auto txn2 = db_->beginTransaction();
    
    // txn1 deletes
    ASSERT_TRUE(txn1->del("key1"));
    ASSERT_TRUE(txn1->commit());
    
    // txn2 tries to update (should fail - key was deleted)
    std::vector<uint8_t> new_value = {4, 5, 6};
    EXPECT_FALSE(txn2->put("key1", new_value)); // Conflict detected at put
    
    txn2->rollback();
}

// Test 9: Read-Write No Conflict
TEST_F(MVCCTest, ReadWriteNoConflict) {
    std::vector<uint8_t> value = {1, 2, 3};
    db_->put("key1", value);
    
    auto txn1 = db_->beginTransaction();
    auto txn2 = db_->beginTransaction();
    
    // txn1 only reads
    auto read1 = txn1->get("key1");
    ASSERT_TRUE(read1.has_value());
    
    // txn2 writes
    std::vector<uint8_t> new_value = {4, 5, 6};
    ASSERT_TRUE(txn2->put("key1", new_value));
    ASSERT_TRUE(txn2->commit());
    
    // txn1 commit should still succeed (no write conflict)
    EXPECT_TRUE(txn1->commit());
}

// Test 10: Auto Rollback on Destructor
TEST_F(MVCCTest, AutoRollbackOnDestructor) {
    {
        auto txn = db_->beginTransaction();
        std::vector<uint8_t> value = {1, 2, 3};
        ASSERT_TRUE(txn->put("key1", value));
        // txn goes out of scope without commit
    }
    
    // Verify data not persisted
    EXPECT_FALSE(db_->get("key1").has_value());
}

// Test 11: Concurrent Transactions Performance
TEST_F(MVCCTest, ConcurrentTransactionsPerformance) {
    const int num_keys = 100;
    
    // Setup initial data
    for (int i = 0; i < num_keys; ++i) {
        std::string key = "key_" + std::to_string(i);
        std::vector<uint8_t> value = {static_cast<uint8_t>(i)};
        db_->put(key, value);
    }
    
    auto start = std::chrono::steady_clock::now();
    
    // Simulate concurrent transactions
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    std::atomic<int> conflict_count{0};
    
    for (int t = 0; t < 5; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < 20; ++i) {
                auto txn = db_->beginTransaction();
                std::string key = "key_" + std::to_string(i);
                std::vector<uint8_t> value = {static_cast<uint8_t>(t), static_cast<uint8_t>(i)};
                txn->put(key, value);
                
                if (txn->commit()) {
                    success_count++;
                } else {
                    conflict_count++;
                }
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "MVCC Performance Test:\n";
    std::cout << "  Duration: " << duration.count() << " ms\n";
    std::cout << "  Successful commits: " << success_count << "\n";
    std::cout << "  Conflicts detected: " << conflict_count << "\n";
    std::cout << "  Total transactions: " << (success_count + conflict_count) << "\n";
    
    // Expect some conflicts but mostly successes
    EXPECT_GT(success_count.load(), 0);
}

// Test 12: Transaction with Mixed Operations
TEST_F(MVCCTest, MixedOperations) {
    // Setup
    db_->put("key1", {1});
    db_->put("key2", {2});
    db_->put("key3", {3});
    
    auto txn = db_->beginTransaction();
    
    // Read existing
    auto read1 = txn->get("key1");
    ASSERT_TRUE(read1.has_value());
    
    // Update existing
    ASSERT_TRUE(txn->put("key2", {20}));
    
    // Delete existing
    ASSERT_TRUE(txn->del("key3"));
    
    // Insert new
    ASSERT_TRUE(txn->put("key4", {4}));
    
    ASSERT_TRUE(txn->commit());
    
    // Verify
    EXPECT_TRUE(db_->get("key1").has_value());
    EXPECT_EQ(*db_->get("key2"), std::vector<uint8_t>{20});
    EXPECT_FALSE(db_->get("key3").has_value());
    EXPECT_EQ(*db_->get("key4"), std::vector<uint8_t>{4});
}
