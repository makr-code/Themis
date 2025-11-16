// AQL OR Operator Tests

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

class AQLOrTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clean up test database
        std::filesystem::remove_all("data/themis_aql_or_test");
        
        RocksDBWrapper::Config cfg;
        cfg.db_path = "data/themis_aql_or_test";
        cfg.memtable_size_mb = 64;
        cfg.block_cache_size_mb = 128;
        
        db = std::make_unique<RocksDBWrapper>(cfg);
        ASSERT_TRUE(db->open());
        secIdx = std::make_unique<SecondaryIndexManager>(*db);
        engine = std::make_unique<QueryEngine>(*db, *secIdx);
        
        // Create indexes
        auto st1 = secIdx->createIndex("users", "status");
        ASSERT_TRUE(st1.ok) << st1.message;
        
        auto st2 = secIdx->createRangeIndex("users", "age");
        ASSERT_TRUE(st2.ok) << st2.message;
        
        auto st3 = secIdx->createIndex("users", "city");
        ASSERT_TRUE(st3.ok) << st3.message;
        
        // Insert test data
        BaseEntity user1("u1");
        user1.setField("name", "Alice");
        user1.setField("age", "25");
        user1.setField("status", "active");
        user1.setField("city", "Berlin");
        secIdx->put("users", user1);
        
        BaseEntity user2("u2");
        user2.setField("name", "Bob");
        user2.setField("age", "30");
        user2.setField("status", "inactive");
        user2.setField("city", "Munich");
        secIdx->put("users", user2);
        
        BaseEntity user3("u3");
        user3.setField("name", "Charlie");
        user3.setField("age", "35");
        user3.setField("status", "active");
        user3.setField("city", "Hamburg");
        secIdx->put("users", user3);
        
        BaseEntity user4("u4");
        user4.setField("name", "Diana");
        user4.setField("age", "28");
        user4.setField("status", "pending");
        user4.setField("city", "Berlin");
        secIdx->put("users", user4);
        
        BaseEntity user5("u5");
        user5.setField("name", "Eve");
        user5.setField("age", "40");
        user5.setField("status", "inactive");
        user5.setField("city", "Munich");
        secIdx->put("users", user5);
    }
    
    void TearDown() override {
        engine.reset();
        secIdx.reset();
        db.reset();
        std::filesystem::remove_all("data/themis_aql_or_test");
    }
    
    std::unique_ptr<RocksDBWrapper> db;
    std::unique_ptr<SecondaryIndexManager> secIdx;
    std::unique_ptr<QueryEngine> engine;
};

// ============================================================================
// Parser Tests
// ============================================================================

TEST_F(AQLOrTest, ParseSimpleOr) {
    std::string aql = R"(
        FOR user IN users
        FILTER user.status == "active" OR user.status == "pending"
        RETURN user
    )";
    
    AQLParser parser;
    auto result = parser.parse(aql);
    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query, nullptr);
    EXPECT_EQ(result.query->for_node.collection, "users");
    ASSERT_EQ(result.query->filters.size(), 1);
    
    auto filter = result.query->filters[0];
    ASSERT_EQ(filter->condition->getType(), ASTNodeType::BinaryOp);
    
    auto binOp = std::static_pointer_cast<BinaryOpExpr>(filter->condition);
    EXPECT_EQ(binOp->op, BinaryOperator::Or);
}

TEST_F(AQLOrTest, ParseMultipleOr) {
    std::string aql = R"(
        FOR user IN users
        FILTER user.city == "Berlin" OR user.city == "Munich" OR user.city == "Hamburg"
        RETURN user
    )";
    
    AQLParser parser;
    auto result = parser.parse(aql);
    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query, nullptr);
}

TEST_F(AQLOrTest, ParseMixedAndOr) {
    std::string aql = R"(
        FOR user IN users
        FILTER (user.status == "active" AND user.age > 25) OR user.city == "Berlin"
        RETURN user
    )";
    
    AQLParser parser;
    auto result = parser.parse(aql);
    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query, nullptr);
}

// ============================================================================
// Translator Tests
// ============================================================================

TEST_F(AQLOrTest, TranslateSimpleOr) {
    std::string aql = R"(
        FOR user IN users
        FILTER user.status == "active" OR user.status == "pending"
        RETURN user
    )";
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success) << translateResult.error_message;
    ASSERT_TRUE(translateResult.disjunctive.has_value());
    
    const auto& dq = *translateResult.disjunctive;
    EXPECT_EQ(dq.table, "users");
    EXPECT_EQ(dq.disjuncts.size(), 2); // Two alternatives: status==active OR status==pending
    
    // First disjunct: status == "active"
    EXPECT_EQ(dq.disjuncts[0].predicates.size(), 1);
    EXPECT_EQ(dq.disjuncts[0].predicates[0].column, "status");
    EXPECT_EQ(dq.disjuncts[0].predicates[0].value, "active");
    
    // Second disjunct: status == "pending"
    EXPECT_EQ(dq.disjuncts[1].predicates.size(), 1);
    EXPECT_EQ(dq.disjuncts[1].predicates[0].column, "status");
    EXPECT_EQ(dq.disjuncts[1].predicates[0].value, "pending");
}

TEST_F(AQLOrTest, TranslateMixedAndOr) {
    std::string aql = R"(
        FOR user IN users
        FILTER (user.status == "active" AND user.age >= 30) OR user.city == "Berlin"
        RETURN user
    )";
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success) << translateResult.error_message;
    ASSERT_TRUE(translateResult.disjunctive.has_value());
    
    const auto& dq = *translateResult.disjunctive;
    EXPECT_EQ(dq.disjuncts.size(), 2);
    
    // First disjunct: status=="active" AND age>=30
    EXPECT_EQ(dq.disjuncts[0].predicates.size(), 1); // status
    EXPECT_EQ(dq.disjuncts[0].rangePredicates.size(), 1); // age>=30
    
    // Second disjunct: city=="Berlin"
    EXPECT_EQ(dq.disjuncts[1].predicates.size(), 1);
    EXPECT_EQ(dq.disjuncts[1].predicates[0].column, "city");
}

TEST_F(AQLOrTest, TranslateDNFExpansion) {
    // (A OR B) AND (C OR D) should expand to (A AND C) OR (A AND D) OR (B AND C) OR (B AND D)
    std::string aql = R"(
        FOR user IN users
        FILTER (user.status == "active" OR user.status == "pending") AND (user.city == "Berlin" OR user.city == "Munich")
        RETURN user
    )";
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success) << translateResult.error_message;
    ASSERT_TRUE(translateResult.disjunctive.has_value());
    
    const auto& dq = *translateResult.disjunctive;
    EXPECT_EQ(dq.disjuncts.size(), 4); // 2x2 = 4 combinations
    
    // Each disjunct should have exactly 2 predicates (status AND city)
    for (const auto& conj : dq.disjuncts) {
        EXPECT_EQ(conj.predicates.size(), 2);
    }
}

// ============================================================================
// Execution Tests
// ============================================================================

TEST_F(AQLOrTest, ExecuteSimpleOr) {
    std::string aql = R"(
        FOR user IN users
        FILTER user.status == "active" OR user.status == "pending"
        RETURN user
    )";
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success);
    
    auto [status, keys] = engine->executeOrKeys(*translateResult.disjunctive);
    ASSERT_TRUE(status.ok) << status.message;
    
    // Should find u1 (active), u3 (active), u4 (pending)
    EXPECT_EQ(keys.size(), 3);
    
    std::set<std::string> keySet(keys.begin(), keys.end());
    EXPECT_TRUE(keySet.count("u1"));
    EXPECT_TRUE(keySet.count("u3"));
    EXPECT_TRUE(keySet.count("u4"));
}

TEST_F(AQLOrTest, ExecuteOrWithRange) {
    std::string aql = R"(
        FOR user IN users
        FILTER user.age < 28 OR user.age > 35
        RETURN user
    )";
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success);
    
    auto [status, keys] = engine->executeOrKeys(*translateResult.disjunctive);
    ASSERT_TRUE(status.ok) << status.message;
    
    // Should find u1 (age=25), u5 (age=40)
    EXPECT_EQ(keys.size(), 2);
    
    std::set<std::string> keySet(keys.begin(), keys.end());
    EXPECT_TRUE(keySet.count("u1"));
    EXPECT_TRUE(keySet.count("u5"));
}

TEST_F(AQLOrTest, ExecuteMixedAndOr) {
    std::string aql = R"(
        FOR user IN users
        FILTER (user.status == "active" AND user.city == "Berlin") OR user.age >= 35
        RETURN user
    )";
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success);
    
    auto [status, keys] = engine->executeOrKeys(*translateResult.disjunctive);
    ASSERT_TRUE(status.ok) << status.message;
    
    // Should find u1 (active + Berlin), u3 (age=35), u5 (age=40)
    EXPECT_EQ(keys.size(), 3);
    
    std::set<std::string> keySet(keys.begin(), keys.end());
    EXPECT_TRUE(keySet.count("u1"));
    EXPECT_TRUE(keySet.count("u3"));
    EXPECT_TRUE(keySet.count("u5"));
}

TEST_F(AQLOrTest, ExecuteComplexDNF) {
    std::string aql = R"(
        FOR user IN users
        FILTER (user.city == "Berlin" OR user.city == "Munich") AND user.status == "active"
        RETURN user
    )";
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success);
    
    auto [status, keys] = engine->executeOrKeys(*translateResult.disjunctive);
    ASSERT_TRUE(status.ok) << status.message;
    
    // Should find only u1 (Berlin + active)
    // u2 is Munich but inactive, u3 is Hamburg (not Berlin/Munich)
    EXPECT_EQ(keys.size(), 1);
    EXPECT_EQ(keys[0], "u1");
}

TEST_F(AQLOrTest, ExecuteTripleOr) {
    std::string aql = R"(
        FOR user IN users
        FILTER user.city == "Berlin" OR user.city == "Munich" OR user.city == "Hamburg"
        RETURN user
    )";
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success);
    
    auto [status, keys] = engine->executeOrKeys(*translateResult.disjunctive);
    ASSERT_TRUE(status.ok) << status.message;
    
    // Should find all 5 users (all cities covered)
    EXPECT_EQ(keys.size(), 5);
}

TEST_F(AQLOrTest, ExecuteOrNoResults) {
    std::string aql = R"(
        FOR user IN users
        FILTER user.status == "deleted" OR user.status == "archived"
        RETURN user
    )";
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success);
    
    auto [status, keys] = engine->executeOrKeys(*translateResult.disjunctive);
    ASSERT_TRUE(status.ok) << status.message;
    EXPECT_EQ(keys.size(), 0);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(AQLOrTest, PureAnd_ShouldUseConjunctiveQuery) {
    std::string aql = R"(
        FOR user IN users
        FILTER user.status == "active" AND user.age >= 30
        RETURN user
    )";
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success);
    
    // Should use ConjunctiveQuery, not DisjunctiveQuery
    EXPECT_FALSE(translateResult.disjunctive.has_value());
    EXPECT_EQ(translateResult.query.table, "users");
}

TEST_F(AQLOrTest, FulltextInOr_ShouldFail) {
    // FULLTEXT not yet supported in OR expressions
    std::string aql = R"(
        FOR doc IN articles
        FILTER FULLTEXT(doc.content, "AI") OR doc.year >= 2023
        RETURN doc
    )";
    
    // Create fulltext index first
    SecondaryIndexManager::FulltextConfig config;
    config.stemming_enabled = false;
    auto st = secIdx->createFulltextIndex("articles", "content", config);
    ASSERT_TRUE(st.ok);
    
    AQLParser parser;
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success) << translateResult.error_message;
    // Expect a disjunctive result because the FILTER contains OR
    EXPECT_TRUE(translateResult.disjunctive.has_value());
}
