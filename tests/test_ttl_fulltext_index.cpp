#include <gtest/gtest.h>
#include "index/secondary_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include <filesystem>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;

class TTLFulltextIndexTest : public ::testing::Test {
protected:
    void SetUp() override {
    test_db_path_ = "./data/themis_ttl_fulltext_index_test";
        fs::remove_all(test_db_path_);
        
        themis::RocksDBWrapper::Config config;
        config.db_path = test_db_path_;
        config.memtable_size_mb = 64;
        config.block_cache_size_mb = 256;
        
        db_ = std::make_unique<themis::RocksDBWrapper>(config);
        ASSERT_TRUE(db_->open());
        
        idx_ = std::make_unique<themis::SecondaryIndexManager>(*db_);
    }

    void TearDown() override {
        idx_.reset();
        db_->close();
        db_.reset();
        fs::remove_all(test_db_path_);
    }

    std::string test_db_path_;
    std::unique_ptr<themis::RocksDBWrapper> db_;
    std::unique_ptr<themis::SecondaryIndexManager> idx_;
};

// ────────────────────────────────────────────────────────────────────────────
// TTL Index Tests
// ────────────────────────────────────────────────────────────────────────────

TEST_F(TTLFulltextIndexTest, CreateAndDropTTLIndex) {
    auto st = idx_->createTTLIndex("sessions", "created_at", 3600); // 1 hour TTL
    ASSERT_TRUE(st.ok) << st.message;
    EXPECT_TRUE(idx_->hasTTLIndex("sessions", "created_at"));

    st = idx_->dropTTLIndex("sessions", "created_at");
    ASSERT_TRUE(st.ok) << st.message;
    EXPECT_FALSE(idx_->hasTTLIndex("sessions", "created_at"));
}

TEST_F(TTLFulltextIndexTest, TTLIndex_AutoMaintenance) {
    // Create TTL index with 2 second TTL
    auto st = idx_->createTTLIndex("sessions", "created_at", 2);
    ASSERT_TRUE(st.ok);
    
    // Also create regular index for user field (for querying)
    ASSERT_TRUE(idx_->createIndex("sessions", "user", false).ok);
    
    // Insert entity
    themis::BaseEntity session("sess1");
    session.setField("user", "alice");
    session.setField("created_at", "2025-10-27"); // Value doesn't matter, TTL is based on insert time
    
    ASSERT_TRUE(idx_->put("sessions", session).ok);
    
    // Immediately: entity should exist
    auto [st1, pks1] = idx_->scanKeysEqual("sessions", "user", "alice");
    ASSERT_TRUE(st1.ok);
    EXPECT_EQ(pks1.size(), 1);
    
    // Wait for TTL to expire (2 seconds + margin)
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    // Cleanup expired entities
    auto [st2, deletedCount] = idx_->cleanupExpiredEntities("sessions", "created_at");
    ASSERT_TRUE(st2.ok) << st2.message;
    EXPECT_EQ(deletedCount, 1);
    
    // Entity should be gone
    auto [st3, pks3] = idx_->scanKeysEqual("sessions", "user", "alice");
    ASSERT_TRUE(st3.ok);
    EXPECT_TRUE(pks3.empty());
}

TEST_F(TTLFulltextIndexTest, TTLIndex_MultipleEntities) {
    // Create TTL index
    auto st = idx_->createTTLIndex("cache", "timestamp", 1); // 1 second TTL
    ASSERT_TRUE(st.ok);
    
    // Insert 3 entities
    for (int i = 0; i < 3; ++i) {
        themis::BaseEntity entry("cache" + std::to_string(i));
        entry.setField("value", "data" + std::to_string(i));
        entry.setField("timestamp", "now");
        ASSERT_TRUE(idx_->put("cache", entry).ok);
    }
    
    // Wait for expiration
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Cleanup
    auto [st2, deletedCount] = idx_->cleanupExpiredEntities("cache", "timestamp");
    ASSERT_TRUE(st2.ok);
    EXPECT_EQ(deletedCount, 3);
}

// ────────────────────────────────────────────────────────────────────────────
// Fulltext Index Tests
// ────────────────────────────────────────────────────────────────────────────

TEST_F(TTLFulltextIndexTest, CreateAndDropFulltextIndex) {
    auto st = idx_->createFulltextIndex("articles", "content");
    ASSERT_TRUE(st.ok) << st.message;
    EXPECT_TRUE(idx_->hasFulltextIndex("articles", "content"));

    st = idx_->dropFulltextIndex("articles", "content");
    ASSERT_TRUE(st.ok) << st.message;
    EXPECT_FALSE(idx_->hasFulltextIndex("articles", "content"));
}

TEST_F(TTLFulltextIndexTest, Tokenizer_WhitespaceAndLowercase) {
    auto tokens = themis::SecondaryIndexManager::tokenize("Hello World! This is a TEST.");
    
    ASSERT_EQ(tokens.size(), 6);
    EXPECT_EQ(tokens[0], "hello");
    EXPECT_EQ(tokens[1], "world");
    EXPECT_EQ(tokens[2], "this");
    EXPECT_EQ(tokens[3], "is");
    EXPECT_EQ(tokens[4], "a");
    EXPECT_EQ(tokens[5], "test");
}

TEST_F(TTLFulltextIndexTest, Tokenizer_Punctuation) {
    auto tokens = themis::SecondaryIndexManager::tokenize("foo,bar:baz;qux");
    
    ASSERT_EQ(tokens.size(), 4);
    EXPECT_EQ(tokens[0], "foo");
    EXPECT_EQ(tokens[1], "bar");
    EXPECT_EQ(tokens[2], "baz");
    EXPECT_EQ(tokens[3], "qux");
}

TEST_F(TTLFulltextIndexTest, Fulltext_AutoMaintenance) {
    // Create fulltext index
    auto st = idx_->createFulltextIndex("documents", "content");
    ASSERT_TRUE(st.ok);
    
    // Insert documents
    themis::BaseEntity doc1("doc1");
    doc1.setField("title", "Document One");
    doc1.setField("content", "The quick brown fox jumps over the lazy dog");
    
    themis::BaseEntity doc2("doc2");
    doc2.setField("title", "Document Two");
    doc2.setField("content", "The lazy cat sleeps all day");
    
    themis::BaseEntity doc3("doc3");
    doc3.setField("title", "Document Three");
    doc3.setField("content", "Quick brown foxes are smart");
    
    ASSERT_TRUE(idx_->put("documents", doc1).ok);
    ASSERT_TRUE(idx_->put("documents", doc2).ok);
    ASSERT_TRUE(idx_->put("documents", doc3).ok);
    
    // Search: "quick" -> should find doc1 and doc3
    auto [st1, pks1] = idx_->scanFulltext("documents", "content", "quick");
    ASSERT_TRUE(st1.ok) << st1.message;
    EXPECT_EQ(pks1.size(), 2);
    std::sort(pks1.begin(), pks1.end());
    EXPECT_EQ(pks1[0], "doc1");
    EXPECT_EQ(pks1[1], "doc3");
    
    // Search: "lazy" -> should find doc1 and doc2
    auto [st2, pks2] = idx_->scanFulltext("documents", "content", "lazy");
    ASSERT_TRUE(st2.ok);
    EXPECT_EQ(pks2.size(), 2);
    std::sort(pks2.begin(), pks2.end());
    EXPECT_EQ(pks2[0], "doc1");
    EXPECT_EQ(pks2[1], "doc2");
    
    // Search: "cat" -> should find only doc2
    auto [st3, pks3] = idx_->scanFulltext("documents", "content", "cat");
    ASSERT_TRUE(st3.ok);
    EXPECT_EQ(pks3.size(), 1);
    EXPECT_EQ(pks3[0], "doc2");
}

TEST_F(TTLFulltextIndexTest, Fulltext_MultiTokenAND) {
    auto st = idx_->createFulltextIndex("documents", "content");
    ASSERT_TRUE(st.ok);
    
    themis::BaseEntity doc1("doc1");
    doc1.setField("content", "apple banana orange");
    
    themis::BaseEntity doc2("doc2");
    doc2.setField("content", "apple banana");
    
    themis::BaseEntity doc3("doc3");
    doc3.setField("content", "apple orange");
    
    ASSERT_TRUE(idx_->put("documents", doc1).ok);
    ASSERT_TRUE(idx_->put("documents", doc2).ok);
    ASSERT_TRUE(idx_->put("documents", doc3).ok);
    
    // Search: "apple banana" (AND logic) -> should find doc1 and doc2
    auto [st1, pks1] = idx_->scanFulltext("documents", "content", "apple banana");
    ASSERT_TRUE(st1.ok);
    EXPECT_EQ(pks1.size(), 2);
    std::sort(pks1.begin(), pks1.end());
    EXPECT_EQ(pks1[0], "doc1");
    EXPECT_EQ(pks1[1], "doc2");
    
    // Search: "apple banana orange" (AND) -> should find only doc1
    auto [st2, pks2] = idx_->scanFulltext("documents", "content", "apple banana orange");
    ASSERT_TRUE(st2.ok);
    EXPECT_EQ(pks2.size(), 1);
    EXPECT_EQ(pks2[0], "doc1");
    
    // Search: "kiwi" -> should find nothing
    auto [st3, pks3] = idx_->scanFulltext("documents", "content", "kiwi");
    ASSERT_TRUE(st3.ok);
    EXPECT_TRUE(pks3.empty());
}

TEST_F(TTLFulltextIndexTest, Fulltext_DeleteRemovesTokens) {
    auto st = idx_->createFulltextIndex("documents", "content");
    ASSERT_TRUE(st.ok);
    
    themis::BaseEntity doc1("doc1");
    doc1.setField("content", "hello world");
    ASSERT_TRUE(idx_->put("documents", doc1).ok);
    
    // Verify search works
    auto [st1, pks1] = idx_->scanFulltext("documents", "content", "hello");
    ASSERT_TRUE(st1.ok);
    EXPECT_EQ(pks1.size(), 1);
    
    // Delete document
    ASSERT_TRUE(idx_->erase("documents", "doc1").ok);
    
    // Search should return nothing
    auto [st2, pks2] = idx_->scanFulltext("documents", "content", "hello");
    ASSERT_TRUE(st2.ok);
    EXPECT_TRUE(pks2.empty());
}
