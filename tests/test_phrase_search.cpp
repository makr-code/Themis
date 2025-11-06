// Test: Phrase search via quoted queries in fulltext

#include <gtest/gtest.h>
#include "index/secondary_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include <filesystem>

namespace fs = std::filesystem;
using namespace themis;

class PhraseSearchTest : public ::testing::Test {
protected:
    void SetUp() override {
        dbPath_ = "test_phrase_db";
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

TEST_F(PhraseSearchTest, ExactPhraseFiltersCandidates) {
    SecondaryIndexManager::FulltextConfig config;
    config.stemming_enabled = false;
    config.language = "en";
    config.stopwords_enabled = false;

    auto st = idx_->createFulltextIndex("docs", "text", config);
    ASSERT_TRUE(st.ok);

    BaseEntity d1("a"); d1.setField("text", "machine learning is fun");
    BaseEntity d2("b"); d2.setField("text", "learning about machine components");
    idx_->put("docs", d1);
    idx_->put("docs", d2);

    // Unquoted: both tokens present in both docs -> two results
    {
        auto [status, results] = idx_->scanFulltext("docs", "text", "machine learning");
        ASSERT_TRUE(status.ok);
        ASSERT_EQ(results.size(), 2u);
    }

    // Quoted phrase should match only d1
    {
        auto [status, results] = idx_->scanFulltext("docs", "text", "\"machine learning\"");
        ASSERT_TRUE(status.ok);
        ASSERT_EQ(results.size(), 1u);
        EXPECT_EQ(results[0], "a");
    }
}

TEST_F(PhraseSearchTest, PhraseWithUmlautNormalization) {
    SecondaryIndexManager::FulltextConfig config;
    config.stemming_enabled = false;
    config.language = "de";
    config.stopwords_enabled = false;
    config.normalize_umlauts = true;

    auto st = idx_->createFulltextIndex("docs", "text", config);
    ASSERT_TRUE(st.ok);

    BaseEntity d1("x"); d1.setField("text", "er läuft sehr schnell");
    idx_->put("docs", d1);

    // Phrase without umlaut should match with normalization enabled
    {
        auto [status, results] = idx_->scanFulltext("docs", "text", "\"er lauft\"");
        ASSERT_TRUE(status.ok);
        ASSERT_EQ(results.size(), 1u);
        EXPECT_EQ(results[0], "x");
    }
}
