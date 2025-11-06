// Test: Stopwords functionality for fulltext indexes

#include <gtest/gtest.h>
#include "index/secondary_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include <filesystem>

namespace fs = std::filesystem;
using namespace themis;

class StopwordsTest : public ::testing::Test {
protected:
    void SetUp() override {
        dbPath_ = "test_stopwords_db";
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

// =============================================================================
// Config persistence
// =============================================================================

TEST_F(StopwordsTest, Config_PersistedWithDefaults) {
    SecondaryIndexManager::FulltextConfig config;
    config.stemming_enabled = false;
    config.language = "en";
    config.stopwords_enabled = true; // default list

    auto st = idx_->createFulltextIndex("docs", "text", config);
    ASSERT_TRUE(st.ok);

    auto conf = idx_->getFulltextConfig("docs", "text");
    ASSERT_TRUE(conf.has_value());
    EXPECT_FALSE(conf->stemming_enabled);
    EXPECT_EQ(conf->language, "en");
    EXPECT_TRUE(conf->stopwords_enabled);
}

TEST_F(StopwordsTest, Config_CustomStopwordsPersisted) {
    SecondaryIndexManager::FulltextConfig config;
    config.stemming_enabled = false;
    config.language = "none"; // only custom list
    config.stopwords_enabled = true;
    config.stopwords = {"foo", "bar"};

    auto st = idx_->createFulltextIndex("docs", "text", config);
    ASSERT_TRUE(st.ok);

    auto conf = idx_->getFulltextConfig("docs", "text");
    ASSERT_TRUE(conf.has_value());
    EXPECT_TRUE(conf->stopwords_enabled);
    ASSERT_EQ(conf->stopwords.size(), 2u);
    EXPECT_EQ(conf->stopwords[0], "foo");
    EXPECT_EQ(conf->stopwords[1], "bar");
}

// =============================================================================
// Integration behavior
// =============================================================================

TEST_F(StopwordsTest, EN_DefaultStopwords_FilteredInQueryAndIndex) {
    SecondaryIndexManager::FulltextConfig config;
    config.stemming_enabled = false;
    config.language = "en";
    config.stopwords_enabled = true; // enable default EN stopwords

    auto st = idx_->createFulltextIndex("articles", "content", config);
    ASSERT_TRUE(st.ok);

    BaseEntity d1("doc1");
    d1.setField("content", "the quick brown fox");
    idx_->put("articles", d1);

    // Query on a stopword-only term should return no results
    {
        auto [status, results] = idx_->scanFulltext("articles", "content", "the");
        ASSERT_TRUE(status.ok);
        EXPECT_EQ(results.size(), 0u);
    }

    // Stopword in query is removed; AND behaves as if only non-stopwords remain
    {
        auto [status, results] = idx_->scanFulltext("articles", "content", "the quick");
        ASSERT_TRUE(status.ok);
        ASSERT_EQ(results.size(), 1u);
        EXPECT_EQ(results[0], "doc1");
    }
}

TEST_F(StopwordsTest, EN_NoStopwords_StopwordIsIndexedAndQueryable) {
    SecondaryIndexManager::FulltextConfig config;
    config.stemming_enabled = false;
    config.language = "en";
    config.stopwords_enabled = false;

    auto st = idx_->createFulltextIndex("articles", "content", config);
    ASSERT_TRUE(st.ok);

    BaseEntity d1("doc1");
    d1.setField("content", "the quick brown fox");
    idx_->put("articles", d1);

    // Query for stopword should match when stopwords are disabled
    auto [status, results] = idx_->scanFulltext("articles", "content", "the");
    ASSERT_TRUE(status.ok);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0], "doc1");
}

TEST_F(StopwordsTest, CustomStopwords_Filtered) {
    SecondaryIndexManager::FulltextConfig config;
    config.stemming_enabled = false;
    config.language = "none"; // only custom list applies
    config.stopwords_enabled = true;
    config.stopwords = {"foo"};

    auto st = idx_->createFulltextIndex("notes", "body", config);
    ASSERT_TRUE(st.ok);

    BaseEntity d1("n1");
    d1.setField("body", "foo bar baz");
    idx_->put("notes", d1);

    // Query consisting solely of a custom stopword yields no results
    {
        auto [status, results] = idx_->scanFulltext("notes", "body", "foo");
        ASSERT_TRUE(status.ok);
        EXPECT_EQ(results.size(), 0u);
    }

    // Mixed query behaves as if custom stopword removed
    {
        auto [status, results] = idx_->scanFulltext("notes", "body", "foo bar");
        ASSERT_TRUE(status.ok);
        ASSERT_EQ(results.size(), 1u);
        EXPECT_EQ(results[0], "n1");
    }
}
