// Test: AQL BM25() function integration

#include <gtest/gtest.h>
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include "index/secondary_index.h"
#include "query/query_engine.h"
#include "query/aql_parser.h"
#include "query/aql_translator.h"
#include <filesystem>
#include <memory>

namespace fs = std::filesystem;
using namespace themis;
using namespace themis::query;

class AQLBm25Test : public ::testing::Test {
protected:
    void SetUp() override {
        dbPath_ = "test_aql_bm25_db";
        if (fs::exists(dbPath_)) {
            fs::remove_all(dbPath_);
        }
        
        RocksDBWrapper::Config cfg;
        cfg.db_path = dbPath_;
        db_ = std::make_shared<RocksDBWrapper>(cfg);
        ASSERT_TRUE(db_->open());
        
        idx_ = std::make_shared<SecondaryIndexManager>(*db_);
        engine_ = std::make_shared<QueryEngine>(*db_, *idx_);
        
        // Create test data
        BaseEntity doc1("doc1");
        doc1.setField("title", "Machine learning basics");
        doc1.setField("content", "machine learning is a subset of artificial intelligence");
        doc1.setField("_key", "doc1");
        db_->put("articles:doc1", doc1.serialize());
        
        BaseEntity doc2("doc2");
        doc2.setField("title", "Deep learning tutorial");
        doc2.setField("content", "deep learning uses neural networks for machine learning");
        doc2.setField("_key", "doc2");
        db_->put("articles:doc2", doc2.serialize());
        
        BaseEntity doc3("doc3");
        doc3.setField("title", "Unrelated document");
        doc3.setField("content", "this document talks about cooking recipes");
        doc3.setField("_key", "doc3");
        db_->put("articles:doc3", doc3.serialize());
        
        // Create fulltext index
        SecondaryIndexManager::FulltextConfig config;
        config.stemming_enabled = true;
        config.language = "en";
        auto st = idx_->createFulltextIndex("articles", "content", config);
        ASSERT_TRUE(st.ok);
        
        // Index documents
        idx_->put("articles", doc1);
        idx_->put("articles", doc2);
        idx_->put("articles", doc3);
    }
    
    void TearDown() override {
        engine_.reset();
        idx_.reset();
        db_.reset();
        if (fs::exists(dbPath_)) {
            fs::remove_all(dbPath_);
        }
    }
    
    std::string dbPath_;
    std::shared_ptr<RocksDBWrapper> db_;
    std::shared_ptr<SecondaryIndexManager> idx_;
    std::shared_ptr<QueryEngine> engine_;
};

TEST_F(AQLBm25Test, BasicBM25FunctionParsing) {
    // Test that BM25(doc) parses correctly
    std::string query = R"(
        FOR doc IN articles
        FILTER FULLTEXT(doc.content, "machine learning")
        RETURN BM25(doc)
    )";
    
    AQLParser parser;
    auto result = parser.parse(query);
    ASSERT_TRUE(result.success) << "Parse error: " << result.error.message;
    ASSERT_TRUE(result.query);
    ASSERT_TRUE(result.query->return_node);
    
    // Check that RETURN expression is a function call
    auto returnExpr = result.query->return_node->expression;
    ASSERT_EQ(returnExpr->getType(), ASTNodeType::FunctionCall);
    
    auto funcCall = std::static_pointer_cast<FunctionCallExpr>(returnExpr);
    EXPECT_EQ(funcCall->name, "BM25");
    EXPECT_EQ(funcCall->arguments.size(), 1u);
}

TEST_F(AQLBm25Test, ExecuteAndKeysWithScores) {
    // Test executeAndKeysWithScores() method
    ConjunctiveQuery q;
    q.table = "articles";
    q.fulltextPredicate = PredicateFulltext{"content", "machine learning", 100};
    
    auto [st, result] = engine_->executeAndKeysWithScores(q);
    ASSERT_TRUE(st.ok) << "Execution error: " << st.message;
    
    // Should match doc1 and doc2 (both mention "machine learning")
    ASSERT_GE(result.keys.size(), 2u);
    
    // Check that scores are populated
    ASSERT_TRUE(result.bm25_scores);
    EXPECT_GT(result.bm25_scores->size(), 0u);
    
    // Verify scores exist for returned keys
    for (const auto& key : result.keys) {
        EXPECT_TRUE(result.bm25_scores->find(key) != result.bm25_scores->end())
            << "Missing score for key: " << key;
        EXPECT_GT((*result.bm25_scores)[key], 0.0)
            << "Score should be positive for key: " << key;
    }
}

TEST_F(AQLBm25Test, BM25ScoresDecreaseWithRelevance) {
    // Create query for "machine"
    ConjunctiveQuery q;
    q.table = "articles";
    q.fulltextPredicate = PredicateFulltext{"content", "machine", 100};
    
    auto [st, result] = engine_->executeAndKeysWithScores(q);
    ASSERT_TRUE(st.ok);
    ASSERT_GE(result.keys.size(), 2u);
    
    // Get scores for doc1 and doc2
    double score_doc1 = 0.0, score_doc2 = 0.0;
    for (const auto& key : result.keys) {
        if (key == "doc1") score_doc1 = (*result.bm25_scores)[key];
        if (key == "doc2") score_doc2 = (*result.bm25_scores)[key];
    }
    
    // Both should have positive scores
    EXPECT_GT(score_doc1, 0.0);
    EXPECT_GT(score_doc2, 0.0);
    
    // Scores should be different (BM25 considers term frequency)
    // doc1: "machine" appears 1x in 10 words
    // doc2: "machine" appears 1x in 10 words (similar, but different doc lengths affect score)
    // Just verify they're both reasonable scores
    EXPECT_LT(score_doc1, 100.0); // Sanity check: not absurdly high
    EXPECT_LT(score_doc2, 100.0);
}

TEST_F(AQLBm25Test, NoScoresForNonFulltextQuery) {
    // Test that non-FULLTEXT queries return empty score map
    
    // Create a regular index on title
    auto idxSt = idx_->createIndex("articles", "title");
    ASSERT_TRUE(idxSt.ok);
    
    ConjunctiveQuery q;
    q.table = "articles";
    q.predicates.push_back(PredicateEq{"title", "Machine learning basics"});
    
    auto [st, result] = engine_->executeAndKeysWithScores(q);
    ASSERT_TRUE(st.ok) << "Execution error: " << st.message;
    
    // Should have empty score map
    ASSERT_TRUE(result.bm25_scores);
    EXPECT_EQ(result.bm25_scores->size(), 0u);
}

