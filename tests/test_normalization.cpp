// Test: German umlaut/ß normalization for fulltext indexes

#include <gtest/gtest.h>
#include "index/secondary_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include <filesystem>

namespace fs = std::filesystem;
using namespace themis;

class NormalizationTest : public ::testing::Test {
protected:
    void SetUp() override {
        dbPath_ = "test_norm_db";
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

TEST_F(NormalizationTest, GermanUmlautsEnabled) {
    SecondaryIndexManager::FulltextConfig config;
    config.stemming_enabled = true;
    config.language = "de";
    config.stopwords_enabled = false;
    config.normalize_umlauts = true;

    auto st = idx_->createFulltextIndex("docs", "text", config);
    ASSERT_TRUE(st.ok);

    BaseEntity d1("d1");
    d1.setField("text", "er läuft sehr schnell");
    idx_->put("docs", d1);

    // Query without umlaut should still match
    {
        auto [status, results] = idx_->scanFulltext("docs", "text", "lauft");
        ASSERT_TRUE(status.ok);
        ASSERT_EQ(results.size(), 1u);
        EXPECT_EQ(results[0], "d1");
    }

    // Original umlaut query should also match (normalized during query)
    {
        auto [status, results] = idx_->scanFulltext("docs", "text", "läuft");
        ASSERT_TRUE(status.ok);
        ASSERT_EQ(results.size(), 1u);
        EXPECT_EQ(results[0], "d1");
    }
}

TEST_F(NormalizationTest, GermanUmlautsDisabled) {
    SecondaryIndexManager::FulltextConfig config;
    config.stemming_enabled = false;
    config.language = "de";
    config.stopwords_enabled = false;
    config.normalize_umlauts = false;

    auto st = idx_->createFulltextIndex("docs", "text", config);
    ASSERT_TRUE(st.ok);

    BaseEntity d1("d1");
    d1.setField("text", "er läuft sehr schnell");
    idx_->put("docs", d1);

    // Without normalization, querying "lauft" should not match
    auto [status, results] = idx_->scanFulltext("docs", "text", "lauft");
    ASSERT_TRUE(status.ok);
    EXPECT_EQ(results.size(), 0u);
}
