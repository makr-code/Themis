// Test: Transaction Isolation Levels
// Validates ACID isolation guarantees including dirty reads, phantom reads, etc.

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
#include <atomic>
#include <vector>

using namespace themis;

class TransactionIsolationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clean up any existing test database
        test_db_path_ = "./data/themis_transaction_isolation_test";
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
        tx_manager_ = std::make_unique<TransactionManager>(
            *db_, *secondary_index_, *graph_index_, *vector_index_);
    }
    
    void TearDown() override {
        tx_manager_.reset();
        vector_index_.reset();
        graph_index_.reset();
        secondary_index_.reset();
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

// ===== Dirty Read Tests =====

TEST_F(TransactionIsolationTest, NoDirtyReads) {
    // Transaction T1 writes but doesn't commit
    // Transaction T2 should NOT see T1's uncommitted changes
    
    const std::string key = "account_1";
    
    // Initial state: account balance = 1000
    {
        auto txn_id = tx_manager_->beginTransaction();
        auto txn = tx_manager_->getTransaction(txn_id);
        
        BaseEntity entity(key);
        entity.setField("balance", 1000.0);
        txn->putEntity("accounts", entity);
        
        auto status = tx_manager_->commitTransaction(txn_id);
        ASSERT_TRUE(status.ok);
    }
    
    std::atomic<bool> t1_started{false};
    std::atomic<bool> t2_can_read{false};
    std::atomic<double> t2_read_value{0.0};
    
    // T1: Deduct 500 but don't commit yet
    std::thread t1([&]() {
        auto txn_id = tx_manager_->beginTransaction();
        auto txn = tx_manager_->getTransaction(txn_id);
        
        auto result = txn->getEntity("accounts", key);
        ASSERT_TRUE(result.has_value());
        
        auto entity = result.value();
        double balance = entity.getField<double>("balance").value();
        entity.setField("balance", balance - 500.0); // Now 500
        
        txn->putEntity("accounts", entity);
        
        t1_started = true;
        
        // Wait for T2 to try reading
        while (!t2_can_read) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // Sleep a bit before committing
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        tx_manager_->commitTransaction(txn_id);
    });
    
    // T2: Should read committed value (1000), not uncommitted (500)
    std::thread t2([&]() {
        // Wait for T1 to start
        while (!t1_started) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        auto txn_id = tx_manager_->beginTransaction();
        auto txn = tx_manager_->getTransaction(txn_id);
        
        t2_can_read = true;
        
        auto result = txn->getEntity("accounts", key);
        ASSERT_TRUE(result.has_value());
        
        auto entity = result.value();
        double balance = entity.getField<double>("balance").value();
        t2_read_value = balance;
        
        tx_manager_->commitTransaction(txn_id);
    });
    
    t1.join();
    t2.join();
    
    // T2 should have read the committed value (1000), not the uncommitted value (500)
    EXPECT_EQ(t2_read_value, 1000.0) << "Dirty read detected!";
}

// ===== Non-Repeatable Read Tests =====

TEST_F(TransactionIsolationTest, RepeatableReads) {
    // Transaction T1 reads a value twice
    // Transaction T2 modifies the value in between
    // T1 should see consistent snapshot (repeatable read)
    
    const std::string key = "product_1";
    
    // Initial state: price = 100
    {
        auto txn_id = tx_manager_->beginTransaction();
        auto txn = tx_manager_->getTransaction(txn_id);
        
        BaseEntity entity(key);
        entity.setField("price", 100.0);
        txn->putEntity("products", entity);
        
        auto status = tx_manager_->commitTransaction(txn_id);
        ASSERT_TRUE(status.ok);
    }
    
    std::atomic<double> first_read{0.0};
    std::atomic<double> second_read{0.0};
    std::atomic<bool> t1_first_read_done{false};
    std::atomic<bool> t2_done{false};
    
    // T1: Read, wait, read again
    std::thread t1([&]() {
        auto txn_id = tx_manager_->beginTransaction();
        auto txn = tx_manager_->getTransaction(txn_id);
        
        // First read
        auto result = txn->getEntity("products", key);
        ASSERT_TRUE(result.has_value());
        first_read = result.value().getField<double>("price").value();
        
        t1_first_read_done = true;
        
        // Wait for T2 to modify
        while (!t2_done) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // Second read (should see same value due to snapshot isolation)
        result = txn->getEntity("products", key);
        ASSERT_TRUE(result.has_value());
        second_read = result.value().getField<double>("price").value();
        
        tx_manager_->commitTransaction(txn_id);
    });
    
    // T2: Modify price
    std::thread t2([&]() {
        // Wait for T1's first read
        while (!t1_first_read_done) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        auto txn_id = tx_manager_->beginTransaction();
        auto txn = tx_manager_->getTransaction(txn_id);
        
        auto result = txn->getEntity("products", key);
        ASSERT_TRUE(result.has_value());
        
        auto entity = result.value();
        entity.setField("price", 150.0); // Change price
        txn->putEntity("products", entity);
        
        tx_manager_->commitTransaction(txn_id);
        t2_done = true;
    });
    
    t1.join();
    t2.join();
    
    // T1 should see consistent snapshot
    EXPECT_EQ(first_read, 100.0);
    EXPECT_EQ(second_read, 100.0) << "Non-repeatable read detected!";
}

// ===== Phantom Read Tests =====

TEST_F(TransactionIsolationTest, NoPhantomReads) {
    // Transaction T1 queries for entities matching a condition
    // Transaction T2 inserts a new entity that matches the condition
    // T1 queries again and should NOT see the new entity (no phantom)
    
    // Initial state: 2 products with price < 100
    {
        auto txn_id = tx_manager_->beginTransaction();
        auto txn = tx_manager_->getTransaction(txn_id);
        
        BaseEntity p1("product_1");
        p1.setField("price", 50.0);
        txn->putEntity("products", p1);
        
        BaseEntity p2("product_2");
        p2.setField("price", 75.0);
        txn->putEntity("products", p2);
        
        auto status = tx_manager_->commitTransaction(txn_id);
        ASSERT_TRUE(status.ok);
    }
    
    std::atomic<size_t> first_count{0};
    std::atomic<size_t> second_count{0};
    std::atomic<bool> t1_first_query_done{false};
    std::atomic<bool> t2_done{false};
    
    // T1: Query twice for products with price < 100
    std::thread t1([&]() {
        auto txn_id = tx_manager_->beginTransaction();
        auto txn = tx_manager_->getTransaction(txn_id);
        
        // First query
        auto results = secondary_index_->scan("products");
        size_t count = 0;
        for (const auto& entity : results) {
            if (entity.getField<double>("price").value_or(1000.0) < 100.0) {
                count++;
            }
        }
        first_count = count;
        
        t1_first_query_done = true;
        
        // Wait for T2 to insert
        while (!t2_done) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // Second query (should see same count due to snapshot isolation)
        results = secondary_index_->scan("products");
        count = 0;
        for (const auto& entity : results) {
            if (entity.getField<double>("price").value_or(1000.0) < 100.0) {
                count++;
            }
        }
        second_count = count;
        
        tx_manager_->commitTransaction(txn_id);
    });
    
    // T2: Insert new product with price < 100
    std::thread t2([&]() {
        // Wait for T1's first query
        while (!t1_first_query_done) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        auto txn_id = tx_manager_->beginTransaction();
        auto txn = tx_manager_->getTransaction(txn_id);
        
        BaseEntity p3("product_3");
        p3.setField("price", 80.0); // Matches the condition
        txn->putEntity("products", p3);
        
        tx_manager_->commitTransaction(txn_id);
        t2_done = true;
    });
    
    t1.join();
    t2.join();
    
    // T1 should see consistent snapshot (no phantom)
    EXPECT_EQ(first_count, 2);
    EXPECT_EQ(second_count, 2) << "Phantom read detected!";
}

// ===== Write Skew Tests =====

TEST_F(TransactionIsolationTest, WriteSkewDetection) {
    // Two doctors on call, at least one must always be on call
    // T1: Doctor A goes off call (reads B is on call)
    // T2: Doctor B goes off call (reads A is on call)
    // Both commit -> constraint violated (write skew anomaly)
    
    // Initial state: both doctors on call
    {
        auto txn_id = tx_manager_->beginTransaction();
        auto txn = tx_manager_->getTransaction(txn_id);
        
        BaseEntity doctorA("doctor_a");
        doctorA.setField("on_call", true);
        txn->putEntity("doctors", doctorA);
        
        BaseEntity doctorB("doctor_b");
        doctorB.setField("on_call", true);
        txn->putEntity("doctors", doctorB);
        
        auto status = tx_manager_->commitTransaction(txn_id);
        ASSERT_TRUE(status.ok);
    }
    
    std::atomic<bool> t1_committed{false};
    std::atomic<bool> t2_committed{false};
    std::atomic<bool> both_started{false};
    
    // T1: Doctor A goes off call
    std::thread t1([&]() {
        auto txn_id = tx_manager_->beginTransaction();
        auto txn = tx_manager_->getTransaction(txn_id);
        
        // Check if doctor B is on call
        auto resultB = txn->getEntity("doctors", "doctor_b");
        ASSERT_TRUE(resultB.has_value());
        bool b_on_call = resultB.value().getField<bool>("on_call").value();
        
        both_started = true;
        
        // Wait a bit to ensure T2 also starts
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        // If B is on call, A can go off call
        if (b_on_call) {
            auto resultA = txn->getEntity("doctors", "doctor_a");
            auto doctorA = resultA.value();
            doctorA.setField("on_call", false);
            txn->putEntity("doctors", doctorA);
        }
        
        auto status = tx_manager_->commitTransaction(txn_id);
        t1_committed = status.ok;
    });
    
    // T2: Doctor B goes off call
    std::thread t2([&]() {
        // Wait for both transactions to start
        while (!both_started) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        auto txn_id = tx_manager_->beginTransaction();
        auto txn = tx_manager_->getTransaction(txn_id);
        
        // Check if doctor A is on call
        auto resultA = txn->getEntity("doctors", "doctor_a");
        ASSERT_TRUE(resultA.has_value());
        bool a_on_call = resultA.value().getField<bool>("on_call").value();
        
        // If A is on call, B can go off call
        if (a_on_call) {
            auto resultB = txn->getEntity("doctors", "doctor_b");
            auto doctorB = resultB.value();
            doctorB.setField("on_call", false);
            txn->putEntity("doctors", doctorB);
        }
        
        auto status = tx_manager_->commitTransaction(txn_id);
        t2_committed = status.ok;
    });
    
    t1.join();
    t2.join();
    
    // At least one transaction should fail (to prevent write skew)
    // In serializable isolation, both cannot commit
    EXPECT_FALSE(t1_committed && t2_committed) 
        << "Write skew anomaly: both doctors went off call!";
}

// ===== Lost Update Tests =====

TEST_F(TransactionIsolationTest, NoLostUpdates) {
    // Two transactions read the same value, increment it, and write back
    // Final value should reflect both increments (no lost update)
    
    const std::string key = "counter";
    
    // Initial state: counter = 0
    {
        auto txn_id = tx_manager_->beginTransaction();
        auto txn = tx_manager_->getTransaction(txn_id);
        
        BaseEntity entity(key);
        entity.setField("value", int64_t(0));
        txn->putEntity("counters", entity);
        
        auto status = tx_manager_->commitTransaction(txn_id);
        ASSERT_TRUE(status.ok);
    }
    
    std::atomic<bool> t1_committed{false};
    std::atomic<bool> t2_committed{false};
    
    // T1: Read, increment, write
    std::thread t1([&]() {
        auto txn_id = tx_manager_->beginTransaction();
        auto txn = tx_manager_->getTransaction(txn_id);
        
        auto result = txn->getEntity("counters", key);
        ASSERT_TRUE(result.has_value());
        
        auto entity = result.value();
        int64_t value = entity.getField<int64_t>("value").value();
        
        // Simulate some processing time
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        entity.setField("value", value + 1);
        txn->putEntity("counters", entity);
        
        auto status = tx_manager_->commitTransaction(txn_id);
        t1_committed = status.ok;
    });
    
    // T2: Read, increment, write
    std::thread t2([&]() {
        // Start slightly after T1
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        auto txn_id = tx_manager_->beginTransaction();
        auto txn = tx_manager_->getTransaction(txn_id);
        
        auto result = txn->getEntity("counters", key);
        ASSERT_TRUE(result.has_value());
        
        auto entity = result.value();
        int64_t value = entity.getField<int64_t>("value").value();
        
        entity.setField("value", value + 1);
        txn->putEntity("counters", entity);
        
        auto status = tx_manager_->commitTransaction(txn_id);
        t2_committed = status.ok;
    });
    
    t1.join();
    t2.join();
    
    // At least one should commit
    EXPECT_TRUE(t1_committed || t2_committed);
    
    // Read final value
    auto txn_id = tx_manager_->beginTransaction();
    auto txn = tx_manager_->getTransaction(txn_id);
    auto result = txn->getEntity("counters", key);
    ASSERT_TRUE(result.has_value());
    
    int64_t final_value = result.value().getField<int64_t>("value").value();
    tx_manager_->commitTransaction(txn_id);
    
    // If both committed, final value should be 2
    // If one failed, final value should be 1
    if (t1_committed && t2_committed) {
        EXPECT_EQ(final_value, 2) << "Lost update detected!";
    } else {
        EXPECT_EQ(final_value, 1);
    }
}

// ===== Concurrent Write Detection =====

TEST_F(TransactionIsolationTest, ConcurrentWriteConflict) {
    // Multiple transactions writing to the same key
    // Should detect conflicts and abort conflicting transactions
    
    const std::string key = "shared_resource";
    
    const int num_threads = 10;
    std::vector<std::thread> threads;
    std::atomic<int> successful_commits{0};
    std::atomic<int> failed_commits{0};
    
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back([&, i]() {
            auto txn_id = tx_manager_->beginTransaction();
            auto txn = tx_manager_->getTransaction(txn_id);
            
            BaseEntity entity(key);
            entity.setField("thread_id", i);
            entity.setField("timestamp", std::chrono::system_clock::now().time_since_epoch().count());
            
            txn->putEntity("resources", entity);
            
            auto status = tx_manager_->commitTransaction(txn_id);
            if (status.ok) {
                successful_commits++;
            } else {
                failed_commits++;
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // At least one should succeed
    EXPECT_GT(successful_commits, 0);
    
    // Total should equal num_threads
    EXPECT_EQ(successful_commits + failed_commits, num_threads);
}
