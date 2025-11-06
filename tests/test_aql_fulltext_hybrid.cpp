// AQL FULLTEXT + AND/OR Combination Tests

#include <gtest/gtest.h>
#include "query/aql_parser.h"
#include "query/aql_translator.h"
#include "query/query_engine.h"
#include "index/secondary_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include "utils/logger.h"

using namespace themis;
using namespace themis::query;

class AQLFulltextHybridTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clean up test database
        std::filesystem::remove_all("data/themis_aql_ft_hybrid_test");
        
        RocksDBWrapper::Config cfg;
        cfg.db_path = "data/themis_aql_ft_hybrid_test";
        cfg.memtable_size_mb = 64;
        cfg.block_cache_size_mb = 128;
        
        db = std::make_unique<RocksDBWrapper>(cfg);
        ASSERT_TRUE(db->open());
        secIdx = std::make_unique<SecondaryIndexManager>(*db);
        engine = std::make_unique<QueryEngine>(*db, *secIdx);
        
        // Create fulltext index
        SecondaryIndexManager::FulltextConfig ftConfig;
        ftConfig.stemming_enabled = true;
        ftConfig.language = "en";
        ftConfig.stopwords_enabled = true;
        
        auto ftSt = secIdx->createFulltextIndex("articles", "content", ftConfig);
        ASSERT_TRUE(ftSt.ok) << ftSt.message;
        
        // Create equality index on year
        auto yearSt = secIdx->createIndex("articles", "year");
        ASSERT_TRUE(yearSt.ok) << yearSt.message;
        
        // Create range index on views
        auto viewsSt = secIdx->createRangeIndex("articles", "views");
        ASSERT_TRUE(viewsSt.ok) << viewsSt.message;
        
        // Create equality index on category
        auto catSt = secIdx->createIndex("articles", "category");
        ASSERT_TRUE(catSt.ok) << catSt.message;
        
        // Insert test data
        BaseEntity art1("a1");
        art1.setField("title", "Machine Learning Basics");
        art1.setField("content", "Introduction to machine learning algorithms and neural networks");
        art1.setField("year", "2023");
        art1.setField("views", "1000");
        art1.setField("category", "AI");
        secIdx->put("articles", art1);
        
        BaseEntity art2("a2");
        art2.setField("title", "Deep Learning Advanced");
        art2.setField("content", "Deep learning with convolutional neural networks");
        art2.setField("year", "2024");
        art2.setField("views", "5000");
        art2.setField("category", "AI");
        secIdx->put("articles", art2);
        
        BaseEntity art3("a3");
        art3.setField("title", "Database Systems");
        art3.setField("content", "Relational databases and SQL optimization");
        art3.setField("year", "2023");
        art3.setField("views", "800");
        art3.setField("category", "Database");
        secIdx->put("articles", art3);
        
        BaseEntity art4("a4");
        art4.setField("title", "Neural Network Architectures");
        art4.setField("content", "Modern neural network architectures for machine learning");
        art4.setField("year", "2024");
        art4.setField("views", "3000");
        art4.setField("category", "AI");
        secIdx->put("articles", art4);
        
        BaseEntity art5("a5");
        art5.setField("title", "Web Development");
        art5.setField("content", "Building modern web applications with JavaScript");
        art5.setField("year", "2023");
        art5.setField("views", "2000");
        art5.setField("category", "Web");
        secIdx->put("articles", art5);
    }
    
    void TearDown() override {
        engine.reset();
        secIdx.reset();
        db.reset();
        std::filesystem::remove_all("data/themis_aql_ft_hybrid_test");
    }
    
    std::unique_ptr<RocksDBWrapper> db;
    std::unique_ptr<SecondaryIndexManager> secIdx;
    std::unique_ptr<QueryEngine> engine;
};

// ============================================================================
// Parser Tests
// ============================================================================

TEST_F(AQLFulltextHybridTest, ParseFulltextAndEquality) {
    std::string aql = R"(
        FOR doc IN articles
        FILTER FULLTEXT(doc.content, "machine learning") AND doc.year == "2024"
        RETURN doc
    )";
    
    AQLParser parser;
    auto result = parser.parse(aql);
    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query, nullptr);
}

TEST_F(AQLFulltextHybridTest, ParseFulltextAndRange) {
    std::string aql = R"(
        FOR doc IN articles
        FILTER FULLTEXT(doc.content, "neural") AND doc.views >= 2000
        RETURN doc
    )";
    
    AQLParser parser;
    auto result = parser.parse(aql);
    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query, nullptr);
}

TEST_F(AQLFulltextHybridTest, ParseFulltextAndMultiple) {
    std::string aql = R"(
        FOR doc IN articles
        FILTER FULLTEXT(doc.content, "learning") AND doc.category == "AI" AND doc.year == "2024"
        RETURN doc
    )";
    
    AQLParser parser;
    auto result = parser.parse(aql);
    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query, nullptr);
}

// ============================================================================
// Translator Tests
// ============================================================================

TEST_F(AQLFulltextHybridTest, TranslateFulltextAndEquality) {
    std::string aql = R"(
        FOR doc IN articles
        FILTER FULLTEXT(doc.content, "machine learning") AND doc.year == "2024"
        RETURN doc
    )";
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success) << translateResult.error_message;
    
    // Should use ConjunctiveQuery with both fulltext and equality predicates
    EXPECT_TRUE(translateResult.query.fulltextPredicate.has_value());
    EXPECT_EQ(translateResult.query.fulltextPredicate->column, "content");
    EXPECT_EQ(translateResult.query.fulltextPredicate->query, "machine learning");
    
    EXPECT_EQ(translateResult.query.predicates.size(), 1);
    EXPECT_EQ(translateResult.query.predicates[0].column, "year");
    EXPECT_EQ(translateResult.query.predicates[0].value, "2024");
}

TEST_F(AQLFulltextHybridTest, TranslateFulltextAndRange) {
    std::string aql = R"(
        FOR doc IN articles
        FILTER FULLTEXT(doc.content, "neural networks") AND doc.views >= 1000
        RETURN doc
    )";
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success) << translateResult.error_message;
    
    EXPECT_TRUE(translateResult.query.fulltextPredicate.has_value());
    EXPECT_EQ(translateResult.query.rangePredicates.size(), 1);
    EXPECT_EQ(translateResult.query.rangePredicates[0].column, "views");
}

TEST_F(AQLFulltextHybridTest, TranslateFulltextAndMultiple) {
    std::string aql = R"(
        FOR doc IN articles
        FILTER FULLTEXT(doc.content, "learning") AND doc.category == "AI" AND doc.views >= 2000
        RETURN doc
    )";
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success) << translateResult.error_message;
    
    EXPECT_TRUE(translateResult.query.fulltextPredicate.has_value());
    EXPECT_EQ(translateResult.query.predicates.size(), 1);  // category
    EXPECT_EQ(translateResult.query.rangePredicates.size(), 1);  // views
}

// ============================================================================
// Execution Tests
// ============================================================================

TEST_F(AQLFulltextHybridTest, ExecuteFulltextAndEquality) {
    std::string aql = R"(
        FOR doc IN articles
        FILTER FULLTEXT(doc.content, "machine learning") AND doc.year == "2024"
        RETURN doc
    )";
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success);
    
    auto [status, keys] = engine->executeAndKeys(translateResult.query);
    ASSERT_TRUE(status.ok) << status.message;
    
    // Should find only a4 (machine learning + 2024)
    // a1 has machine learning but year=2023
    EXPECT_EQ(keys.size(), 1);
    if (keys.size() >= 1) {
        EXPECT_EQ(keys[0], "a4");
    }
}

TEST_F(AQLFulltextHybridTest, ExecuteFulltextAndRange) {
    std::string aql = R"(
        FOR doc IN articles
        FILTER FULLTEXT(doc.content, "neural") AND doc.views >= 3000
        RETURN doc
    )";
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success);
    
    auto [status, keys] = engine->executeAndKeys(translateResult.query);
    ASSERT_TRUE(status.ok) << status.message;
    
    // Should find a2 (neural + views=5000) and a4 (neural + views=3000)
    EXPECT_EQ(keys.size(), 2);
    
    std::set<std::string> keySet(keys.begin(), keys.end());
    EXPECT_TRUE(keySet.count("a2"));
    EXPECT_TRUE(keySet.count("a4"));
}

TEST_F(AQLFulltextHybridTest, ExecuteFulltextAndCategory) {
    std::string aql = R"(
        FOR doc IN articles
        FILTER FULLTEXT(doc.content, "learning") AND doc.category == "AI"
        RETURN doc
    )";
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success);
    
    auto [status, keys] = engine->executeAndKeys(translateResult.query);
    ASSERT_TRUE(status.ok) << status.message;
    
    // Should find a1, a2, a4 (all have "learning" and category=AI)
    EXPECT_EQ(keys.size(), 3);
    
    std::set<std::string> keySet(keys.begin(), keys.end());
    EXPECT_TRUE(keySet.count("a1"));
    EXPECT_TRUE(keySet.count("a2"));
    EXPECT_TRUE(keySet.count("a4"));
}

TEST_F(AQLFulltextHybridTest, ExecuteFulltextAndMultiplePredicates) {
    std::string aql = R"(
        FOR doc IN articles
        FILTER FULLTEXT(doc.content, "neural") AND doc.category == "AI" AND doc.year == "2024"
        RETURN doc
    )";
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success);
    
    auto [status, keys] = engine->executeAndKeys(translateResult.query);
    ASSERT_TRUE(status.ok) << status.message;
    
    // Should find only a2 and a4 (neural + AI + 2024)
    EXPECT_EQ(keys.size(), 2);
    
    std::set<std::string> keySet(keys.begin(), keys.end());
    EXPECT_TRUE(keySet.count("a2"));
    EXPECT_TRUE(keySet.count("a4"));
}

TEST_F(AQLFulltextHybridTest, ExecuteFulltextAndNoIntersection) {
    std::string aql = R"(
        FOR doc IN articles
        FILTER FULLTEXT(doc.content, "machine learning") AND doc.category == "Database"
        RETURN doc
    )";
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success);
    
    auto [status, keys] = engine->executeAndKeys(translateResult.query);
    ASSERT_TRUE(status.ok) << status.message;
    
    // No articles match both criteria
    EXPECT_EQ(keys.size(), 0);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(AQLFulltextHybridTest, FulltextOrStillNotSupported) {
    std::string aql = R"(
        FOR doc IN articles
        FILTER FULLTEXT(doc.content, "AI") OR doc.year == "2024"
        RETURN doc
    )";
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    // Should fail - FULLTEXT in OR not supported yet
    EXPECT_FALSE(translateResult.success);
    EXPECT_NE(translateResult.error_message.find("FULLTEXT"), std::string::npos);
}

TEST_F(AQLFulltextHybridTest, ReverseOrderFulltextAnd) {
    // Test with equality before FULLTEXT
    std::string aql = R"(
        FOR doc IN articles
        FILTER doc.category == "AI" AND FULLTEXT(doc.content, "learning")
        RETURN doc
    )";
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success) << translateResult.error_message;
    
    auto [status, keys] = engine->executeAndKeys(translateResult.query);
    ASSERT_TRUE(status.ok) << status.message;
    
    // Should work the same as FULLTEXT first
    EXPECT_EQ(keys.size(), 3);
}
