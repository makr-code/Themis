// AQL Fulltext Search Tests

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

class AQLFulltextTest : public ::testing::Test {
protected:
void SetUp() override {
    // Clean up test database
    std::filesystem::remove_all("data/themis_aql_fulltext_test");
    
    RocksDBWrapper::Config cfg;
    cfg.db_path = "data/themis_aql_fulltext_test";
    cfg.memtable_size_mb = 64;
    cfg.block_cache_size_mb = 128;
    
    db = std::make_unique<RocksDBWrapper>(cfg);
    ASSERT_TRUE(db->open());
    secIdx = std::make_unique<SecondaryIndexManager>(*db);
    engine = std::make_unique<QueryEngine>(*db, *secIdx);        // Create fulltext index on articles.content
        SecondaryIndexManager::FulltextConfig config;
        config.stemming_enabled = true;
        config.language = "en";
        config.stopwords_enabled = true;
        
        auto st = secIdx->createFulltextIndex("articles", "content", config);
        ASSERT_TRUE(st.ok) << st.message;
        
        // Insert test documents
        BaseEntity doc1("art1");
        doc1.setField("content", "Machine learning and deep neural networks");
        doc1.setField("title", "ML Intro");
        secIdx->put("articles", doc1);
        
        BaseEntity doc2("art2");
        doc2.setField("content", "Deep learning for computer vision");
        doc2.setField("title", "Vision AI");
        secIdx->put("articles", doc2);
        
        BaseEntity doc3("art3");
        doc3.setField("content", "Neural network optimization techniques");
        doc3.setField("title", "Optimization");
        secIdx->put("articles", doc3);
        
        BaseEntity doc4("art4");
        doc4.setField("content", "The quick brown fox jumps over the lazy dog");
        doc4.setField("title", "Classic");
        secIdx->put("articles", doc4);
    }
    
    void TearDown() override {
        engine.reset();
        secIdx.reset();
        db.reset();
        std::filesystem::remove_all("data/themis_aql_fulltext_test");
    }
    
    std::unique_ptr<RocksDBWrapper> db;
    std::unique_ptr<SecondaryIndexManager> secIdx;
    std::unique_ptr<QueryEngine> engine;
};

TEST_F(AQLFulltextTest, ParseFulltextFunction) {
    std::string aql = R"(
        FOR doc IN articles
        FILTER FULLTEXT(doc.content, "machine learning")
        RETURN doc
    )";
    
    AQLParser parser;
    auto result = parser.parse(aql);
    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query, nullptr);
    EXPECT_EQ(result.query->for_node.collection, "articles");
    ASSERT_EQ(result.query->filters.size(), 1);
    
    auto filter = result.query->filters[0];
    ASSERT_EQ(filter->condition->getType(), ASTNodeType::FunctionCall);
    
    auto funcCall = std::static_pointer_cast<FunctionCallExpr>(filter->condition);
    EXPECT_EQ(funcCall->name, "FULLTEXT");
    EXPECT_EQ(funcCall->arguments.size(), 2);
}

TEST_F(AQLFulltextTest, ParseFulltextWithLimit) {
    std::string aql = R"(
        FOR doc IN articles
        FILTER FULLTEXT(doc.content, "neural", 10)
        RETURN doc
    )";
    
    AQLParser parser;
    auto result = parser.parse(aql);
    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query, nullptr);
    
    auto filter = result.query->filters[0];
    auto funcCall = std::static_pointer_cast<FunctionCallExpr>(filter->condition);
    EXPECT_EQ(funcCall->arguments.size(), 3);
}

TEST_F(AQLFulltextTest, TranslateFulltextToQuery) {
    std::string aql = R"(
        FOR doc IN articles
        FILTER FULLTEXT(doc.content, "deep learning")
        RETURN doc
    )";
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success) << parseResult.error.toString();
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success) << translateResult.error_message;
    ASSERT_TRUE(translateResult.query.fulltextPredicate.has_value());
    
    const auto& ft = *translateResult.query.fulltextPredicate;
    EXPECT_EQ(ft.column, "content");
    EXPECT_EQ(ft.query, "deep learning");
    EXPECT_EQ(ft.limit, 1000); // default
}

TEST_F(AQLFulltextTest, TranslateFulltextWithCustomLimit) {
    std::string aql = R"(
        FOR doc IN articles
        FILTER FULLTEXT(doc.content, "neural network", 5)
        RETURN doc
    )";
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success) << parseResult.error.toString();
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success) << translateResult.error_message;
    ASSERT_TRUE(translateResult.query.fulltextPredicate.has_value());
    
    const auto& ft = *translateResult.query.fulltextPredicate;
    EXPECT_EQ(ft.limit, 5);
}

TEST_F(AQLFulltextTest, ExecuteFulltextQuery) {
    std::string aql = R"(
        FOR doc IN articles
        FILTER FULLTEXT(doc.content, "deep learning")
        RETURN doc
    )";
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success);
    
    auto [status, keys] = engine->executeAndKeys(translateResult.query);
    ASSERT_TRUE(status.ok) << status.message;
    
    // Should find doc2 (exact match) and possibly doc1 (partial)
    EXPECT_GE(keys.size(), 1);
    EXPECT_LE(keys.size(), 2);
    
    // doc2 should be first (higher BM25 score for exact match)
    if (keys.size() >= 1) {
        EXPECT_EQ(keys[0], "art2");
    }
}

TEST_F(AQLFulltextTest, ExecuteFulltextWithPhraseQuery) {
    std::string aql = R"(
        FOR doc IN articles
        FILTER FULLTEXT(doc.content, '"deep learning"')
        RETURN doc
    )";
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success);
    
    auto [status, keys] = engine->executeAndKeys(translateResult.query);
    ASSERT_TRUE(status.ok) << status.message;
    
    // Should find only doc2 with exact phrase "deep learning"
    EXPECT_EQ(keys.size(), 1);
    if (keys.size() >= 1) {
        EXPECT_EQ(keys[0], "art2");
    }
}

TEST_F(AQLFulltextTest, ExecuteFulltextNoResults) {
    std::string aql = R"(
        FOR doc IN articles
        FILTER FULLTEXT(doc.content, "quantum computing")
        RETURN doc
    )";
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success);
    
    auto [status, keys] = engine->executeAndKeys(translateResult.query);
    ASSERT_TRUE(status.ok);
    EXPECT_EQ(keys.size(), 0);
}

TEST_F(AQLFulltextTest, ExecuteFulltextMultipleTerms) {
    std::string aql = R"(
        FOR doc IN articles
        FILTER FULLTEXT(doc.content, "neural network optimization")
        RETURN doc
    )";
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success);
    
    auto [status, keys] = engine->executeAndKeys(translateResult.query);
    ASSERT_TRUE(status.ok) << status.message;
    
    // Should find doc3 (all three terms)
    EXPECT_GE(keys.size(), 1);
    if (keys.size() >= 1) {
        EXPECT_EQ(keys[0], "art3");
    }
}

TEST_F(AQLFulltextTest, InvalidFulltextMissingArguments) {
    std::string aql = R"(
        FOR doc IN articles
        FILTER FULLTEXT(doc.content)
        RETURN doc
    )";
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success); // Parser allows it
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    EXPECT_FALSE(translateResult.success);
    EXPECT_NE(translateResult.error_message.find("2-3 arguments"), std::string::npos);
}

TEST_F(AQLFulltextTest, InvalidFulltextNonLiteralQuery) {
    std::string aql = R"(
        FOR doc IN articles
        FILTER FULLTEXT(doc.content, doc.title)
        RETURN doc
    )";
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success); // Parser allows it
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    EXPECT_FALSE(translateResult.success);
    EXPECT_NE(translateResult.error_message.find("string literal"), std::string::npos);
}
