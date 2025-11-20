#include <gtest/gtest.h>
#include "index/vector_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include <filesystem>
#include <cmath>

namespace fs = std::filesystem;

class VectorStatsTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_db_path_ = "./data/themis_vector_stats_test";
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

// =============================================================================
// Batch Operations Tests
// =============================================================================

TEST_F(VectorStatsTest, AddBatch_MultipleEntities) {
    vector_mgr_->init("documents", 3, themis::VectorIndexManager::Metric::COSINE);
    
    std::vector<themis::BaseEntity> entities;
    for (int i = 0; i < 10; ++i) {
        std::string pk = "doc" + std::to_string(i);
        themis::BaseEntity entity(pk);
        entity.setField("id", themis::Value(pk));
        entity.setField("embedding", themis::Value(std::vector<float>{
            static_cast<float>(i), 
            static_cast<float>(i * 2), 
            static_cast<float>(i * 3)
        }));
        entities.push_back(entity);
    }
    
    auto st = vector_mgr_->addBatch(entities);
    ASSERT_TRUE(st.ok) << st.message;
    EXPECT_EQ(vector_mgr_->getVectorCount(), 10);
}

TEST_F(VectorStatsTest, UpdateBatch_ModifiesMultipleVectors) {
    vector_mgr_->init("documents", 3, themis::VectorIndexManager::Metric::L2);
    
    // Add initial entities
    std::vector<themis::BaseEntity> entities;
    for (int i = 0; i < 5; ++i) {
        std::string pk = "doc" + std::to_string(i);
        themis::BaseEntity entity(pk);
        entity.setField("id", themis::Value(pk));
        entity.setField("embedding", themis::Value(std::vector<float>{1.0f, 2.0f, 3.0f}));
        entities.push_back(entity);
    }
    vector_mgr_->addBatch(entities);
    
    // Update all entities with new vectors
    entities.clear();
    for (int i = 0; i < 5; ++i) {
        std::string pk = "doc" + std::to_string(i);
        themis::BaseEntity entity(pk);
        entity.setField("id", themis::Value(pk));
        entity.setField("embedding", themis::Value(std::vector<float>{10.0f, 20.0f, 30.0f}));
        entities.push_back(entity);
    }
    
    auto st = vector_mgr_->updateBatch(entities);
    ASSERT_TRUE(st.ok) << st.message;
    EXPECT_EQ(vector_mgr_->getVectorCount(), 5);
}

TEST_F(VectorStatsTest, RemoveBatch_DeletesMultipleVectors) {
    vector_mgr_->init("documents", 3, themis::VectorIndexManager::Metric::COSINE);
    
    // Add entities
    std::vector<themis::BaseEntity> entities;
    for (int i = 0; i < 10; ++i) {
        std::string pk = "doc" + std::to_string(i);
        themis::BaseEntity entity(pk);
        entity.setField("id", themis::Value(pk));
        entity.setField("embedding", themis::Value(std::vector<float>{1.0f, 0.0f, 0.0f}));
        entities.push_back(entity);
    }
    vector_mgr_->addBatch(entities);
    EXPECT_EQ(vector_mgr_->getVectorCount(), 10);
    
    // Remove first 5
    std::vector<std::string> pks_to_remove = {"doc0", "doc1", "doc2", "doc3", "doc4"};
    auto st = vector_mgr_->removeBatch(pks_to_remove);
    ASSERT_TRUE(st.ok) << st.message;
    EXPECT_EQ(vector_mgr_->getVectorCount(), 5);
}

// =============================================================================
// Statistics Tests
// =============================================================================

TEST_F(VectorStatsTest, GetStatistics_ReturnsBasicInfo) {
    vector_mgr_->init("documents", 3, themis::VectorIndexManager::Metric::COSINE);
    
    // Add some vectors
    std::vector<themis::BaseEntity> entities;
    for (int i = 0; i < 5; ++i) {
        std::string pk = "doc" + std::to_string(i);
        themis::BaseEntity entity(pk);
        entity.setField("embedding", themis::Value(std::vector<float>{
            static_cast<float>(i), 0.0f, 0.0f
        }));
        entities.push_back(entity);
    }
    vector_mgr_->addBatch(entities);
    
    auto [st, stats] = vector_mgr_->getStatistics();
    ASSERT_TRUE(st.ok) << st.message;
    
    EXPECT_EQ(stats.vector_count, 5);
    EXPECT_EQ(stats.dimension, 3);
    EXPECT_EQ(stats.metric_name, "COSINE");
    EXPECT_GE(stats.mean_distance, 0.0f);
}

TEST_F(VectorStatsTest, ComputeCentroid_ReturnsAverageVector) {
    vector_mgr_->init("documents", 3, themis::VectorIndexManager::Metric::L2);
    
    // Add vectors: [1,0,0], [0,1,0], [0,0,1]
    // Centroid should be [0.33, 0.33, 0.33]
    std::vector<themis::BaseEntity> entities;
    
    themis::BaseEntity e1("doc1");
    e1.setField("embedding", themis::Value(std::vector<float>{1.0f, 0.0f, 0.0f}));
    entities.push_back(e1);
    
    themis::BaseEntity e2("doc2");
    e2.setField("embedding", themis::Value(std::vector<float>{0.0f, 1.0f, 0.0f}));
    entities.push_back(e2);
    
    themis::BaseEntity e3("doc3");
    e3.setField("embedding", themis::Value(std::vector<float>{0.0f, 0.0f, 1.0f}));
    entities.push_back(e3);
    
    vector_mgr_->addBatch(entities);
    
    auto [st, centroid] = vector_mgr_->computeCentroid();
    ASSERT_TRUE(st.ok) << st.message;
    ASSERT_EQ(centroid.size(), 3);
    
    EXPECT_NEAR(centroid[0], 0.333f, 0.01f);
    EXPECT_NEAR(centroid[1], 0.333f, 0.01f);
    EXPECT_NEAR(centroid[2], 0.333f, 0.01f);
}

TEST_F(VectorStatsTest, ComputeVariance_ReturnsDimensionWiseVariance) {
    vector_mgr_->init("documents", 2, themis::VectorIndexManager::Metric::L2);
    
    // Add vectors with known variance
    std::vector<themis::BaseEntity> entities;
    
    themis::BaseEntity e1("doc1");
    e1.setField("embedding", themis::Value(std::vector<float>{1.0f, 1.0f}));
    entities.push_back(e1);
    
    themis::BaseEntity e2("doc2");
    e2.setField("embedding", themis::Value(std::vector<float>{3.0f, 3.0f}));
    entities.push_back(e2);
    
    themis::BaseEntity e3("doc3");
    e3.setField("embedding", themis::Value(std::vector<float>{5.0f, 5.0f}));
    entities.push_back(e3);
    
    vector_mgr_->addBatch(entities);
    
    auto [st, variance] = vector_mgr_->computeVariance();
    ASSERT_TRUE(st.ok) << st.message;
    ASSERT_EQ(variance.size(), 2);
    
    // Variance of [1, 3, 5] = ((1-3)^2 + (3-3)^2 + (5-3)^2) / 3 = 8/3 = 2.667
    EXPECT_NEAR(variance[0], 2.667f, 0.01f);
    EXPECT_NEAR(variance[1], 2.667f, 0.01f);
}

TEST_F(VectorStatsTest, FindOutliers_IdentifiesFarVectors) {
    vector_mgr_->init("documents", 2, themis::VectorIndexManager::Metric::L2);
    
    std::vector<themis::BaseEntity> entities;
    
    // Normal cluster around origin
    for (int i = 0; i < 5; ++i) {
        std::string pk = "normal" + std::to_string(i);
        themis::BaseEntity entity(pk);
        entity.setField("embedding", themis::Value(std::vector<float>{
            static_cast<float>(i * 0.1f), 
            static_cast<float>(i * 0.1f)
        }));
        entities.push_back(entity);
    }
    
    // Outlier far away
    themis::BaseEntity outlier("outlier1");
    outlier.setField("embedding", themis::Value(std::vector<float>{100.0f, 100.0f}));
    entities.push_back(outlier);
    
    vector_mgr_->addBatch(entities);
    
    auto [st, outliers] = vector_mgr_->findOutliers(2.0f);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Should find the outlier
    EXPECT_GE(outliers.size(), 1);
    if (outliers.size() > 0) {
        EXPECT_EQ(outliers[0], "outlier1");
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
