#include <gtest/gtest.h>
#include "index/vector_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include <filesystem>
#include <cmath>

namespace fs = std::filesystem;

class VectorIndexTest : public ::testing::Test {
protected:
    void SetUp() override {
    test_db_path_ = "./data/themis_vector_index_test";
        fs::remove_all(test_db_path_);
        
        themis::RocksDBWrapper::Config config;
        config.db_path = test_db_path_;
        config.memtable_size_mb = 64;
        config.block_cache_size_mb = 256;
        config.max_background_jobs = 2;
        config.compression_default = "lz4";
        config.compression_bottommost = "zstd";
        
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

TEST_F(VectorIndexTest, Init_CreatesIndex) {
    auto st = vector_mgr_->init("documents", 3, themis::VectorIndexManager::Metric::COSINE);
    ASSERT_TRUE(st.ok) << st.message;
}

TEST_F(VectorIndexTest, AddEntity_StoresVector) {
    vector_mgr_->init("documents", 3, themis::VectorIndexManager::Metric::COSINE);
    
    themis::BaseEntity entity("doc1");
    entity.setField("id", "doc1");
    entity.setField("embedding", std::vector<float>{1.0f, 0.0f, 0.0f});
    
    auto st = vector_mgr_->addEntity(entity, "embedding");
    ASSERT_TRUE(st.ok) << st.message;
}

TEST_F(VectorIndexTest, SearchKnn_FindsNearestNeighbors) {
    vector_mgr_->init("documents", 3, themis::VectorIndexManager::Metric::COSINE);
    
    // Add 3 vectors
    themis::BaseEntity e1("doc1");
    e1.setField("embedding", std::vector<float>{1.0f, 0.0f, 0.0f});
    vector_mgr_->addEntity(e1, "embedding");
    
    themis::BaseEntity e2("doc2");
    e2.setField("embedding", std::vector<float>{0.0f, 1.0f, 0.0f});
    vector_mgr_->addEntity(e2, "embedding");
    
    themis::BaseEntity e3("doc3");
    e3.setField("embedding", std::vector<float>{0.9f, 0.1f, 0.0f}); // Similar to doc1
    vector_mgr_->addEntity(e3, "embedding");
    
    // Search for nearest to [1, 0, 0]
    std::vector<float> query{1.0f, 0.0f, 0.0f};
    auto [st, results] = vector_mgr_->searchKnn(query, 2);
    
    ASSERT_TRUE(st.ok) << st.message;
    ASSERT_GE(results.size(), 1u);
    // doc1 or doc3 should be closest
    EXPECT_TRUE(results[0].pk == "doc1" || results[0].pk == "doc3");
}

TEST_F(VectorIndexTest, SearchKnn_WithWhitelist) {
    vector_mgr_->init("documents", 3, themis::VectorIndexManager::Metric::L2);
    
    themis::BaseEntity e1("doc1");
    e1.setField("embedding", std::vector<float>{1.0f, 0.0f, 0.0f});
    vector_mgr_->addEntity(e1, "embedding");
    
    themis::BaseEntity e2("doc2");
    e2.setField("embedding", std::vector<float>{0.0f, 1.0f, 0.0f});
    vector_mgr_->addEntity(e2, "embedding");
    
    themis::BaseEntity e3("doc3");
    e3.setField("embedding", std::vector<float>{0.9f, 0.0f, 0.0f});
    vector_mgr_->addEntity(e3, "embedding");
    
    // Search with whitelist: only doc2 and doc3
    std::vector<std::string> whitelist{"doc2", "doc3"};
    std::vector<float> query{1.0f, 0.0f, 0.0f};
    auto [st, results] = vector_mgr_->searchKnn(query, 2, &whitelist);
    
    ASSERT_TRUE(st.ok);
    ASSERT_GE(results.size(), 1u);
    // doc3 should be closest among whitelist
    EXPECT_EQ(results[0].pk, "doc3");
}

TEST_F(VectorIndexTest, RemoveByPk_DeletesVector) {
    vector_mgr_->init("documents", 3, themis::VectorIndexManager::Metric::COSINE);
    
    themis::BaseEntity e1("doc1");
    e1.setField("embedding", std::vector<float>{1.0f, 0.0f, 0.0f});
    vector_mgr_->addEntity(e1, "embedding");
    
    auto st = vector_mgr_->removeByPk("doc1");
    ASSERT_TRUE(st.ok);
    
    // Search should return empty or not find doc1
    std::vector<float> query{1.0f, 0.0f, 0.0f};
    auto [st2, results] = vector_mgr_->searchKnn(query, 1);
    ASSERT_TRUE(st2.ok);
    // Either empty or doesn't contain doc1
    for (const auto& r : results) {
        EXPECT_NE(r.pk, "doc1");
    }
}

TEST_F(VectorIndexTest, UpdateEntity_UpdatesVector) {
    vector_mgr_->init("documents", 3, themis::VectorIndexManager::Metric::L2);
    
    themis::BaseEntity e1("doc1");
    e1.setField("embedding", std::vector<float>{1.0f, 0.0f, 0.0f});
    vector_mgr_->addEntity(e1, "embedding");
    
    // Update to different vector
    themis::BaseEntity e1_updated("doc1");
    e1_updated.setField("embedding", std::vector<float>{0.0f, 1.0f, 0.0f});
    auto st = vector_mgr_->updateEntity(e1_updated, "embedding");
    ASSERT_TRUE(st.ok);
    
    // Search for [0, 1, 0] should find doc1
    std::vector<float> query{0.0f, 1.0f, 0.0f};
    auto [st2, results] = vector_mgr_->searchKnn(query, 1);
    ASSERT_TRUE(st2.ok);
    ASSERT_GE(results.size(), 1u);
    EXPECT_EQ(results[0].pk, "doc1");
}

TEST_F(VectorIndexTest, CosineVsL2_DifferentRanking) {
    // Test that Cosine and L2 can produce different rankings
    // Cosine cares about direction, L2 cares about absolute distance
    
    // L2 Index
    auto st_l2 = vector_mgr_->init("docs_l2", 2, themis::VectorIndexManager::Metric::L2);
    ASSERT_TRUE(st_l2.ok);
    
    themis::BaseEntity e1("doc1");
    e1.setField("vec", std::vector<float>{1.0f, 0.0f});
    vector_mgr_->addEntity(e1, "vec");
    
    themis::BaseEntity e2("doc2");
    e2.setField("vec", std::vector<float>{10.0f, 0.0f}); // Same direction, 10x magnitude
    vector_mgr_->addEntity(e2, "vec");
    
    themis::BaseEntity e3("doc3");
    e3.setField("vec", std::vector<float>{0.7f, 0.7f}); // 45 degrees
    vector_mgr_->addEntity(e3, "vec");
    
    std::vector<float> query{1.0f, 0.0f};
    auto [st_l2_search, results_l2] = vector_mgr_->searchKnn(query, 3);
    ASSERT_TRUE(st_l2_search.ok);
    ASSERT_EQ(results_l2.size(), 3u);
    // L2: doc1 closest (distance 0), then doc3, then doc2 (distance 9)
    EXPECT_EQ(results_l2[0].pk, "doc1");
    
    // Cosine Index (new index)
    vector_mgr_.reset();
    vector_mgr_ = std::make_unique<themis::VectorIndexManager>(*db_);
    auto st_cos = vector_mgr_->init("docs_cos", 2, themis::VectorIndexManager::Metric::COSINE);
    ASSERT_TRUE(st_cos.ok);
    
    themis::BaseEntity c1("doc1");
    c1.setField("vec", std::vector<float>{1.0f, 0.0f});
    vector_mgr_->addEntity(c1, "vec");
    
    themis::BaseEntity c2("doc2");
    c2.setField("vec", std::vector<float>{10.0f, 0.0f}); // Same direction
    vector_mgr_->addEntity(c2, "vec");
    
    themis::BaseEntity c3("doc3");
    c3.setField("vec", std::vector<float>{0.7f, 0.7f}); // Different direction
    vector_mgr_->addEntity(c3, "vec");
    
    auto [st_cos_search, results_cos] = vector_mgr_->searchKnn(query, 3);
    ASSERT_TRUE(st_cos_search.ok);
    ASSERT_EQ(results_cos.size(), 3u);
    // Cosine: doc1 and doc2 should have same/similar score (same direction after normalization)
    // Both should rank higher than doc3
    EXPECT_TRUE(results_cos[0].pk == "doc1" || results_cos[0].pk == "doc2");
    EXPECT_TRUE(results_cos[1].pk == "doc1" || results_cos[1].pk == "doc2");
    EXPECT_EQ(results_cos[2].pk, "doc3");
}

TEST_F(VectorIndexTest, DotProductMetric_NoNormalization) {
    // DOT metric uses raw dot product (negated for distance)
    // Unlike COSINE, vectors are NOT normalized
    auto st_dot = vector_mgr_->init("docs_dot", 2, themis::VectorIndexManager::Metric::DOT);
    ASSERT_TRUE(st_dot.ok);
    
    // Add vectors with different magnitudes
    themis::BaseEntity e1("doc1");
    e1.setField("vec", std::vector<float>{1.0f, 0.0f});
    vector_mgr_->addEntity(e1, "vec");
    
    themis::BaseEntity e2("doc2");
    e2.setField("vec", std::vector<float>{10.0f, 0.0f}); // Same direction, 10x magnitude
    vector_mgr_->addEntity(e2, "vec");
    
    themis::BaseEntity e3("doc3");
    e3.setField("vec", std::vector<float>{0.5f, 0.5f}); // Different direction, smaller magnitude
    vector_mgr_->addEntity(e3, "vec");
    
    // Query with [1, 0]
    std::vector<float> query{1.0f, 0.0f};
    auto [st_search, results] = vector_mgr_->searchKnn(query, 3);
    ASSERT_TRUE(st_search.ok);
    ASSERT_EQ(results.size(), 3u);
    
    // DOT: Higher dot product = more similar (lower distance after negation)
    // doc2: dot = 10.0 (highest)
    // doc1: dot = 1.0
    // doc3: dot = 0.5 (lowest)
    EXPECT_EQ(results[0].pk, "doc2"); // Highest dot product
    EXPECT_EQ(results[1].pk, "doc1");
    EXPECT_EQ(results[2].pk, "doc3");
    
    // Verify distances are negated dot products (lower distance = better)
    // Distance for doc2 should be -10.0
    EXPECT_LT(results[0].distance, results[1].distance);
    EXPECT_LT(results[1].distance, results[2].distance);
}

TEST_F(VectorIndexTest, PersistenceRoundtrip_SaveAndLoad) {
    // Create index with some data
    auto st_init = vector_mgr_->init("docs_persist", 3, themis::VectorIndexManager::Metric::COSINE);
    ASSERT_TRUE(st_init.ok);
    
    themis::BaseEntity e1("doc1");
    e1.setField("emb", std::vector<float>{1.0f, 0.0f, 0.0f});
    vector_mgr_->addEntity(e1, "emb");
    
    themis::BaseEntity e2("doc2");
    e2.setField("emb", std::vector<float>{0.0f, 1.0f, 0.0f});
    vector_mgr_->addEntity(e2, "emb");
    
    themis::BaseEntity e3("doc3");
    e3.setField("emb", std::vector<float>{0.0f, 0.0f, 1.0f});
    vector_mgr_->addEntity(e3, "emb");
    
    // Search before save
    std::vector<float> query{1.0f, 0.0f, 0.0f};
    auto [st_before, results_before] = vector_mgr_->searchKnn(query, 2);
    ASSERT_TRUE(st_before.ok);
    ASSERT_GE(results_before.size(), 1u);
    std::string first_pk_before = results_before[0].pk;
    
    // Save index to disk
    std::string save_dir = "./data/vector_index_save_test";
    fs::remove_all(save_dir);
    fs::create_directories(save_dir);
    auto st_save = vector_mgr_->saveIndex(save_dir);
    ASSERT_TRUE(st_save.ok) << st_save.message;
    
    // Verify files exist
    EXPECT_TRUE(fs::exists(save_dir + "/meta.txt"));
    EXPECT_TRUE(fs::exists(save_dir + "/labels.txt"));
    EXPECT_TRUE(fs::exists(save_dir + "/index.bin"));
    
    // Create new index manager and load
    vector_mgr_.reset();
    vector_mgr_ = std::make_unique<themis::VectorIndexManager>(*db_);
    
    // Init with same objectName before loading (loadIndex expects objectName_ to match)
    auto st_init_load = vector_mgr_->init("docs_persist", 3, themis::VectorIndexManager::Metric::COSINE);
    ASSERT_TRUE(st_init_load.ok);
    
    auto st_load = vector_mgr_->loadIndex(save_dir);
    ASSERT_TRUE(st_load.ok) << st_load.message;
    
    // Search after load - should give same results
    auto [st_after, results_after] = vector_mgr_->searchKnn(query, 2);
    ASSERT_TRUE(st_after.ok);
    ASSERT_GE(results_after.size(), 1u);
    EXPECT_EQ(results_after[0].pk, first_pk_before);
    
    // Cleanup
    fs::remove_all(save_dir);
}

TEST_F(VectorIndexTest, SetEfSearch_UpdatesSearchParameter) {
    auto st_init = vector_mgr_->init("docs_ef", 3, themis::VectorIndexManager::Metric::L2);
    ASSERT_TRUE(st_init.ok);
    
    // Add some vectors
    for (int i = 0; i < 10; i++) {
        themis::BaseEntity e("doc" + std::to_string(i));
        float x = static_cast<float>(i) / 10.0f;
        e.setField("vec", std::vector<float>{x, 0.0f, 0.0f});
        vector_mgr_->addEntity(e, "vec");
    }
    
    // Set efSearch to low value
    auto st_ef_low = vector_mgr_->setEfSearch(5);
    ASSERT_TRUE(st_ef_low.ok);
    
    std::vector<float> query{0.5f, 0.0f, 0.0f};
    auto [st1, results1] = vector_mgr_->searchKnn(query, 3);
    ASSERT_TRUE(st1.ok);
    
    // Set efSearch to high value (should improve recall)
    auto st_ef_high = vector_mgr_->setEfSearch(200);
    ASSERT_TRUE(st_ef_high.ok);
    
    auto [st2, results2] = vector_mgr_->searchKnn(query, 3);
    ASSERT_TRUE(st2.ok);
    
    // Both should return results (exact ranking may vary slightly)
    EXPECT_GE(results1.size(), 1u);
    EXPECT_GE(results2.size(), 1u);
}

TEST_F(VectorIndexTest, PersistenceLoadInvalidDirectory_ReturnsError) {
    std::string invalid_dir = "./data/nonexistent_index_dir";
    fs::remove_all(invalid_dir);
    
    auto st = vector_mgr_->loadIndex(invalid_dir);
    EXPECT_FALSE(st.ok);
    EXPECT_NE(st.message.find("meta.txt"), std::string::npos);
}
