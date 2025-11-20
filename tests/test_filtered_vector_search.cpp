// Tests for Filtered Vector Search (Phase 2.1)

#include <gtest/gtest.h>
#include "query/query_engine.h"
#include "index/vector_index.h"
#include "index/secondary_index.h"
#include "index/graph_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include <filesystem>
#include <cmath>

namespace themis {

class FilteredVectorSearchTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clean up test directory
        std::filesystem::remove_all(test_dir);
        std::filesystem::create_directories(test_dir);
        
        // Create DB
        RocksDBWrapper::Config cfg;
        cfg.db_path = test_dir + "/rocksdb";
        db = std::make_unique<RocksDBWrapper>(cfg);
        ASSERT_TRUE(db->open());
        
        // Create managers
        secIdx = std::make_unique<SecondaryIndexManager>(*db);
        vectorIdx = std::make_unique<VectorIndexManager>(*db);
        
        // Initialize vector index
        auto status = vectorIdx->init("documents", 128, VectorIndexManager::Metric::COSINE);
        ASSERT_TRUE(status.ok) << status.message;
        
        // Create secondary indexes for filtering
        ASSERT_TRUE(secIdx->createIndex("documents", "category").ok);
        ASSERT_TRUE(secIdx->createRangeIndex("documents", "score").ok);
        ASSERT_TRUE(secIdx->createIndex("documents", "lang").ok);
        
        // Insert test data
        insertTestData();
        
        // Debug: Verify vector index populated
        std::cout << "DEBUG SetUp: VectorIndex has " << vectorIdx->getVectorCount() << " vectors\n";
        
        // Debug: Verify secondary index populated
        auto [st, techDocs] = secIdx->scanKeysEqual("documents", "category", "tech");
        std::cout << "DEBUG SetUp: SecondaryIndex scan for category=tech returned " 
              << techDocs.size() << " results (status: " << st.message << ")\n";
    }
    
    void TearDown() override {
        vectorIdx.reset();
        secIdx.reset();
        db.reset();
        std::filesystem::remove_all(test_dir);
    }
    
    void insertTestData() {
        // Insert 100 documents with different categories and scores
        for (int i = 0; i < 100; ++i) {
            std::string pk = "doc_" + std::to_string(i);
            
            BaseEntity e(pk);
            
            // Categories: tech (60%), science (30%), art (10%)
            std::string category;
            if (i < 60) category = "tech";
            else if (i < 90) category = "science";
            else category = "art";
            e.setField("category", category);
            
            // Scores: 0.5-1.0 distributed
            double score = 0.5 + (i % 50) / 100.0;
            e.setField("score", score);
            
            // Language: en (80%), de (20%)
            std::string lang = (i % 5 == 0) ? "de" : "en";
            e.setField("lang", lang);
            
            // Generate embedding (128-dim, simple pattern based on category)
            std::vector<float> embedding(128);
            float base = (category == "tech") ? 0.1f : (category == "science") ? 0.5f : 0.9f;
            for (int d = 0; d < 128; ++d) {
                embedding[d] = base + (i % 10) * 0.01f + (d % 10) * 0.001f;
            }
            e.setField("embedding", embedding);
            
            // Add to vector index and secondary indexes
            ASSERT_TRUE(vectorIdx->addEntity(e, "embedding").ok);
            ASSERT_TRUE(secIdx->put("documents", e).ok);
        }
    }
    
    std::string test_dir = "./test_filtered_vector_search_tmp";
    std::unique_ptr<RocksDBWrapper> db;
    std::unique_ptr<SecondaryIndexManager> secIdx;
    std::unique_ptr<VectorIndexManager> vectorIdx;
};

// Test 1: Simple equality filter (category)
TEST_F(FilteredVectorSearchTest, EqualityFilter_Category) {
    GraphIndexManager graphIdx(*db);
    QueryEngine engine(*db, *secIdx, graphIdx, vectorIdx.get(), nullptr);
    
    // Query vector (similar to tech documents)
    std::vector<float> query(128);
    for (int d = 0; d < 128; ++d) {
        query[d] = 0.1f + (d % 10) * 0.001f;
    }
    
    FilteredVectorSearchQuery fvq;
    fvq.table = "documents";
    fvq.vector_field = "embedding";
    fvq.query_vector = query;
    fvq.k = 10;
    
    // Filter: category == "tech"
    FilteredVectorSearchQuery::AttributeFilter filter;
    filter.field = "category";
    filter.op = FilteredVectorSearchQuery::AttributeFilter::Op::EQUALS;
    filter.value = "tech";
    fvq.filters.push_back(filter);
    
    // Direct VectorIndexManager test
    std::vector<VectorIndexManager::AttributeFilterV2> vFilters;
    VectorIndexManager::AttributeFilterV2 vf;
    vf.field = "category";
    vf.value = "tech";
    vf.op = VectorIndexManager::AttributeFilterV2::Op::EQUALS;
    vFilters.push_back(vf);
    
    auto [vimStatus, vimResults] = vectorIdx->searchKnnPreFiltered(query, 10, vFilters, secIdx.get());
    std::cout << "DEBUG Test: VectorIndexManager::searchKnnPreFiltered returned status=" << vimStatus.ok
              << " message='" << vimStatus.message << "' results=" << vimResults.size() << "\n";
    if (!vimResults.empty()) {
        std::cout << "DEBUG Test: First result pk=" << vimResults[0].pk << " distance=" << vimResults[0].distance << "\n";
    }
    
    
    std::cout << "DEBUG Test: executeFilteredVectorSearch returned status=" << status.ok 
              << " message='" << status.message << "' results=" << results.size() << "\n";
    
    ASSERT_EQ(results.size(), 10);
    
    // Verify all results are tech category
    for (const auto& r : results) {
        EXPECT_EQ(r.entity["category"].get<std::string>(), "tech");
    }
}

// Test 2: Range filter (score >= 0.8)
TEST_F(FilteredVectorSearchTest, RangeFilter_ScoreGTE) {
    // Need GraphIndexManager placeholder for 3-arg constructor
    GraphIndexManager graphIdx(*db);
    QueryEngine engine(*db, *secIdx, graphIdx, vectorIdx.get(), nullptr);
    
    std::vector<float> query(128, 0.5f);
    
    FilteredVectorSearchQuery fvq;
    fvq.table = "documents";
    fvq.query_vector = query;
    fvq.k = 10;
    
    // Filter: score >= 0.8
    FilteredVectorSearchQuery::AttributeFilter filter;
    filter.field = "score";
    filter.op = FilteredVectorSearchQuery::AttributeFilter::Op::GREATER_EQUAL;
    filter.value = "0.8"; // String encoding for range index
    fvq.filters.push_back(filter);
    
    auto [status, results] = engine.executeFilteredVectorSearch(fvq);
    
    ASSERT_TRUE(status.ok) << status.message;
    EXPECT_GT(results.size(), 0);
    
    // Verify all results have score >= 0.8
    for (const auto& r : results) {
        double score = r.entity["score"].get<double>();
        EXPECT_GE(score, 0.8);
    }
}

// Test 3: Combined filters (category AND score range)
TEST_F(FilteredVectorSearchTest, CombinedFilters_CategoryAndScore) {
    GraphIndexManager graphIdx(*db);
    QueryEngine engine(*db, *secIdx, graphIdx, vectorIdx.get(), nullptr);
    
    std::vector<float> query(128, 0.5f);
    
    FilteredVectorSearchQuery fvq;
    fvq.table = "documents";
    fvq.query_vector = query;
    fvq.k = 5;
    
    // Filter 1: category == "science"
    FilteredVectorSearchQuery::AttributeFilter f1;
    f1.field = "category";
    f1.op = FilteredVectorSearchQuery::AttributeFilter::Op::EQUALS;
    f1.value = "science";
    fvq.filters.push_back(f1);
    
    // Filter 2: score >= 0.7
    FilteredVectorSearchQuery::AttributeFilter f2;
    f2.field = "score";
    f2.op = FilteredVectorSearchQuery::AttributeFilter::Op::GREATER_EQUAL;
    f2.value = "0.7";
    fvq.filters.push_back(f2);
    
    auto [status, results] = engine.executeFilteredVectorSearch(fvq);
    
    ASSERT_TRUE(status.ok) << status.message;
    EXPECT_GT(results.size(), 0);
    
    // Verify all results match both filters
    for (const auto& r : results) {
        EXPECT_EQ(r.entity["category"].get<std::string>(), "science");
        double score = r.entity["score"].get<double>();
        EXPECT_GE(score, 0.7);
    }
}

// Test 4: IN filter (multiple values)
TEST_F(FilteredVectorSearchTest, InFilter_MultipleCategories) {
    GraphIndexManager graphIdx(*db);
    QueryEngine engine(*db, *secIdx, graphIdx, vectorIdx.get(), nullptr);
    
    std::vector<float> query(128, 0.5f);
    
    FilteredVectorSearchQuery fvq;
    fvq.table = "documents";
    fvq.query_vector = query;
    fvq.k = 10;
    
    // Filter: category IN ["tech", "science"]
    FilteredVectorSearchQuery::AttributeFilter filter;
    filter.field = "category";
    filter.op = FilteredVectorSearchQuery::AttributeFilter::Op::IN;
    filter.values = {"tech", "science"};
    fvq.filters.push_back(filter);
    
    auto [status, results] = engine.executeFilteredVectorSearch(fvq);
    
    ASSERT_TRUE(status.ok) << status.message;
    ASSERT_EQ(results.size(), 10);
    
    // Verify all results are tech OR science
    for (const auto& r : results) {
        std::string cat = r.entity["category"].get<std::string>();
        EXPECT_TRUE(cat == "tech" || cat == "science");
    }
}

// Test 5: Range filter (score BETWEEN 0.6 AND 0.8)
TEST_F(FilteredVectorSearchTest, RangeFilter_ScoreBetween) {
    GraphIndexManager graphIdx(*db);
    QueryEngine engine(*db, *secIdx, graphIdx, vectorIdx.get(), nullptr);
    
    std::vector<float> query(128, 0.5f);
    
    FilteredVectorSearchQuery fvq;
    fvq.table = "documents";
    fvq.query_vector = query;
    fvq.k = 10;
    
    // Filter: 0.6 <= score <= 0.8
    FilteredVectorSearchQuery::AttributeFilter filter;
    filter.field = "score";
    filter.op = FilteredVectorSearchQuery::AttributeFilter::Op::RANGE;
    filter.value_min = "0.6";
    filter.value_max = "0.8";
    fvq.filters.push_back(filter);
    
    auto [status, results] = engine.executeFilteredVectorSearch(fvq);
    
    ASSERT_TRUE(status.ok) << status.message;
    EXPECT_GT(results.size(), 0);
    
    // Verify all results in range
    for (const auto& r : results) {
        double score = r.entity["score"].get<double>();
        EXPECT_GE(score, 0.6);
        EXPECT_LE(score, 0.8);
    }
}

// Test 6: Empty result set (highly selective filter)
TEST_F(FilteredVectorSearchTest, EmptyResultSet_HighlySelective) {
    GraphIndexManager graphIdx(*db);
    QueryEngine engine(*db, *secIdx, graphIdx, vectorIdx.get(), nullptr);
    
    std::vector<float> query(128, 0.5f);
    
    FilteredVectorSearchQuery fvq;
    fvq.table = "documents";
    fvq.query_vector = query;
    fvq.k = 10;
    
    // Impossible combination: art AND score > 1.0
    FilteredVectorSearchQuery::AttributeFilter f1;
    f1.field = "category";
    f1.op = FilteredVectorSearchQuery::AttributeFilter::Op::EQUALS;
    f1.value = "art";
    fvq.filters.push_back(f1);
    
    FilteredVectorSearchQuery::AttributeFilter f2;
    f2.field = "score";
    f2.op = FilteredVectorSearchQuery::AttributeFilter::Op::GREATER_THAN;
    f2.value = "1.0";
    fvq.filters.push_back(f2);
    
    auto [status, results] = engine.executeFilteredVectorSearch(fvq);
    
    ASSERT_TRUE(status.ok) << status.message;
    EXPECT_EQ(results.size(), 0); // No results expected
}

// Test 7: High selectivity (90% filtered out)
TEST_F(FilteredVectorSearchTest, HighSelectivity_SmallCategory) {
    GraphIndexManager graphIdx(*db);
    QueryEngine engine(*db, *secIdx, graphIdx, vectorIdx.get(), nullptr);
    
    std::vector<float> query(128, 0.9f); // Similar to art docs
    
    FilteredVectorSearchQuery fvq;
    fvq.table = "documents";
    fvq.query_vector = query;
    fvq.k = 5;
    
    // Filter: category == "art" (only 10% of docs)
    FilteredVectorSearchQuery::AttributeFilter filter;
    filter.field = "category";
    filter.op = FilteredVectorSearchQuery::AttributeFilter::Op::EQUALS;
    filter.value = "art";
    fvq.filters.push_back(filter);
    
    auto [status, results] = engine.executeFilteredVectorSearch(fvq);
    
    ASSERT_TRUE(status.ok) << status.message;
    EXPECT_LE(results.size(), 10); // Max 10 art docs exist
    
    // Verify all are art
    for (const auto& r : results) {
        EXPECT_EQ(r.entity["category"].get<std::string>(), "art");
    }
}

// Test 8: Triple filter (category + score + lang)
TEST_F(FilteredVectorSearchTest, TripleFilter_CategoryScoreLang) {
    GraphIndexManager graphIdx(*db);
    QueryEngine engine(*db, *secIdx, graphIdx, vectorIdx.get(), nullptr);
    
    std::vector<float> query(128, 0.1f);
    
    FilteredVectorSearchQuery fvq;
    fvq.table = "documents";
    fvq.query_vector = query;
    fvq.k = 3;
    
    // Filter 1: category == "tech"
    FilteredVectorSearchQuery::AttributeFilter f1;
    f1.field = "category";
    f1.op = FilteredVectorSearchQuery::AttributeFilter::Op::EQUALS;
    f1.value = "tech";
    fvq.filters.push_back(f1);
    
    // Filter 2: score < 0.7
    FilteredVectorSearchQuery::AttributeFilter f2;
    f2.field = "score";
    f2.op = FilteredVectorSearchQuery::AttributeFilter::Op::LESS_THAN;
    f2.value = "0.7";
    fvq.filters.push_back(f2);
    
    // Filter 3: lang == "en"
    FilteredVectorSearchQuery::AttributeFilter f3;
    f3.field = "lang";
    f3.op = FilteredVectorSearchQuery::AttributeFilter::Op::EQUALS;
    f3.value = "en";
    fvq.filters.push_back(f3);
    
    auto [status, results] = engine.executeFilteredVectorSearch(fvq);
    
    ASSERT_TRUE(status.ok) << status.message;
    
    // Verify all results match all three filters
    for (const auto& r : results) {
        EXPECT_EQ(r.entity["category"].get<std::string>(), "tech");
        EXPECT_LT(r.entity["score"].get<double>(), 0.7);
        EXPECT_EQ(r.entity["lang"].get<std::string>(), "en");
    }
}

// Test 9: Distance ordering verification
TEST_F(FilteredVectorSearchTest, DistanceOrdering_Ascending) {
    GraphIndexManager graphIdx(*db);
    QueryEngine engine(*db, *secIdx, graphIdx, vectorIdx.get(), nullptr);
    
    std::vector<float> query(128, 0.1f);
    
    FilteredVectorSearchQuery fvq;
    fvq.table = "documents";
    fvq.query_vector = query;
    fvq.k = 10;
    
    // Filter: category == "tech"
    FilteredVectorSearchQuery::AttributeFilter filter;
    filter.field = "category";
    filter.op = FilteredVectorSearchQuery::AttributeFilter::Op::EQUALS;
    filter.value = "tech";
    fvq.filters.push_back(filter);
    
    auto [status, results] = engine.executeFilteredVectorSearch(fvq);
    
    ASSERT_TRUE(status.ok) << status.message;
    ASSERT_GT(results.size(), 1);
    
    // Verify results are ordered by distance (ascending)
    for (size_t i = 1; i < results.size(); ++i) {
        EXPECT_LE(results[i-1].vector_distance, results[i].vector_distance);
    }
}

// Test 10: No filters (fallback to standard KNN)
TEST_F(FilteredVectorSearchTest, NoFilters_StandardKNN) {
    GraphIndexManager graphIdx(*db);
    QueryEngine engine(*db, *secIdx, graphIdx, vectorIdx.get(), nullptr);
    
    std::vector<float> query(128, 0.5f);
    
    FilteredVectorSearchQuery fvq;
    fvq.table = "documents";
    fvq.query_vector = query;
    fvq.k = 10;
    // No filters
    
    auto [status, results] = engine.executeFilteredVectorSearch(fvq);
    
    ASSERT_TRUE(status.ok) << status.message;
    EXPECT_EQ(results.size(), 10);
}

} // namespace themis
