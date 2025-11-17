#include <gtest/gtest.h>
#include "query/aql_translator.h"
#include "query/aql_parser.h"
#include "query/query_engine.h"
#include "index/secondary_index.h"
#include "storage/rocksdb_wrapper.h"
#include <nlohmann/json.hpp>

using namespace themis;
using namespace themis::query;

class OrNotQueryTest : public ::testing::Test {
protected:
    std::unique_ptr<RocksDBWrapper> db;
    std::unique_ptr<SecondaryIndexManager> secIdx;
    std::unique_ptr<QueryEngine> engine;
    AQLTranslator translator;

    void SetUp() override {
        db = std::make_unique<RocksDBWrapper>();
        db->open("/tmp/test_or_not_db", false);
        
        secIdx = std::make_unique<SecondaryIndexManager>(*db);
        engine = std::make_unique<QueryEngine>(*db, *secIdx);
        
        // Create test collection with secondary indexes
        secIdx->createIndex("users", "city");
        secIdx->createIndex("users", "age");
        secIdx->createRangeIndex("users", "age");
        
        // Insert test data
        insertTestData();
    }

    void TearDown() override {
        engine.reset();
        secIdx.reset();
        db.reset();
        system("rm -rf /tmp/test_or_not_db");
    }

    void insertTestData() {
        // User 1: Alice, Berlin, age 25
        nlohmann::json user1 = {
            {"name", "Alice"},
            {"city", "Berlin"},
            {"age", 25}
        };
        db->put("users:1", user1.dump());
        secIdx->indexDocument("users", "1", user1);

        // User 2: Bob, Munich, age 30
        nlohmann::json user2 = {
            {"name", "Bob"},
            {"city", "Munich"},
            {"age", 30}
        };
        db->put("users:2", user2.dump());
        secIdx->indexDocument("users", "2", user2);

        // User 3: Charlie, Berlin, age 35
        nlohmann::json user3 = {
            {"name", "Charlie"},
            {"city", "Berlin"},
            {"age", 35}
        };
        db->put("users:3", user3.dump());
        secIdx->indexDocument("users", "3", user3);

        // User 4: Diana, Hamburg, age 25
        nlohmann::json user4 = {
            {"name", "Diana"},
            {"city", "Hamburg"},
            {"age", 25}
        };
        db->put("users:4", user4.dump());
        secIdx->indexDocument("users", "4", user4);

        // User 5: Eve, Munich, age 40
        nlohmann::json user5 = {
            {"name", "Eve"},
            {"city", "Munich"},
            {"age", 40}
        };
        db->put("users:5", user5.dump());
        secIdx->indexDocument("users", "5", user5);
    }
};

// ============================================================================
// OR Query Tests
// ============================================================================

TEST_F(OrNotQueryTest, SimpleOrQuery) {
    // FOR doc IN users FILTER doc.city == "Berlin" OR doc.city == "Munich" RETURN doc
    
    AQLParser parser;
    auto query = parser.parse("FOR doc IN users FILTER doc.city == \"Berlin\" OR doc.city == \"Munich\" RETURN doc");
    ASSERT_TRUE(query != nullptr);

    auto result = translator.translate(query);
    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(std::holds_alternative<AQLTranslator::TranslationResult::DisjunctiveQuery>(result.value()));

    auto& disjQuery = std::get<AQLTranslator::TranslationResult::DisjunctiveQuery>(result.value());
    EXPECT_EQ(disjQuery.table, "users");
    EXPECT_EQ(disjQuery.disjuncts.size(), 2); // Two OR branches

    // Execute query
    auto [status, keys] = engine->executeOrKeys(disjQuery);
    ASSERT_TRUE(status.ok);
    EXPECT_EQ(keys.size(), 4); // Alice, Bob, Charlie, Eve (all from Berlin or Munich)
}

TEST_F(OrNotQueryTest, OrWithRangePredicates) {
    // FOR doc IN users FILTER doc.age < 28 OR doc.age > 38 RETURN doc
    
    AQLParser parser;
    auto query = parser.parse("FOR doc IN users FILTER doc.age < 28 OR doc.age > 38 RETURN doc");
    ASSERT_TRUE(query != nullptr);

    auto result = translator.translate(query);
    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(std::holds_alternative<AQLTranslator::TranslationResult::DisjunctiveQuery>(result.value()));

    auto& disjQuery = std::get<AQLTranslator::TranslationResult::DisjunctiveQuery>(result.value());
    EXPECT_EQ(disjQuery.disjuncts.size(), 2);

    // Execute query
    auto [status, keys] = engine->executeOrKeys(disjQuery);
    ASSERT_TRUE(status.ok);
    EXPECT_EQ(keys.size(), 3); // Alice (25), Diana (25), Eve (40)
}

TEST_F(OrNotQueryTest, ComplexOrQuery) {
    // FOR doc IN users FILTER (doc.city == "Berlin" AND doc.age > 30) OR (doc.city == "Munich" AND doc.age < 35) RETURN doc
    
    AQLParser parser;
    auto query = parser.parse("FOR doc IN users FILTER (doc.city == \"Berlin\" AND doc.age > 30) OR (doc.city == \"Munich\" AND doc.age < 35) RETURN doc");
    ASSERT_TRUE(query != nullptr);

    auto result = translator.translate(query);
    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(std::holds_alternative<AQLTranslator::TranslationResult::DisjunctiveQuery>(result.value()));

    auto& disjQuery = std::get<AQLTranslator::TranslationResult::DisjunctiveQuery>(result.value());
    EXPECT_EQ(disjQuery.disjuncts.size(), 2);

    // First disjunct: city == Berlin AND age > 30
    EXPECT_EQ(disjQuery.disjuncts[0].predicates.size(), 1); // city == Berlin
    EXPECT_EQ(disjQuery.disjuncts[0].rangePredicates.size(), 1); // age > 30

    // Second disjunct: city == Munich AND age < 35
    EXPECT_EQ(disjQuery.disjuncts[1].predicates.size(), 1); // city == Munich
    EXPECT_EQ(disjQuery.disjuncts[1].rangePredicates.size(), 1); // age < 35

    // Execute query
    auto [status, keys] = engine->executeOrKeys(disjQuery);
    ASSERT_TRUE(status.ok);
    EXPECT_EQ(keys.size(), 2); // Charlie (Berlin, 35), Bob (Munich, 30)
}

// ============================================================================
// NOT Query Tests
// ============================================================================

TEST_F(OrNotQueryTest, SimpleNotQuery) {
    // FOR doc IN users FILTER NOT (doc.city == "Berlin") RETURN doc
    // This should become: doc.city < "Berlin" OR doc.city > "Berlin"
    
    AQLParser parser;
    auto query = parser.parse("FOR doc IN users FILTER NOT (doc.city == \"Berlin\") RETURN doc");
    ASSERT_TRUE(query != nullptr);

    auto result = translator.translate(query);
    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(std::holds_alternative<AQLTranslator::TranslationResult::DisjunctiveQuery>(result.value()));

    auto& disjQuery = std::get<AQLTranslator::TranslationResult::DisjunctiveQuery>(result.value());
    EXPECT_EQ(disjQuery.disjuncts.size(), 2); // Converted to OR of two range predicates

    // Execute query
    auto [status, keys] = engine->executeOrKeys(disjQuery);
    ASSERT_TRUE(status.ok);
    EXPECT_EQ(keys.size(), 3); // Bob (Munich), Diana (Hamburg), Eve (Munich)
}

TEST_F(OrNotQueryTest, NotWithRangePredicate) {
    // FOR doc IN users FILTER NOT (doc.age < 30) RETURN doc
    // This becomes: doc.age >= 30
    
    AQLParser parser;
    auto query = parser.parse("FOR doc IN users FILTER NOT (doc.age < 30) RETURN doc");
    ASSERT_TRUE(query != nullptr);

    auto result = translator.translate(query);
    ASSERT_TRUE(result.ok());

    // Should be converted to simple conjunctive query with age >= 30
    // (NOT flips < to >=)
    ASSERT_TRUE(std::holds_alternative<AQLTranslator::TranslationResult::ConjunctiveQuery>(result.value()) ||
                std::holds_alternative<AQLTranslator::TranslationResult::DisjunctiveQuery>(result.value()));

    if (std::holds_alternative<AQLTranslator::TranslationResult::ConjunctiveQuery>(result.value())) {
        auto& conjQuery = std::get<AQLTranslator::TranslationResult::ConjunctiveQuery>(result.value());
        EXPECT_EQ(conjQuery.rangePredicates.size(), 1);
        EXPECT_EQ(conjQuery.rangePredicates[0].column, "age");
        EXPECT_TRUE(conjQuery.rangePredicates[0].lower.has_value());
        EXPECT_EQ(conjQuery.rangePredicates[0].lower.value(), "30");
        EXPECT_TRUE(conjQuery.rangePredicates[0].includeLower);
    }
}

TEST_F(OrNotQueryTest, DeMorganLaw_NotOrBecomesAndNot) {
    // FOR doc IN users FILTER NOT (doc.city == "Berlin" OR doc.city == "Munich") RETURN doc
    // De Morgan: NOT (A OR B) = (NOT A) AND (NOT B)
    // Becomes: NOT (city == Berlin) AND NOT (city == Munich)
    // Which becomes: (city < Berlin OR city > Berlin) AND (city < Munich OR city > Munich)
    
    AQLParser parser;
    auto query = parser.parse("FOR doc IN users FILTER NOT (doc.city == \"Berlin\" OR doc.city == \"Munich\") RETURN doc");
    ASSERT_TRUE(query != nullptr);

    auto result = translator.translate(query);
    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(std::holds_alternative<AQLTranslator::TranslationResult::DisjunctiveQuery>(result.value()));

    auto& disjQuery = std::get<AQLTranslator::TranslationResult::DisjunctiveQuery>(result.value());
    
    // After DNF expansion: (city < Berlin AND city < Munich) OR (city < Berlin AND city > Munich) OR ...
    // This creates 4 disjuncts from cartesian product
    EXPECT_GT(disjQuery.disjuncts.size(), 0);

    // Execute query
    auto [status, keys] = engine->executeOrKeys(disjQuery);
    ASSERT_TRUE(status.ok);
    EXPECT_EQ(keys.size(), 1); // Only Diana (Hamburg) - not in Berlin or Munich
}

TEST_F(OrNotQueryTest, DeMorganLaw_NotAndBecomesOrNot) {
    // FOR doc IN users FILTER NOT (doc.city == "Berlin" AND doc.age < 30) RETURN doc
    // De Morgan: NOT (A AND B) = (NOT A) OR (NOT B)
    // Becomes: NOT (city == Berlin) OR NOT (age < 30)
    // Which becomes: (city < Berlin OR city > Berlin) OR (age >= 30)
    
    AQLParser parser;
    auto query = parser.parse("FOR doc IN users FILTER NOT (doc.city == \"Berlin\" AND doc.age < 30) RETURN doc");
    ASSERT_TRUE(query != nullptr);

    auto result = translator.translate(query);
    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(std::holds_alternative<AQLTranslator::TranslationResult::DisjunctiveQuery>(result.value()));

    auto& disjQuery = std::get<AQLTranslator::TranslationResult::DisjunctiveQuery>(result.value());
    EXPECT_GT(disjQuery.disjuncts.size(), 0);

    // Execute query
    auto [status, keys] = engine->executeOrKeys(disjQuery);
    ASSERT_TRUE(status.ok);
    EXPECT_EQ(keys.size(), 4); // Bob, Charlie, Diana, Eve (all except Alice who is Berlin AND age < 30)
}

TEST_F(OrNotQueryTest, DoubleNegation) {
    // FOR doc IN users FILTER NOT (NOT (doc.city == "Berlin")) RETURN doc
    // Double negation cancels out: becomes doc.city == "Berlin"
    
    AQLParser parser;
    auto query = parser.parse("FOR doc IN users FILTER NOT (NOT (doc.city == \"Berlin\")) RETURN doc");
    ASSERT_TRUE(query != nullptr);

    auto result = translator.translate(query);
    ASSERT_TRUE(result.ok());

    // Should simplify to simple equality query
    if (std::holds_alternative<AQLTranslator::TranslationResult::ConjunctiveQuery>(result.value())) {
        auto& conjQuery = std::get<AQLTranslator::TranslationResult::ConjunctiveQuery>(result.value());
        EXPECT_EQ(conjQuery.predicates.size(), 1);
        EXPECT_EQ(conjQuery.predicates[0].column, "city");
        EXPECT_EQ(conjQuery.predicates[0].value, "Berlin");
    }
}

// ============================================================================
// NEQ (!=) Query Tests
// ============================================================================

TEST_F(OrNotQueryTest, NeqConvertedToOr) {
    // FOR doc IN users FILTER doc.city != "Berlin" RETURN doc
    // NEQ is converted to: city < "Berlin" OR city > "Berlin"
    
    AQLParser parser;
    auto query = parser.parse("FOR doc IN users FILTER doc.city != \"Berlin\" RETURN doc");
    ASSERT_TRUE(query != nullptr);

    auto result = translator.translate(query);
    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(std::holds_alternative<AQLTranslator::TranslationResult::DisjunctiveQuery>(result.value()));

    auto& disjQuery = std::get<AQLTranslator::TranslationResult::DisjunctiveQuery>(result.value());
    EXPECT_EQ(disjQuery.disjuncts.size(), 2); // Two OR branches

    // Execute query
    auto [status, keys] = engine->executeOrKeys(disjQuery);
    ASSERT_TRUE(status.ok);
    EXPECT_EQ(keys.size(), 3); // Bob (Munich), Diana (Hamburg), Eve (Munich)
}

TEST_F(OrNotQueryTest, NeqWithAndCondition) {
    // FOR doc IN users FILTER doc.city != "Berlin" AND doc.age > 25 RETURN doc
    // Becomes: (city < Berlin AND age > 25) OR (city > Berlin AND age > 25)
    
    AQLParser parser;
    auto query = parser.parse("FOR doc IN users FILTER doc.city != \"Berlin\" AND doc.age > 25 RETURN doc");
    ASSERT_TRUE(query != nullptr);

    auto result = translator.translate(query);
    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(std::holds_alternative<AQLTranslator::TranslationResult::DisjunctiveQuery>(result.value()));

    auto& disjQuery = std::get<AQLTranslator::TranslationResult::DisjunctiveQuery>(result.value());
    EXPECT_EQ(disjQuery.disjuncts.size(), 2);

    // Each disjunct should have 1 range predicate for city and 1 for age
    for (const auto& disjunct : disjQuery.disjuncts) {
        EXPECT_EQ(disjunct.rangePredicates.size(), 2);
    }

    // Execute query
    auto [status, keys] = engine->executeOrKeys(disjQuery);
    ASSERT_TRUE(status.ok);
    EXPECT_EQ(keys.size(), 3); // Bob (30), Diana (should not match), Eve (40) -> Actually Bob and Eve
}

// ============================================================================
// Complex Combined Tests
// ============================================================================

TEST_F(OrNotQueryTest, ComplexNotOrAnd) {
    // FOR doc IN users FILTER NOT ((doc.city == "Berlin" OR doc.city == "Munich") AND doc.age < 30) RETURN doc
    // NOT ((A OR B) AND C) = NOT (A OR B) OR NOT C = (NOT A AND NOT B) OR NOT C
    
    AQLParser parser;
    auto query = parser.parse("FOR doc IN users FILTER NOT ((doc.city == \"Berlin\" OR doc.city == \"Munich\") AND doc.age < 30) RETURN doc");
    ASSERT_TRUE(query != nullptr);

    auto result = translator.translate(query);
    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(std::holds_alternative<AQLTranslator::TranslationResult::DisjunctiveQuery>(result.value()));

    auto& disjQuery = std::get<AQLTranslator::TranslationResult::DisjunctiveQuery>(result.value());
    EXPECT_GT(disjQuery.disjuncts.size(), 0);

    // Execute query
    auto [status, keys] = engine->executeOrKeys(disjQuery);
    ASSERT_TRUE(status.ok);
    EXPECT_GT(keys.size(), 0); // Should return users not matching the condition
}

TEST_F(OrNotQueryTest, MultipleOrConditions) {
    // FOR doc IN users FILTER doc.city == "Berlin" OR doc.city == "Munich" OR doc.city == "Hamburg" RETURN doc
    
    AQLParser parser;
    auto query = parser.parse("FOR doc IN users FILTER doc.city == \"Berlin\" OR doc.city == \"Munich\" OR doc.city == \"Hamburg\" RETURN doc");
    ASSERT_TRUE(query != nullptr);

    auto result = translator.translate(query);
    ASSERT_TRUE(result.ok());
    ASSERT_TRUE(std::holds_alternative<AQLTranslator::TranslationResult::DisjunctiveQuery>(result.value()));

    auto& disjQuery = std::get<AQLTranslator::TranslationResult::DisjunctiveQuery>(result.value());
    EXPECT_EQ(disjQuery.disjuncts.size(), 3); // Three OR branches

    // Execute query
    auto [status, keys] = engine->executeOrKeys(disjQuery);
    ASSERT_TRUE(status.ok);
    EXPECT_EQ(keys.size(), 5); // All users
}

TEST_F(OrNotQueryTest, NotInequality) {
    // FOR doc IN users FILTER NOT (doc.age > 30) RETURN doc
    // Becomes: age <= 30
    
    AQLParser parser;
    auto query = parser.parse("FOR doc IN users FILTER NOT (doc.age > 30) RETURN doc");
    ASSERT_TRUE(query != nullptr);

    auto result = translator.translate(query);
    ASSERT_TRUE(result.ok());

    // Should be simple conjunctive query
    if (std::holds_alternative<AQLTranslator::TranslationResult::ConjunctiveQuery>(result.value())) {
        auto& conjQuery = std::get<AQLTranslator::TranslationResult::ConjunctiveQuery>(result.value());
        EXPECT_EQ(conjQuery.rangePredicates.size(), 1);
        EXPECT_EQ(conjQuery.rangePredicates[0].column, "age");
        EXPECT_TRUE(conjQuery.rangePredicates[0].upper.has_value());
        EXPECT_EQ(conjQuery.rangePredicates[0].upper.value(), "30");
        EXPECT_TRUE(conjQuery.rangePredicates[0].includeUpper); // <= not <
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
