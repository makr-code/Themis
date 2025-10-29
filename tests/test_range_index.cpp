// Test suite for range indexes

#include <gtest/gtest.h>
#include "index/secondary_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include <filesystem>
#include <memory>

namespace fs = std::filesystem;

namespace themis {

class RangeIndexTest : public ::testing::Test {
protected:
    void SetUp() override {
        testPath_ = "./data/test_range_index";
        fs::remove_all(testPath_);
        
        RocksDBWrapper::Config cfg;
        cfg.db_path = testPath_;
        cfg.enable_blobdb = false;
        db_ = std::make_unique<RocksDBWrapper>(cfg);
        ASSERT_TRUE(db_->open());
        
        mgr_ = std::make_unique<SecondaryIndexManager>(*db_);
    }
    
    void TearDown() override {
        mgr_.reset();
        db_.reset();
        fs::remove_all(testPath_);
    }
    
    std::string testPath_;
    std::unique_ptr<RocksDBWrapper> db_;
    std::unique_ptr<SecondaryIndexManager> mgr_;
};

// Test 1: Range-Index erstellen und prüfen
TEST_F(RangeIndexTest, CreateAndDrop) {
    auto st = mgr_->createRangeIndex("users", "age");
    ASSERT_TRUE(st.ok) << st.message;
    
    EXPECT_TRUE(mgr_->hasRangeIndex("users", "age"));
    
    st = mgr_->dropRangeIndex("users", "age");
    ASSERT_TRUE(st.ok) << st.message;
    
    EXPECT_FALSE(mgr_->hasRangeIndex("users", "age"));
}

// Test 2: Automatische Index-Pflege bei Put
TEST_F(RangeIndexTest, AutomaticIndexMaintenance) {
    // Erstelle Range-Index und normalen Index für Vergleich
    auto st = mgr_->createRangeIndex("users", "age");
    ASSERT_TRUE(st.ok) << st.message;
    st = mgr_->createIndex("users", "age"); // Auch Equality-Index
    ASSERT_TRUE(st.ok) << st.message;
    
    BaseEntity user1("user1");
    user1.setField("age", "25");
    user1.setField("name", "Alice");
    
    st = mgr_->put("users", user1);
    EXPECT_TRUE(st.ok) << st.message;
    
    // Verify Range-Index wurde gepflegt via scanKeysRange
    auto [rst, keys] = mgr_->scanKeysRange("users", "age", "25", "25", true, true, 10, false);
    ASSERT_TRUE(rst.ok) << rst.message;
    ASSERT_EQ(keys.size(), 1);
    EXPECT_EQ(keys[0], "user1");
}

// Test 2b: Automatische Index-Pflege nur mit Range-Index (ohne Equality-Index)
TEST_F(RangeIndexTest, AutomaticIndexMaintenance_RangeOnly) {
    auto st = mgr_->createRangeIndex("users", "age");
    ASSERT_TRUE(st.ok) << st.message;

    BaseEntity user1("user1");
    user1.setField("age", "25");
    user1.setField("name", "Alice");

    st = mgr_->put("users", user1);
    EXPECT_TRUE(st.ok) << st.message;

    auto [rst, keys] = mgr_->scanKeysRange("users", "age", "25", "25", true, true, 10, false);
    ASSERT_TRUE(rst.ok) << rst.message;
    ASSERT_EQ(keys.size(), 1);
    EXPECT_EQ(keys[0], "user1");
}

// Test 3: Range-Scan mit Grenzen (inklusive)
TEST_F(RangeIndexTest, RangeScanInclusive) {
    auto st = mgr_->createRangeIndex("users", "age");
    ASSERT_TRUE(st.ok) << st.message;
    st = mgr_->createIndex("users", "age");
    ASSERT_TRUE(st.ok) << st.message;
    
    // Daten: age 20, 25, 30, 35
    for (int age : {20, 25, 30, 35}) {
        BaseEntity e("u" + std::to_string(age));
        e.setField("age", std::to_string(age));
        st = mgr_->put("users", e);
        ASSERT_TRUE(st.ok) << st.message;
    }
    
    // Scan [25, 30] (inklusive)
    auto [rst, keys] = mgr_->scanKeysRange("users", "age", "25", "30", true, true, 100, false);
    ASSERT_TRUE(rst.ok) << rst.message;
    EXPECT_EQ(keys.size(), 2); // u25, u30
    EXPECT_TRUE(std::find(keys.begin(), keys.end(), "u25") != keys.end());
    EXPECT_TRUE(std::find(keys.begin(), keys.end(), "u30") != keys.end());
}

// Test 4: Range-Scan exklusive Grenzen
TEST_F(RangeIndexTest, RangeScanExclusive) {
    auto st = mgr_->createRangeIndex("users", "age");
    ASSERT_TRUE(st.ok) << st.message;
    st = mgr_->createIndex("users", "age");
    ASSERT_TRUE(st.ok) << st.message;
    
    for (int age : {20, 25, 30, 35}) {
        BaseEntity e("u" + std::to_string(age));
        e.setField("age", std::to_string(age));
        st = mgr_->put("users", e);
        ASSERT_TRUE(st.ok) << st.message;
    }
    
    // Scan (20, 35) (exklusiv)
    auto [rst, keys] = mgr_->scanKeysRange("users", "age", "20", "35", false, false, 100, false);
    ASSERT_TRUE(rst.ok) << rst.message;
    EXPECT_EQ(keys.size(), 2); // u25, u30 (ohne 20 und 35)
    EXPECT_TRUE(std::find(keys.begin(), keys.end(), "u25") != keys.end());
    EXPECT_TRUE(std::find(keys.begin(), keys.end(), "u30") != keys.end());
}

// Test 5: Range-Scan ohne untere Grenze
TEST_F(RangeIndexTest, RangeScanNoLowerBound) {
    auto st = mgr_->createRangeIndex("users", "age");
    ASSERT_TRUE(st.ok) << st.message;
    st = mgr_->createIndex("users", "age");
    ASSERT_TRUE(st.ok) << st.message;
    
    for (int age : {20, 25, 30}) {
        BaseEntity e("u" + std::to_string(age));
        e.setField("age", std::to_string(age));
        st = mgr_->put("users", e);
        ASSERT_TRUE(st.ok) << st.message;
    }
    
    // Scan [-∞, 25]
    auto [rst, keys] = mgr_->scanKeysRange("users", "age", std::nullopt, "25", true, true, 100, false);
    ASSERT_TRUE(rst.ok) << rst.message;
    EXPECT_EQ(keys.size(), 2); // u20, u25
}

// Test 6: Range-Scan ohne obere Grenze
TEST_F(RangeIndexTest, RangeScanNoUpperBound) {
    auto st = mgr_->createRangeIndex("users", "age");
    ASSERT_TRUE(st.ok) << st.message;
    st = mgr_->createIndex("users", "age");
    ASSERT_TRUE(st.ok) << st.message;
    
    for (int age : {20, 25, 30}) {
        BaseEntity e("u" + std::to_string(age));
        e.setField("age", std::to_string(age));
        st = mgr_->put("users", e);
        ASSERT_TRUE(st.ok) << st.message;
    }
    
    // Scan [25, +∞)
    auto [rst, keys] = mgr_->scanKeysRange("users", "age", "25", std::nullopt, true, true, 100, false);
    ASSERT_TRUE(rst.ok) << rst.message;
    EXPECT_EQ(keys.size(), 2); // u25, u30
}

// Test 7: Range-Scan mit Limit
TEST_F(RangeIndexTest, RangeScanWithLimit) {
    auto st = mgr_->createRangeIndex("users", "age");
    ASSERT_TRUE(st.ok) << st.message;
    st = mgr_->createIndex("users", "age");
    ASSERT_TRUE(st.ok) << st.message;
    
    for (int age = 10; age <= 50; age += 10) {
        BaseEntity e("u" + std::to_string(age));
        e.setField("age", std::to_string(age));
        st = mgr_->put("users", e);
        ASSERT_TRUE(st.ok) << st.message;
    }
    
    // Scan [0, +∞) mit Limit 2
    auto [rst, keys] = mgr_->scanKeysRange("users", "age", std::nullopt, std::nullopt, true, true, 2, false);
    ASSERT_TRUE(rst.ok) << rst.message;
    EXPECT_EQ(keys.size(), 2);
}

// Test 8: Range-Scan reversed (absteigend)
TEST_F(RangeIndexTest, RangeScanReversed) {
    auto st = mgr_->createRangeIndex("users", "age");
    ASSERT_TRUE(st.ok) << st.message;
    st = mgr_->createIndex("users", "age");
    ASSERT_TRUE(st.ok) << st.message;
    
    for (int age : {20, 25, 30}) {
        BaseEntity e("u" + std::to_string(age));
        e.setField("age", std::to_string(age));
        st = mgr_->put("users", e);
        ASSERT_TRUE(st.ok) << st.message;
    }
    
    // Scan [20, 30] reversed
    auto [rst, keys] = mgr_->scanKeysRange("users", "age", "20", "30", true, true, 100, true);
    ASSERT_TRUE(rst.ok) << rst.message;
    ASSERT_EQ(keys.size(), 3);
    // Sollte absteigend sein: u30, u25, u20
    EXPECT_EQ(keys[0], "u30");
    EXPECT_EQ(keys[1], "u25");
    EXPECT_EQ(keys[2], "u20");
}

// Test 9: Delete entfernt Range-Index-Einträge
TEST_F(RangeIndexTest, DeleteRemovesRangeEntry) {
    auto st = mgr_->createRangeIndex("users", "age");
    ASSERT_TRUE(st.ok) << st.message;
    st = mgr_->createIndex("users", "age");
    ASSERT_TRUE(st.ok) << st.message;
    
    BaseEntity user1("user1");
    user1.setField("age", "25");
    
    st = mgr_->put("users", user1);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Verify vorhanden
    auto [rst1, keys1] = mgr_->scanKeysRange("users", "age", "25", "25", true, true, 10, false);
    ASSERT_TRUE(rst1.ok) << rst1.message;
    EXPECT_EQ(keys1.size(), 1);
    
    // Delete
    st = mgr_->erase("users", "user1");
    EXPECT_TRUE(st.ok) << st.message;
    
    // Verify entfernt
    auto [rst2, keys2] = mgr_->scanKeysRange("users", "age", "25", "25", true, true, 10, false);
    ASSERT_TRUE(rst2.ok) << rst2.message;
    EXPECT_EQ(keys2.size(), 0);
}

// Test 10: String-Werte lexikografisch sortiert
TEST_F(RangeIndexTest, StringValuesLexicographicOrder) {
    auto st = mgr_->createRangeIndex("products", "name");
    ASSERT_TRUE(st.ok) << st.message;
    st = mgr_->createIndex("products", "name");
    ASSERT_TRUE(st.ok) << st.message;
    
    for (const std::string& name : {"Apple", "Banana", "Cherry", "Date"}) {
        BaseEntity e(name);
        e.setField("name", name);
        st = mgr_->put("products", e);
        ASSERT_TRUE(st.ok) << st.message;
    }
    
    // Scan ["B", "D")
    auto [rst, keys] = mgr_->scanKeysRange("products", "name", "B", "D", true, false, 100, false);
    ASSERT_TRUE(rst.ok) << rst.message;
    EXPECT_EQ(keys.size(), 2); // Banana, Cherry (ohne Date wegen exklusiv)
    EXPECT_TRUE(std::find(keys.begin(), keys.end(), "Banana") != keys.end());
    EXPECT_TRUE(std::find(keys.begin(), keys.end(), "Cherry") != keys.end());
}

} // namespace themis
