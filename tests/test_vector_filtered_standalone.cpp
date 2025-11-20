#include <gtest/gtest.h>
#include "index/vector_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include <filesystem>

namespace fs = std::filesystem;

class VectorFilteredTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_db_path_ = "./data/themis_vector_filtered_test";
        fs::remove_all(test_db_path_);
        
        themis::RocksDBWrapper::Config config;
        config.db_path = test_db_path_;
        config.memtable_size_mb = 64;
        config.block_cache_size_mb = 256;
        
        db_ = std::make_unique<themis::RocksDBWrapper>(config);
        ASSERT_TRUE(db_->open());
        vector_mgr_ = std::make_unique<themis::VectorIndexManager>(*db_);
    }

    void TearDown() override {
        vector_mgr_.reset();
        db_.reset();
        fs::remove_all(test_db_path_);
    }

    std::string test_db_path_;
    std::unique_ptr<themis::RocksDBWrapper> db_;
    std::unique_ptr<themis::VectorIndexManager> vector_mgr_;
};

TEST_F(VectorFilteredTest, SearchKnnFiltered_AttributeEquals) {
    vector_mgr_->init("documents", 3, themis::VectorIndexManager::Metric::COSINE);
    
    // Add documents with categories
    themis::BaseEntity doc1("doc1");
    doc1.setField("id", themis::Value(std::string("doc1")));
    doc1.setField("category", themis::Value(std::string("science")));
    doc1.setField("embedding", themis::Value(std::vector<float>{1.0f, 0.0f, 0.0f}));
    vector_mgr_->addEntity(doc1);
    
    themis::BaseEntity doc2("doc2");
    doc2.setField("id", themis::Value(std::string("doc2")));
    doc2.setField("category", themis::Value(std::string("news")));
    doc2.setField("embedding", themis::Value(std::vector<float>{0.9f, 0.1f, 0.0f}));
    vector_mgr_->addEntity(doc2);
    
    themis::BaseEntity doc3("doc3");
    doc3.setField("id", themis::Value(std::string("doc3")));
    doc3.setField("category", themis::Value(std::string("science")));
    doc3.setField("embedding", themis::Value(std::vector<float>{0.8f, 0.0f, 0.2f}));
    vector_mgr_->addEntity(doc3);
    
    // Search with filter: only "science" category
    std::vector<float> query{1.0f, 0.0f, 0.0f};
    std::vector<themis::VectorIndexManager::AttributeFilter> filters;
    filters.push_back({"category", "science", themis::VectorIndexManager::AttributeFilter::Op::EQUALS});
    
    auto [st, results] = vector_mgr_->searchKnnFiltered(query, 2, filters);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Should only return doc1 and doc3 (both science category)
    EXPECT_EQ(results.size(), 2);
    if (results.size() >= 1) EXPECT_EQ(results[0].pk, "doc1");
    if (results.size() >= 2) EXPECT_EQ(results[1].pk, "doc3");
}

TEST_F(VectorFilteredTest, SearchKnnFiltered_MultipleFilters) {
    vector_mgr_->init("documents", 3, themis::VectorIndexManager::Metric::COSINE);
    
    themis::BaseEntity doc1("doc1");
    doc1.setField("id", themis::Value(std::string("doc1")));
    doc1.setField("category", themis::Value(std::string("science")));
    doc1.setField("status", themis::Value(std::string("active")));
    doc1.setField("embedding", themis::Value(std::vector<float>{1.0f, 0.0f, 0.0f}));
    vector_mgr_->addEntity(doc1);
    
    themis::BaseEntity doc2("doc2");
    doc2.setField("id", themis::Value(std::string("doc2")));
    doc2.setField("category", themis::Value(std::string("science")));
    doc2.setField("status", themis::Value(std::string("archived")));
    doc2.setField("embedding", themis::Value(std::vector<float>{0.95f, 0.05f, 0.0f}));
    vector_mgr_->addEntity(doc2);
    
    themis::BaseEntity doc3("doc3");
    doc3.setField("id", themis::Value(std::string("doc3")));
    doc3.setField("category", themis::Value(std::string("science")));
    doc3.setField("status", themis::Value(std::string("active")));
    doc3.setField("embedding", themis::Value(std::vector<float>{0.85f, 0.0f, 0.15f}));
    vector_mgr_->addEntity(doc3);
    
    // Filter: category=science AND status=active
    std::vector<float> query{1.0f, 0.0f, 0.0f};
    std::vector<themis::VectorIndexManager::AttributeFilter> filters;
    filters.push_back({"category", "science", themis::VectorIndexManager::AttributeFilter::Op::EQUALS});
    filters.push_back({"status", "active", themis::VectorIndexManager::AttributeFilter::Op::EQUALS});
    
    auto [st, results] = vector_mgr_->searchKnnFiltered(query, 2, filters);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Should return doc1 and doc3 (both science AND active)
    EXPECT_EQ(results.size(), 2);
    if (results.size() >= 1) EXPECT_EQ(results[0].pk, "doc1");
    if (results.size() >= 2) EXPECT_EQ(results[1].pk, "doc3");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
