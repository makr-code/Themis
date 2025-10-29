// Test for composite secondary indexes

#include "index/secondary_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include <gtest/gtest.h>
#include <filesystem>

using namespace themis;

class CompositeIndexTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clean up test directory
        std::filesystem::remove_all(test_db_path_);
        
        RocksDBWrapper::Config config;
        config.db_path = test_db_path_;
        config.memtable_size_mb = 16;
        config.block_cache_size_mb = 32;
        
        db_ = std::make_unique<RocksDBWrapper>(config);
        ASSERT_TRUE(db_->open());
        
        idx_mgr_ = std::make_unique<SecondaryIndexManager>(*db_);
    }
    
    void TearDown() override {
        idx_mgr_.reset();
        db_->close();
        db_.reset();
        std::filesystem::remove_all(test_db_path_);
    }
    
    std::string test_db_path_ = "./test_composite_index_db";
    std::unique_ptr<RocksDBWrapper> db_;
    std::unique_ptr<SecondaryIndexManager> idx_mgr_;
};

TEST_F(CompositeIndexTest, CreateAndDrop) {
    std::vector<std::string> cols = {"age", "city"};
    
    EXPECT_FALSE(idx_mgr_->hasCompositeIndex("users", cols));
    
    auto st = idx_mgr_->createCompositeIndex("users", cols);
    EXPECT_TRUE(st.ok) << st.message;
    
    EXPECT_TRUE(idx_mgr_->hasCompositeIndex("users", cols));
    
    st = idx_mgr_->dropCompositeIndex("users", cols);
    EXPECT_TRUE(st.ok) << st.message;
    
    EXPECT_FALSE(idx_mgr_->hasCompositeIndex("users", cols));
}

TEST_F(CompositeIndexTest, RequiresMinimumTwoColumns) {
    std::vector<std::string> cols = {"age"};
    
    auto st = idx_mgr_->createCompositeIndex("users", cols);
    EXPECT_FALSE(st.ok);
    EXPECT_NE(st.message.find("mindestens 2"), std::string::npos);
}

TEST_F(CompositeIndexTest, ScanEqualComposite) {
    std::vector<std::string> cols = {"city", "age"};
    auto st = idx_mgr_->createCompositeIndex("users", cols);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Insert test entities
    BaseEntity e1("user1");
    e1.setField("name", "Alice");
    e1.setField("city", "Berlin");
    e1.setField("age", "30");
    st = idx_mgr_->put("users", e1);
    ASSERT_TRUE(st.ok) << st.message;
    
    BaseEntity e2("user2");
    e2.setField("name", "Bob");
    e2.setField("city", "Berlin");
    e2.setField("age", "25");
    st = idx_mgr_->put("users", e2);
    ASSERT_TRUE(st.ok) << st.message;
    
    BaseEntity e3("user3");
    e3.setField("name", "Charlie");
    e3.setField("city", "Berlin");
    e3.setField("age", "30");
    st = idx_mgr_->put("users", e3);
    ASSERT_TRUE(st.ok) << st.message;
    
    BaseEntity e4("user4");
    e4.setField("name", "Diana");
    e4.setField("city", "Munich");
    e4.setField("age", "30");
    st = idx_mgr_->put("users", e4);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Query: city=Berlin AND age=30
    std::vector<std::string> values = {"Berlin", "30"};
    auto [status, keys] = idx_mgr_->scanKeysEqualComposite("users", cols, values);
    ASSERT_TRUE(status.ok) << status.message;
    
    EXPECT_EQ(keys.size(), 2u);
    EXPECT_NE(std::find(keys.begin(), keys.end(), "user1"), keys.end());
    EXPECT_NE(std::find(keys.begin(), keys.end(), "user3"), keys.end());
    
    // Query: city=Munich AND age=30
    values = {"Munich", "30"};
    auto [status2, keys2] = idx_mgr_->scanKeysEqualComposite("users", cols, values);
    ASSERT_TRUE(status2.ok) << status2.message;
    
    EXPECT_EQ(keys2.size(), 1u);
    EXPECT_EQ(keys2[0], "user4");
    
    // Query: city=Berlin AND age=25
    values = {"Berlin", "25"};
    auto [status3, keys3] = idx_mgr_->scanKeysEqualComposite("users", cols, values);
    ASSERT_TRUE(status3.ok) << status3.message;
    
    EXPECT_EQ(keys3.size(), 1u);
    EXPECT_EQ(keys3[0], "user2");
}

TEST_F(CompositeIndexTest, EstimateCount) {
    std::vector<std::string> cols = {"status", "priority"};
    auto st = idx_mgr_->createCompositeIndex("tasks", cols);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Insert 5 tasks with status=open, priority=high
    for (int i = 0; i < 5; ++i) {
        BaseEntity e("task" + std::to_string(i));
        e.setField("status", "open");
        e.setField("priority", "high");
        e.setField("title", "Task " + std::to_string(i));
        st = idx_mgr_->put("tasks", e);
        ASSERT_TRUE(st.ok) << st.message;
    }
    
    // Insert 3 tasks with status=open, priority=low
    for (int i = 5; i < 8; ++i) {
        BaseEntity e("task" + std::to_string(i));
        e.setField("status", "open");
        e.setField("priority", "low");
        e.setField("title", "Task " + std::to_string(i));
        st = idx_mgr_->put("tasks", e);
        ASSERT_TRUE(st.ok) << st.message;
    }
    
    std::vector<std::string> values = {"open", "high"};
    bool capped = false;
    size_t count = idx_mgr_->estimateCountEqualComposite("tasks", cols, values, 100, &capped);
    
    EXPECT_EQ(count, 5u);
    EXPECT_FALSE(capped);
    
    values = {"open", "low"};
    count = idx_mgr_->estimateCountEqualComposite("tasks", cols, values, 100, &capped);
    
    EXPECT_EQ(count, 3u);
    EXPECT_FALSE(capped);
}

TEST_F(CompositeIndexTest, UpdateEntityMaintainsIndex) {
    std::vector<std::string> cols = {"department", "role"};
    auto st = idx_mgr_->createCompositeIndex("employees", cols);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Insert employee
    BaseEntity e("emp1");
    e.setField("name", "Alice");
    e.setField("department", "Engineering");
    e.setField("role", "Senior");
    st = idx_mgr_->put("employees", e);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Verify indexed
    std::vector<std::string> values = {"Engineering", "Senior"};
    auto [status, keys] = idx_mgr_->scanKeysEqualComposite("employees", cols, values);
    ASSERT_TRUE(status.ok) << status.message;
    EXPECT_EQ(keys.size(), 1u);
    EXPECT_EQ(keys[0], "emp1");
    
    // Update employee (change department)
    e.setField("department", "Sales");
    st = idx_mgr_->put("employees", e);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Old index should be gone
    auto [status2, keys2] = idx_mgr_->scanKeysEqualComposite("employees", cols, values);
    ASSERT_TRUE(status2.ok) << status2.message;
    EXPECT_EQ(keys2.size(), 0u);
    
    // New index should exist
    values = {"Sales", "Senior"};
    auto [status3, keys3] = idx_mgr_->scanKeysEqualComposite("employees", cols, values);
    ASSERT_TRUE(status3.ok) << status3.message;
    EXPECT_EQ(keys3.size(), 1u);
    EXPECT_EQ(keys3[0], "emp1");
}

TEST_F(CompositeIndexTest, DeleteEntityRemovesIndexEntry) {
    std::vector<std::string> cols = {"country", "state"};
    auto st = idx_mgr_->createCompositeIndex("locations", cols);
    ASSERT_TRUE(st.ok) << st.message;
    
    BaseEntity e("loc1");
    e.setField("city", "Munich");
    e.setField("country", "Germany");
    e.setField("state", "Bavaria");
    st = idx_mgr_->put("locations", e);
    ASSERT_TRUE(st.ok) << st.message;
    
    std::vector<std::string> values = {"Germany", "Bavaria"};
    auto [status, keys] = idx_mgr_->scanKeysEqualComposite("locations", cols, values);
    ASSERT_TRUE(status.ok) << status.message;
    EXPECT_EQ(keys.size(), 1u);
    
    // Delete
    st = idx_mgr_->erase("locations", "loc1");
    ASSERT_TRUE(st.ok) << st.message;
    
    // Index entry should be gone
    auto [status2, keys2] = idx_mgr_->scanKeysEqualComposite("locations", cols, values);
    ASSERT_TRUE(status2.ok) << status2.message;
    EXPECT_EQ(keys2.size(), 0u);
}

TEST_F(CompositeIndexTest, MismatchedColumnsAndValues) {
    std::vector<std::string> cols = {"a", "b", "c"};
    auto st = idx_mgr_->createCompositeIndex("test", cols);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Too few values
    std::vector<std::string> values = {"1", "2"};
    auto [status, keys] = idx_mgr_->scanKeysEqualComposite("test", cols, values);
    EXPECT_FALSE(status.ok);
    
    // Too many values
    values = {"1", "2", "3", "4"};
    auto [status2, keys2] = idx_mgr_->scanKeysEqualComposite("test", cols, values);
    EXPECT_FALSE(status2.ok);
}

TEST_F(CompositeIndexTest, ThreeColumnComposite) {
    std::vector<std::string> cols = {"region", "year", "quarter"};
    auto st = idx_mgr_->createCompositeIndex("sales", cols);
    ASSERT_TRUE(st.ok) << st.message;
    
    BaseEntity e1("sale1");
    e1.setField("region", "EU");
    e1.setField("year", "2024");
    e1.setField("quarter", "Q1");
    e1.setField("amount", "100000");
    st = idx_mgr_->put("sales", e1);
    ASSERT_TRUE(st.ok) << st.message;
    
    BaseEntity e2("sale2");
    e2.setField("region", "EU");
    e2.setField("year", "2024");
    e2.setField("quarter", "Q2");
    e2.setField("amount", "120000");
    st = idx_mgr_->put("sales", e2);
    ASSERT_TRUE(st.ok) << st.message;
    
    BaseEntity e3("sale3");
    e3.setField("region", "EU");
    e3.setField("year", "2024");
    e3.setField("quarter", "Q1");
    e3.setField("amount", "95000");
    st = idx_mgr_->put("sales", e3);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Query: EU, 2024, Q1
    std::vector<std::string> values = {"EU", "2024", "Q1"};
    auto [status, keys] = idx_mgr_->scanKeysEqualComposite("sales", cols, values);
    ASSERT_TRUE(status.ok) << status.message;
    
    EXPECT_EQ(keys.size(), 2u);
    EXPECT_NE(std::find(keys.begin(), keys.end(), "sale1"), keys.end());
    EXPECT_NE(std::find(keys.begin(), keys.end(), "sale3"), keys.end());
}
