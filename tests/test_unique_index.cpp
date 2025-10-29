// Test suite for unique indexes

#include <gtest/gtest.h>
#include "index/secondary_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include <filesystem>
#include <memory>

namespace fs = std::filesystem;

namespace themis {

class UniqueIndexTest : public ::testing::Test {
protected:
    void SetUp() override {
        testPath_ = "./data/test_unique_index";
        fs::remove_all(testPath_);
        
        RocksDBWrapper::Config cfg;
        cfg.db_path = testPath_;
        cfg.enable_blobdb = false; // Tests benötigen BlobDB nicht
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

// Test 1: Unique Index erstellen und Flag prüfen
TEST_F(UniqueIndexTest, CreateUniqueIndex) {
    auto st = mgr_->createIndex("users", "email", true);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Index sollte existieren
    EXPECT_TRUE(mgr_->hasIndex("users", "email"));
    
    // Non-unique Index zum Vergleich
    st = mgr_->createIndex("users", "city", false);
    ASSERT_TRUE(st.ok) << st.message;
    EXPECT_TRUE(mgr_->hasIndex("users", "city"));
}

// Test 2: Unique Constraint - erste Einfügung erfolgreich
TEST_F(UniqueIndexTest, UniqueFirstInsertSucceeds) {
    auto st = mgr_->createIndex("users", "email", true);
    ASSERT_TRUE(st.ok) << st.message;
    
    BaseEntity user1("user1");
    user1.setField("email", "alice@example.com");
    user1.setField("name", "Alice");
    
    st = mgr_->put("users", user1);
    EXPECT_TRUE(st.ok) << st.message;
}

// Test 3: Unique Constraint - Duplikat verhindert
TEST_F(UniqueIndexTest, UniqueDuplicatePrevented) {
    auto st = mgr_->createIndex("users", "email", true);
    ASSERT_TRUE(st.ok) << st.message;
    
    BaseEntity user1("user1");
    user1.setField("email", "alice@example.com");
    user1.setField("name", "Alice");
    
    st = mgr_->put("users", user1);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Versuch, zweiten User mit gleicher Email einzufügen
    BaseEntity user2("user2");
    user2.setField("email", "alice@example.com"); // Duplikat!
    user2.setField("name", "Alice Clone");
    
    st = mgr_->put("users", user2);
    EXPECT_FALSE(st.ok);
    EXPECT_NE(st.message.find("Unique constraint violation"), std::string::npos);
    EXPECT_NE(st.message.find("email"), std::string::npos);
}

// Test 4: Unique Constraint - Update auf gleichen PK erlaubt
TEST_F(UniqueIndexTest, UniqueUpdateSamePKAllowed) {
    auto st = mgr_->createIndex("users", "email", true);
    ASSERT_TRUE(st.ok) << st.message;
    
    BaseEntity user1("user1");
    user1.setField("email", "alice@example.com");
    user1.setField("name", "Alice");
    
    st = mgr_->put("users", user1);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Update derselben Entity (gleicher PK, gleiche Email)
    user1.setField("name", "Alice Updated");
    st = mgr_->put("users", user1);
    EXPECT_TRUE(st.ok) << st.message; // Sollte erlaubt sein
}

// Test 5: Unique Constraint - Update auf neue Email erlaubt
TEST_F(UniqueIndexTest, UniqueUpdateToNewValueAllowed) {
    auto st = mgr_->createIndex("users", "email", true);
    ASSERT_TRUE(st.ok) << st.message;
    
    BaseEntity user1("user1");
    user1.setField("email", "alice@example.com");
    user1.setField("name", "Alice");
    
    st = mgr_->put("users", user1);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Update auf neue Email
    user1.setField("email", "alice.new@example.com");
    st = mgr_->put("users", user1);
    EXPECT_TRUE(st.ok) << st.message;
}

// Test 6: Unique Constraint - Delete ermöglicht Wiedereinfügung
TEST_F(UniqueIndexTest, UniqueDeleteAllowsReinsertion) {
    auto st = mgr_->createIndex("users", "email", true);
    ASSERT_TRUE(st.ok) << st.message;
    
    BaseEntity user1("user1");
    user1.setField("email", "alice@example.com");
    user1.setField("name", "Alice");
    
    st = mgr_->put("users", user1);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Löschen
    st = mgr_->erase("users", "user1");
    EXPECT_TRUE(st.ok) << st.message;
    
    // Jetzt sollte derselbe Email-Wert wieder erlaubt sein
    BaseEntity user2("user2");
    user2.setField("email", "alice@example.com");
    user2.setField("name", "New Alice");
    
    st = mgr_->put("users", user2);
    EXPECT_TRUE(st.ok) << st.message;
}

// Test 7: Non-Unique Index erlaubt Duplikate
TEST_F(UniqueIndexTest, NonUniqueAllowsDuplicates) {
    auto st = mgr_->createIndex("users", "city", false); // Nicht unique
    ASSERT_TRUE(st.ok) << st.message;
    
    BaseEntity user1("user1");
    user1.setField("city", "Berlin");
    
    BaseEntity user2("user2");
    user2.setField("city", "Berlin"); // Gleicher Wert
    
    st = mgr_->put("users", user1);
    EXPECT_TRUE(st.ok) << st.message;
    
    st = mgr_->put("users", user2);
    EXPECT_TRUE(st.ok) << st.message; // Sollte erlaubt sein
}

// Test 8: Unique Composite Index - Duplikat verhindert
TEST_F(UniqueIndexTest, UniqueCompositeIndexDuplicatePrevented) {
    auto st = mgr_->createCompositeIndex("orders", {"customer_id", "order_date"}, true);
    ASSERT_TRUE(st.ok) << st.message;
    
    BaseEntity order1("order1");
    order1.setField("customer_id", "cust123");
    order1.setField("order_date", "2025-10-27");
    order1.setField("amount", "100");
    
    st = mgr_->put("orders", order1);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Versuch, zweite Order mit gleicher Kombination einzufügen
    BaseEntity order2("order2");
    order2.setField("customer_id", "cust123");
    order2.setField("order_date", "2025-10-27"); // Gleiche Kombination!
    order2.setField("amount", "200");
    
    st = mgr_->put("orders", order2);
    EXPECT_FALSE(st.ok);
    EXPECT_NE(st.message.find("Unique constraint violation"), std::string::npos);
}

// Test 9: Unique Composite Index - Teilweise gleich erlaubt
TEST_F(UniqueIndexTest, UniqueCompositePartialMatchAllowed) {
    auto st = mgr_->createCompositeIndex("orders", {"customer_id", "order_date"}, true);
    ASSERT_TRUE(st.ok) << st.message;
    
    BaseEntity order1("order1");
    order1.setField("customer_id", "cust123");
    order1.setField("order_date", "2025-10-27");
    
    BaseEntity order2("order2");
    order2.setField("customer_id", "cust123"); // Gleicher customer
    order2.setField("order_date", "2025-10-28"); // Aber anderes Datum
    
    st = mgr_->put("orders", order1);
    EXPECT_TRUE(st.ok) << st.message;
    
    st = mgr_->put("orders", order2);
    EXPECT_TRUE(st.ok) << st.message; // Sollte erlaubt sein (Kombination unterschiedlich)
}

// Test 10: Unique Composite Index - Delete ermöglicht Wiedereinfügung
TEST_F(UniqueIndexTest, UniqueCompositeDeleteAllowsReinsertion) {
    auto st = mgr_->createCompositeIndex("orders", {"customer_id", "order_date"}, true);
    ASSERT_TRUE(st.ok) << st.message;
    
    BaseEntity order1("order1");
    order1.setField("customer_id", "cust123");
    order1.setField("order_date", "2025-10-27");
    
    st = mgr_->put("orders", order1);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Löschen
    st = mgr_->erase("orders", "order1");
    EXPECT_TRUE(st.ok) << st.message;
    
    // Wiedereinfügung mit gleicher Kombination sollte erlaubt sein
    BaseEntity order2("order2");
    order2.setField("customer_id", "cust123");
    order2.setField("order_date", "2025-10-27");
    
    st = mgr_->put("orders", order2);
    EXPECT_TRUE(st.ok) << st.message;
}

// Test 11: Multiple Unique Indexes
TEST_F(UniqueIndexTest, MultipleUniqueIndexes) {
    auto st = mgr_->createIndex("users", "email", true);
    ASSERT_TRUE(st.ok) << st.message;
    
    st = mgr_->createIndex("users", "username", true);
    ASSERT_TRUE(st.ok) << st.message;
    
    BaseEntity user1("user1");
    user1.setField("email", "alice@example.com");
    user1.setField("username", "alice");
    
    st = mgr_->put("users", user1);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Duplikat Email sollte blockiert werden
    BaseEntity user2("user2");
    user2.setField("email", "alice@example.com"); // Duplikat
    user2.setField("username", "alice2");
    
    st = mgr_->put("users", user2);
    EXPECT_FALSE(st.ok);
    EXPECT_NE(st.message.find("email"), std::string::npos);
    
    // Duplikat Username sollte blockiert werden
    BaseEntity user3("user3");
    user3.setField("email", "bob@example.com");
    user3.setField("username", "alice"); // Duplikat
    
    st = mgr_->put("users", user3);
    EXPECT_FALSE(st.ok);
    EXPECT_NE(st.message.find("username"), std::string::npos);
}

} // namespace themis
