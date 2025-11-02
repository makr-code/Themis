// Test: Stemming functionality for fulltext indexes

#include <gtest/gtest.h>
#include "index/secondary_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include "utils/stemmer.h"
#include <filesystem>

namespace fs = std::filesystem;
using namespace themis;

class StemmingTest : public ::testing::Test {
protected:
    void SetUp() override {
        dbPath_ = "test_stemming_db";
        if (fs::exists(dbPath_)) {
            fs::remove_all(dbPath_);
        }
    RocksDBWrapper::Config cfg;
    cfg.db_path = dbPath_;
    db_ = std::make_shared<RocksDBWrapper>(cfg);
    ASSERT_TRUE(db_->open());
        idx_ = std::make_shared<SecondaryIndexManager>(*db_);
    }

    void TearDown() override {
        idx_.reset();
        db_.reset();
        if (fs::exists(dbPath_)) {
            fs::remove_all(dbPath_);
        }
    }

    std::string dbPath_;
    std::shared_ptr<RocksDBWrapper> db_;
    std::shared_ptr<SecondaryIndexManager> idx_;
};

// ============================================================================
// Stemmer Unit Tests
// ============================================================================

TEST_F(StemmingTest, Stemmer_EnglishPlurals) {
    using Stemmer = utils::Stemmer;
    
    EXPECT_EQ(Stemmer::stem("cats", Stemmer::Language::EN), "cat");
    EXPECT_EQ(Stemmer::stem("dogs", Stemmer::Language::EN), "dog");
    EXPECT_EQ(Stemmer::stem("cities", Stemmer::Language::EN), "citi");  // ies->i
    EXPECT_EQ(Stemmer::stem("caresses", Stemmer::Language::EN), "caress");  // sses->ss
}

TEST_F(StemmingTest, Stemmer_EnglishEdIng) {
    using Stemmer = utils::Stemmer;
    
    EXPECT_EQ(Stemmer::stem("walked", Stemmer::Language::EN), "walk");
    EXPECT_EQ(Stemmer::stem("running", Stemmer::Language::EN), "run");  // Double consonant
    EXPECT_EQ(Stemmer::stem("played", Stemmer::Language::EN), "play");
    EXPECT_EQ(Stemmer::stem("trying", Stemmer::Language::EN), "try");  // y->i
}

TEST_F(StemmingTest, Stemmer_EnglishSuffixes) {
    using Stemmer = utils::Stemmer;
    
    EXPECT_EQ(Stemmer::stem("relational", Stemmer::Language::EN), "relate");
    EXPECT_EQ(Stemmer::stem("conditional", Stemmer::Language::EN), "condition");
    EXPECT_EQ(Stemmer::stem("valenci", Stemmer::Language::EN), "valenc");  // i->() if short
}

TEST_F(StemmingTest, Stemmer_GermanSuffixes) {
    using Stemmer = utils::Stemmer;
    
    EXPECT_EQ(Stemmer::stem("laufen", Stemmer::Language::DE), "lauf");
    EXPECT_EQ(Stemmer::stem("machte", Stemmer::Language::DE), "macht");
    EXPECT_EQ(Stemmer::stem("gruppen", Stemmer::Language::DE), "grupp");
    EXPECT_EQ(Stemmer::stem("wirkung", Stemmer::Language::DE), "wirk");  // ung suffix
}

TEST_F(StemmingTest, Stemmer_NoStemming) {
    using Stemmer = utils::Stemmer;
    
    std::string word = "example";
    EXPECT_EQ(Stemmer::stem(word, Stemmer::Language::NONE), word);
}

TEST_F(StemmingTest, Stemmer_MinLength) {
    using Stemmer = utils::Stemmer;
    
    // Words shorter than 3 chars should not be stemmed
    EXPECT_EQ(Stemmer::stem("is", Stemmer::Language::EN), "is");
    EXPECT_EQ(Stemmer::stem("a", Stemmer::Language::EN), "a");
}

TEST_F(StemmingTest, Stemmer_ParseLanguage) {
    using Stemmer = utils::Stemmer;
    
    EXPECT_EQ(Stemmer::parseLanguage("en"), Stemmer::Language::EN);
    EXPECT_EQ(Stemmer::parseLanguage("de"), Stemmer::Language::DE);
    EXPECT_EQ(Stemmer::parseLanguage("none"), Stemmer::Language::NONE);
    EXPECT_EQ(Stemmer::parseLanguage("fr"), Stemmer::Language::NONE);  // Unknown -> NONE
}

// ============================================================================
// Fulltext Index Config Tests
// ============================================================================

TEST_F(StemmingTest, FulltextConfig_CreateWithStemming) {
    SecondaryIndexManager::FulltextConfig config;
    config.stemming_enabled = true;
    config.language = "en";
    
    auto st = idx_->createFulltextIndex("docs", "text", config);
    ASSERT_TRUE(st.ok);
    
    // Verify config is persisted
    auto retrievedConfig = idx_->getFulltextConfig("docs", "text");
    ASSERT_TRUE(retrievedConfig.has_value());
    EXPECT_TRUE(retrievedConfig->stemming_enabled);
    EXPECT_EQ(retrievedConfig->language, "en");
}

TEST_F(StemmingTest, FulltextConfig_CreateWithoutStemming) {
    SecondaryIndexManager::FulltextConfig config;
    config.stemming_enabled = false;
    config.language = "none";
    
    auto st = idx_->createFulltextIndex("docs", "text", config);
    ASSERT_TRUE(st.ok);
    
    auto retrievedConfig = idx_->getFulltextConfig("docs", "text");
    ASSERT_TRUE(retrievedConfig.has_value());
    EXPECT_FALSE(retrievedConfig->stemming_enabled);
    EXPECT_EQ(retrievedConfig->language, "none");
}

TEST_F(StemmingTest, FulltextConfig_DefaultConfig) {
    // Default config (backward compatibility)
    auto st = idx_->createFulltextIndex("docs", "text");
    ASSERT_TRUE(st.ok);
    
    auto config = idx_->getFulltextConfig("docs", "text");
    ASSERT_TRUE(config.has_value());
    EXPECT_FALSE(config->stemming_enabled);  // Default: no stemming
    EXPECT_EQ(config->language, "none");
}

TEST_F(StemmingTest, FulltextConfig_GetNonexistent) {
    auto config = idx_->getFulltextConfig("nonexistent", "column");
    EXPECT_FALSE(config.has_value());
}

// ============================================================================
// Integration Tests: Stemming in Index Maintenance
// ============================================================================

TEST_F(StemmingTest, Integration_EnglishStemming) {
    // Create index with English stemming
    SecondaryIndexManager::FulltextConfig config{true, "en"};
    auto st = idx_->createFulltextIndex("articles", "content", config);
    ASSERT_TRUE(st.ok);
    
    // Insert documents with different word forms
    BaseEntity doc1("doc1");
    doc1.setField("content", "running dogs");
    idx_->put("articles", doc1);
    
    BaseEntity doc2("doc2");
    doc2.setField("content", "cats run fast");
    idx_->put("articles", doc2);
    
    // Search with base form should match variations
    auto [status1, results1] = idx_->scanFulltext("articles", "content", "run");
    ASSERT_TRUE(status1.ok);
    EXPECT_EQ(results1.size(), 2);  // Matches "running" and "run"
    
    auto [status2, results2] = idx_->scanFulltext("articles", "content", "cat");
    ASSERT_TRUE(status2.ok);
    EXPECT_EQ(results2.size(), 1);  // Matches "cats"
    
    // Search with inflected form should also work
    auto [status3, results3] = idx_->scanFulltext("articles", "content", "dogs");
    ASSERT_TRUE(status3.ok);
    EXPECT_EQ(results3.size(), 1);  // "dogs" stems to "dog", matches doc1
}

TEST_F(StemmingTest, Integration_GermanStemming) {
    // Create index with German stemming
    SecondaryIndexManager::FulltextConfig config{true, "de"};
    auto st = idx_->createFulltextIndex("dokumente", "inhalt", config);
    ASSERT_TRUE(st.ok);
    
    // Insert documents
    BaseEntity doc1("doc1");
    doc1.setField("inhalt", "laufen und springen");
    idx_->put("dokumente", doc1);
    
    BaseEntity doc2("doc2");
    doc2.setField("inhalt", "der läufer läuft schnell");
    idx_->put("dokumente", doc2);
    
    // Search should work with stem
    auto [status, results] = idx_->scanFulltext("dokumente", "inhalt", "lauf");
    ASSERT_TRUE(status.ok);
    EXPECT_GE(results.size(), 1);  // At least matches "laufen"
}

TEST_F(StemmingTest, Integration_NoStemmingExactMatch) {
    // Create index without stemming
    SecondaryIndexManager::FulltextConfig config{false, "none"};
    auto st = idx_->createFulltextIndex("articles", "content", config);
    ASSERT_TRUE(st.ok);
    
    BaseEntity doc1("doc1");
    doc1.setField("content", "running fast");
    idx_->put("articles", doc1);
    
    // Exact match works
    auto [status1, results1] = idx_->scanFulltext("articles", "content", "running");
    ASSERT_TRUE(status1.ok);
    EXPECT_EQ(results1.size(), 1);
    
    // Stem form does NOT match (no stemming enabled)
    auto [status2, results2] = idx_->scanFulltext("articles", "content", "run");
    ASSERT_TRUE(status2.ok);
    EXPECT_EQ(results2.size(), 0);
}

TEST_F(StemmingTest, Integration_DeleteWithStemming) {
    // Create index with stemming
    SecondaryIndexManager::FulltextConfig config{true, "en"};
    auto st = idx_->createFulltextIndex("docs", "text", config);
    ASSERT_TRUE(st.ok);
    
    BaseEntity doc("doc1");
    doc.setField("text", "running dogs");
    idx_->put("docs", doc);
    
    // Verify indexed
    auto [status1, results1] = idx_->scanFulltext("docs", "text", "run");
    ASSERT_TRUE(status1.ok);
    EXPECT_EQ(results1.size(), 1);
    
    // Delete document
    idx_->erase("docs", "doc1");
    
    // Should be removed from index
    auto [status2, results2] = idx_->scanFulltext("docs", "text", "run");
    ASSERT_TRUE(status2.ok);
    EXPECT_EQ(results2.size(), 0);
}

// ============================================================================
// BM25 Scoring with Stemming
// ============================================================================

TEST_F(StemmingTest, BM25_StemmingRelevance) {
    // Create index with stemming
    SecondaryIndexManager::FulltextConfig config{true, "en"};
    auto st = idx_->createFulltextIndex("articles", "content", config);
    ASSERT_TRUE(st.ok);
    
    BaseEntity doc1("doc1");
    doc1.setField("content", "machine learning algorithms");
    idx_->put("articles", doc1);
    
    BaseEntity doc2("doc2");
    doc2.setField("content", "machines learn from data");
    idx_->put("articles", doc2);
    
    BaseEntity doc3("doc3");
    doc3.setField("content", "deep neural networks");
    idx_->put("articles", doc3);
    
    // Query with stemmed terms
    auto [status, results] = idx_->scanFulltextWithScores("articles", "content", "machine learning", 10);
    ASSERT_TRUE(status.ok);
    
    // Should match both doc1 and doc2 ("machines" -> "machine", "learn" -> "learn")
    EXPECT_GE(results.size(), 2);
    
    // doc1 should rank higher (more matches)
    if (results.size() >= 2) {
        EXPECT_EQ(results[0].pk, "doc1");
        EXPECT_GT(results[0].score, results[1].score);
    }
}

// no main() here - use the shared test runner
