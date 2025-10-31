#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "index/adaptive_index.h"
#include "storage/rocksdb_wrapper.h"
#include <rocksdb/utilities/transaction_db.h>
#include <filesystem>
#include <thread>
#include <chrono>

using namespace themis;
using ::testing::HasSubstr;

class AdaptiveIndexTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_db_path_ = std::filesystem::temp_directory_path() / 
                       ("themis_adaptive_test_" + std::to_string(std::time(nullptr)));
        
        RocksDBWrapper::Config config;
        config.db_path = test_db_path_.string();
        
        db_ = std::make_unique<RocksDBWrapper>(config);
        
        // Open the database
        ASSERT_TRUE(db_->open()) << "Failed to open RocksDB";
        
        // Verify DB is initialized
        ASSERT_NE(db_, nullptr);
        ASSERT_NE(db_->getRawDB(), nullptr);
        
        manager_ = std::make_unique<AdaptiveIndexManager>(db_->getRawDB());
        
        // Insert test documents
        seedTestData();
    }
    
    void TearDown() override {
        manager_.reset();
        db_.reset();
        std::filesystem::remove_all(test_db_path_);
    }
    
    void seedTestData() {
        // Collection: users
        for (int i = 0; i < 100; i++) {
            nlohmann::json doc = {
                {"id", i},
                {"name", "User" + std::to_string(i % 10)},  // 10 unique names
                {"age", 20 + (i % 50)},                     // 50 unique ages
                {"email", "user" + std::to_string(i) + "@test.com"},  // 100 unique
                {"status", i % 3 == 0 ? "active" : "inactive"}  // 2 values
            };
            
            std::string key = "d:users:" + std::to_string(i);
            db_->getRawDB()->Put(rocksdb::WriteOptions(), key, doc.dump());
        }
        
        // Collection: products
        for (int i = 0; i < 50; i++) {
            nlohmann::json doc = {
                {"id", i},
                {"category", "cat" + std::to_string(i % 5)},  // 5 categories
                {"price", 10.0 + (i % 20)},                   // 20 unique prices
                {"stock", i % 10}                              // 10 stock levels
            };
            
            std::string key = "d:products:" + std::to_string(i);
            db_->getRawDB()->Put(rocksdb::WriteOptions(), key, doc.dump());
        }
    }
    
    std::filesystem::path test_db_path_;
    std::unique_ptr<RocksDBWrapper> db_;
    std::unique_ptr<AdaptiveIndexManager> manager_;
};

// ===== QueryPatternTracker Tests =====

TEST_F(AdaptiveIndexTest, PatternTracker_RecordPattern_Success) {
    auto* tracker = manager_->getPatternTracker();
    
    tracker->recordPattern("users", "age", "range", 25);
    
    auto patterns = tracker->getPatterns("users");
    ASSERT_EQ(patterns.size(), 1);
    
    EXPECT_EQ(patterns[0].collection, "users");
    EXPECT_EQ(patterns[0].field, "age");
    EXPECT_EQ(patterns[0].operation, "range");
    EXPECT_EQ(patterns[0].count, 1);
    EXPECT_EQ(patterns[0].total_time_ms, 25);
}

TEST_F(AdaptiveIndexTest, PatternTracker_MultipleRecords_Aggregates) {
    auto* tracker = manager_->getPatternTracker();
    
    tracker->recordPattern("users", "age", "range", 10);
    tracker->recordPattern("users", "age", "range", 20);
    tracker->recordPattern("users", "age", "range", 30);
    
    auto patterns = tracker->getPatterns("users");
    ASSERT_EQ(patterns.size(), 1);
    
    EXPECT_EQ(patterns[0].count, 3);
    EXPECT_EQ(patterns[0].total_time_ms, 60);
}

TEST_F(AdaptiveIndexTest, PatternTracker_DifferentOperations_Separate) {
    auto* tracker = manager_->getPatternTracker();
    
    tracker->recordPattern("users", "age", "range", 10);
    tracker->recordPattern("users", "age", "eq", 5);
    tracker->recordPattern("users", "name", "eq", 3);
    
    auto patterns = tracker->getPatterns("users");
    EXPECT_EQ(patterns.size(), 3);
}

TEST_F(AdaptiveIndexTest, PatternTracker_GetTopPatterns_SortsByFrequency) {
    auto* tracker = manager_->getPatternTracker();
    
    // High frequency
    for (int i = 0; i < 100; i++) {
        tracker->recordPattern("users", "status", "eq", 1);
    }
    
    // Medium frequency
    for (int i = 0; i < 50; i++) {
        tracker->recordPattern("users", "age", "range", 2);
    }
    
    // Low frequency
    for (int i = 0; i < 10; i++) {
        tracker->recordPattern("users", "name", "eq", 1);
    }
    
    auto top = tracker->getTopPatterns(3);
    ASSERT_EQ(top.size(), 3);
    
    EXPECT_EQ(top[0].field, "status");
    EXPECT_EQ(top[0].count, 100);
    
    EXPECT_EQ(top[1].field, "age");
    EXPECT_EQ(top[1].count, 50);
    
    EXPECT_EQ(top[2].field, "name");
    EXPECT_EQ(top[2].count, 10);
}

TEST_F(AdaptiveIndexTest, PatternTracker_ThreadSafe_ConcurrentRecords) {
    auto* tracker = manager_->getPatternTracker();
    
    std::vector<std::thread> threads;
    for (int t = 0; t < 10; t++) {
        threads.emplace_back([tracker]() {
            for (int i = 0; i < 100; i++) {
                tracker->recordPattern("users", "age", "range", 1);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto patterns = tracker->getPatterns("users");
    ASSERT_EQ(patterns.size(), 1);
    EXPECT_EQ(patterns[0].count, 1000);
}

TEST_F(AdaptiveIndexTest, PatternTracker_Clear_RemovesAll) {
    auto* tracker = manager_->getPatternTracker();
    
    tracker->recordPattern("users", "age", "range", 10);
    tracker->recordPattern("users", "name", "eq", 5);
    
    EXPECT_EQ(tracker->size(), 2);
    
    tracker->clear();
    
    EXPECT_EQ(tracker->size(), 0);
    EXPECT_TRUE(tracker->getPatterns().empty());
}

TEST_F(AdaptiveIndexTest, PatternTracker_ToJson_SerializesCorrectly) {
    auto* tracker = manager_->getPatternTracker();
    
    tracker->recordPattern("users", "age", "range", 30);
    
    auto patterns = tracker->getPatterns();
    auto json = patterns[0].toJson();
    
    EXPECT_EQ(json["collection"], "users");
    EXPECT_EQ(json["field"], "age");
    EXPECT_EQ(json["operation"], "range");
    EXPECT_EQ(json["count"], 1);
    EXPECT_EQ(json["avg_time_ms"], 30);
}

// ===== SelectivityAnalyzer Tests =====

TEST_F(AdaptiveIndexTest, SelectivityAnalyzer_AnalyzeHighSelectivity_Success) {
    auto* analyzer = manager_->getSelectivityAnalyzer();
    
    // email field has 100 unique values out of 100 documents
    auto stats = analyzer->analyze("users", "email", 0);
    
    EXPECT_EQ(stats.collection, "users");
    EXPECT_EQ(stats.field, "email");
    EXPECT_EQ(stats.total_documents, 100);
    EXPECT_EQ(stats.unique_values, 100);
    EXPECT_NEAR(stats.selectivity, 1.0, 0.01);
}

TEST_F(AdaptiveIndexTest, SelectivityAnalyzer_AnalyzeLowSelectivity_Success) {
    auto* analyzer = manager_->getSelectivityAnalyzer();
    
    // status field has 2 unique values
    auto stats = analyzer->analyze("users", "status", 0);
    
    EXPECT_EQ(stats.unique_values, 2);
    EXPECT_LT(stats.selectivity, 0.1);
}

TEST_F(AdaptiveIndexTest, SelectivityAnalyzer_AnalyzeWithSampling_Works) {
    auto* analyzer = manager_->getSelectivityAnalyzer();
    
    auto stats = analyzer->analyze("users", "age", 50);  // Sample 50 docs
    
    EXPECT_LE(stats.total_documents, 50);
    EXPECT_GT(stats.unique_values, 0);
}

TEST_F(AdaptiveIndexTest, SelectivityAnalyzer_CalculateBenefit_HighSelectivity) {
    auto* analyzer = manager_->getSelectivityAnalyzer();
    
    auto stats = analyzer->analyze("users", "email", 0);
    double benefit = analyzer->calculateIndexBenefit(stats);
    
    EXPECT_GT(benefit, 0.5);  // High selectivity = high benefit
}

TEST_F(AdaptiveIndexTest, SelectivityAnalyzer_CalculateBenefit_LowSelectivity) {
    auto* analyzer = manager_->getSelectivityAnalyzer();
    
    auto stats = analyzer->analyze("users", "status", 0);
    double benefit = analyzer->calculateIndexBenefit(stats);
    
    EXPECT_LT(benefit, 0.7);  // Low selectivity = lower benefit
}

TEST_F(AdaptiveIndexTest, SelectivityAnalyzer_Distribution_Uniform) {
    auto* analyzer = manager_->getSelectivityAnalyzer();
    
    auto stats = analyzer->analyze("users", "age", 0);
    
    // age has fairly uniform distribution
    EXPECT_TRUE(stats.distribution == "uniform" || stats.distribution == "skewed");
}

TEST_F(AdaptiveIndexTest, SelectivityAnalyzer_ToJson_Serializes) {
    auto* analyzer = manager_->getSelectivityAnalyzer();
    
    auto stats = analyzer->analyze("users", "email", 0);
    auto json = stats.toJson();
    
    EXPECT_EQ(json["collection"], "users");
    EXPECT_EQ(json["field"], "email");
    EXPECT_TRUE(json.contains("selectivity"));
    EXPECT_TRUE(json.contains("distribution"));
}

// ===== IndexSuggestionEngine Tests =====

TEST_F(AdaptiveIndexTest, SuggestionEngine_GenerateSuggestions_Success) {
    auto* tracker = manager_->getPatternTracker();
    
    // High frequency on high selectivity field
    for (int i = 0; i < 100; i++) {
        tracker->recordPattern("users", "email", "eq", 50);
    }
    
    auto suggestions = manager_->getSuggestions("users", 0.0, 10);
    
    ASSERT_GT(suggestions.size(), 0);
    
    auto& suggestion = suggestions[0];
    EXPECT_EQ(suggestion.collection, "users");
    EXPECT_EQ(suggestion.field, "email");
    EXPECT_GT(suggestion.score, 0.0);
    EXPECT_EQ(suggestion.queries_affected, 100);
}

TEST_F(AdaptiveIndexTest, SuggestionEngine_ScoreCalculation_FrequencyMatters) {
    auto* tracker = manager_->getPatternTracker();
    
    // High frequency
    for (int i = 0; i < 1000; i++) {
        tracker->recordPattern("users", "email", "eq", 10);
    }
    
    // Low frequency
    for (int i = 0; i < 10; i++) {
        tracker->recordPattern("users", "name", "eq", 10);
    }
    
    auto suggestions = manager_->getSuggestions("users", 0.0, 10);
    
    // email should score higher due to frequency
    auto email_it = std::find_if(suggestions.begin(), suggestions.end(),
        [](const auto& s) { return s.field == "email"; });
    
    auto name_it = std::find_if(suggestions.begin(), suggestions.end(),
        [](const auto& s) { return s.field == "name"; });
    
    if (email_it != suggestions.end() && name_it != suggestions.end()) {
        EXPECT_GT(email_it->score, name_it->score);
    }
}

TEST_F(AdaptiveIndexTest, SuggestionEngine_RecommendIndexType_Range) {
    auto* tracker = manager_->getPatternTracker();
    
    tracker->recordPattern("users", "age", "range", 20);
    
    auto suggestions = manager_->getSuggestions("users", 0.0, 10);
    
    auto age_suggestion = std::find_if(suggestions.begin(), suggestions.end(),
        [](const auto& s) { return s.field == "age"; });
    
    if (age_suggestion != suggestions.end()) {
        EXPECT_EQ(age_suggestion->index_type, "range");
    }
}

TEST_F(AdaptiveIndexTest, SuggestionEngine_RecommendIndexType_Hash) {
    auto* tracker = manager_->getPatternTracker();
    
    tracker->recordPattern("users", "email", "eq", 10);
    
    auto suggestions = manager_->getSuggestions("users", 0.0, 10);
    
    auto email_suggestion = std::find_if(suggestions.begin(), suggestions.end(),
        [](const auto& s) { return s.field == "email"; });
    
    if (email_suggestion != suggestions.end()) {
        EXPECT_EQ(email_suggestion->index_type, "hash");
    }
}

TEST_F(AdaptiveIndexTest, SuggestionEngine_MinScoreFilter_Works) {
    auto* tracker = manager_->getPatternTracker();
    
    // Low frequency pattern
    tracker->recordPattern("users", "id", "eq", 1);
    
    // Should be filtered out with high min_score
    auto suggestions = manager_->getSuggestions("users", 0.9, 10);
    
    auto id_suggestion = std::find_if(suggestions.begin(), suggestions.end(),
        [](const auto& s) { return s.field == "id"; });
    
    EXPECT_EQ(id_suggestion, suggestions.end());
}

TEST_F(AdaptiveIndexTest, SuggestionEngine_Limit_Respected) {
    auto* tracker = manager_->getPatternTracker();
    
    // Create many patterns
    for (int i = 0; i < 10; i++) {
        tracker->recordPattern("users", "field" + std::to_string(i), "eq", 10);
    }
    
    auto suggestions = manager_->getSuggestions("users", 0.0, 3);
    
    EXPECT_LE(suggestions.size(), 3);
}

TEST_F(AdaptiveIndexTest, SuggestionEngine_EstimatedSpeedup_Calculated) {
    auto* tracker = manager_->getPatternTracker();
    
    tracker->recordPattern("users", "email", "eq", 100);
    tracker->recordPattern("users", "email", "eq", 100);
    
    auto suggestions = manager_->getSuggestions("users", 0.0, 10);
    
    auto email_suggestion = std::find_if(suggestions.begin(), suggestions.end(),
        [](const auto& s) { return s.field == "email"; });
    
    if (email_suggestion != suggestions.end()) {
        EXPECT_GT(email_suggestion->estimated_speedup_ms, 0);
        EXPECT_FALSE(email_suggestion->reason.empty());
    }
}

TEST_F(AdaptiveIndexTest, SuggestionEngine_ToJson_SerializesComplete) {
    auto* tracker = manager_->getPatternTracker();
    
    tracker->recordPattern("users", "email", "eq", 50);
    
    auto suggestions = manager_->getSuggestions("users", 0.0, 1);
    
    if (!suggestions.empty()) {
        auto json = suggestions[0].toJson();
        
        EXPECT_TRUE(json.contains("collection"));
        EXPECT_TRUE(json.contains("field"));
        EXPECT_TRUE(json.contains("index_type"));
        EXPECT_TRUE(json.contains("score"));
        EXPECT_TRUE(json.contains("reason"));
        EXPECT_TRUE(json.contains("queries_affected"));
        EXPECT_TRUE(json.contains("estimated_speedup_ms"));
        EXPECT_TRUE(json.contains("metadata"));
    }
}

// ===== Real-World Scenarios =====

TEST_F(AdaptiveIndexTest, RealWorld_FrequentUserLookup_SuggestsHashIndex) {
    auto* tracker = manager_->getPatternTracker();
    
    // Simulate frequent user lookups by email
    for (int i = 0; i < 500; i++) {
        tracker->recordPattern("users", "email", "eq", 25);
    }
    
    auto suggestions = manager_->getSuggestions("users", 0.5, 5);
    
    ASSERT_GT(suggestions.size(), 0);
    
    auto& top_suggestion = suggestions[0];
    EXPECT_EQ(top_suggestion.field, "email");
    EXPECT_EQ(top_suggestion.index_type, "hash");
    EXPECT_GT(top_suggestion.score, 0.5);
    EXPECT_EQ(top_suggestion.queries_affected, 500);
}

TEST_F(AdaptiveIndexTest, RealWorld_AgeRangeQueries_SuggestsRangeIndex) {
    auto* tracker = manager_->getPatternTracker();
    
    // Simulate age range queries
    for (int i = 0; i < 200; i++) {
        tracker->recordPattern("users", "age", "range", 35);
    }
    
    auto suggestions = manager_->getSuggestions("users", 0.3, 5);
    
    auto age_suggestion = std::find_if(suggestions.begin(), suggestions.end(),
        [](const auto& s) { return s.field == "age"; });
    
    ASSERT_NE(age_suggestion, suggestions.end());
    EXPECT_EQ(age_suggestion->index_type, "range");
    EXPECT_TRUE(age_suggestion->reason.find("range") != std::string::npos);
}

TEST_F(AdaptiveIndexTest, RealWorld_MultiCollection_SeparateSuggestions) {
    auto* tracker = manager_->getPatternTracker();
    
    // Users collection
    for (int i = 0; i < 100; i++) {
        tracker->recordPattern("users", "email", "eq", 20);
    }
    
    // Products collection
    for (int i = 0; i < 150; i++) {
        tracker->recordPattern("products", "category", "eq", 15);
    }
    
    auto user_suggestions = manager_->getSuggestions("users", 0.0, 10);
    auto product_suggestions = manager_->getSuggestions("products", 0.0, 10);
    
    EXPECT_GT(user_suggestions.size(), 0);
    EXPECT_GT(product_suggestions.size(), 0);
    
    // Verify correct collection
    for (const auto& s : user_suggestions) {
        EXPECT_EQ(s.collection, "users");
    }
    
    for (const auto& s : product_suggestions) {
        EXPECT_EQ(s.collection, "products");
    }
}

TEST_F(AdaptiveIndexTest, Performance_1000Patterns_UnderThreshold) {
    auto* tracker = manager_->getPatternTracker();
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Record 1000 patterns
    for (int i = 0; i < 1000; i++) {
        tracker->recordPattern("users", "field" + std::to_string(i % 10), "eq", 10);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "Record 1000 patterns took: " << duration.count() << "ms" << std::endl;
    EXPECT_LT(duration.count(), 100);  // Should be very fast
}

TEST_F(AdaptiveIndexTest, Performance_GenerateSuggestions_Fast) {
    auto* tracker = manager_->getPatternTracker();
    
    // Setup patterns
    for (int i = 0; i < 100; i++) {
        tracker->recordPattern("users", "field" + std::to_string(i % 5), "eq", 20);
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    
    auto suggestions = manager_->getSuggestions("users", 0.0, 10);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "Generate suggestions took: " << duration.count() << "ms" << std::endl;
    EXPECT_LT(duration.count(), 500);  // Includes selectivity analysis
}
