#include <gtest/gtest.h>
#include "query/aql_parser.h"

using namespace themis::query;

class AQLWithClauseTest : public ::testing::Test {
protected:
    AQLParser parser;
};

// ============================================================================
// Phase 3.1: Basic WITH Clause Tests
// ============================================================================

TEST_F(AQLWithClauseTest, SimpleWithClause) {
    auto result = parser.parse(
        "WITH expensiveHotels AS ("
        "  FOR h IN hotels "
        "  FILTER h.price > 100 "
        "  RETURN h"
        ") "
        "FOR doc IN expensiveHotels "
        "RETURN doc"
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query, nullptr);
    
    // Verify WITH clause
    ASSERT_NE(result.query->with_clause, nullptr);
    ASSERT_EQ(result.query->with_clause->ctes.size(), 1);
    
    // Verify CTE definition
    const auto& cte = result.query->with_clause->ctes[0];
    EXPECT_EQ(cte.name, "expensiveHotels");
    ASSERT_NE(cte.subquery, nullptr);
    
    // Verify subquery structure
    EXPECT_EQ(cte.subquery->for_node.variable, "h");
    EXPECT_EQ(cte.subquery->for_node.collection, "hotels");
    ASSERT_EQ(cte.subquery->filters.size(), 1);
    ASSERT_NE(cte.subquery->return_node, nullptr);
    
    // Verify main query
    EXPECT_EQ(result.query->for_node.variable, "doc");
    EXPECT_EQ(result.query->for_node.collection, "expensiveHotels");
}

TEST_F(AQLWithClauseTest, MultipleCtEs) {
    auto result = parser.parse(
        "WITH "
        "  active AS (FOR u IN users FILTER u.active == true RETURN u), "
        "  premium AS (FOR u IN users FILTER u.tier == \"premium\" RETURN u) "
        "FOR doc IN active "
        "RETURN doc"
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query->with_clause, nullptr);
    ASSERT_EQ(result.query->with_clause->ctes.size(), 2);
    
    // First CTE
    EXPECT_EQ(result.query->with_clause->ctes[0].name, "active");
    EXPECT_EQ(result.query->with_clause->ctes[0].subquery->for_node.collection, "users");
    
    // Second CTE
    EXPECT_EQ(result.query->with_clause->ctes[1].name, "premium");
    EXPECT_EQ(result.query->with_clause->ctes[1].subquery->for_node.collection, "users");
}

TEST_F(AQLWithClauseTest, WithClauseWithAggregation) {
    auto result = parser.parse(
        "WITH cityStats AS ("
        "  FOR h IN hotels "
        "  COLLECT city = h.city AGGREGATE avgPrice = AVG(h.price) "
        "  RETURN {city: city, avgPrice: avgPrice}"
        ") "
        "FOR stat IN cityStats "
        "FILTER stat.avgPrice > 150 "
        "RETURN stat"
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query->with_clause, nullptr);
    
    const auto& cte = result.query->with_clause->ctes[0];
    EXPECT_EQ(cte.name, "cityStats");
    ASSERT_NE(cte.subquery->collect, nullptr);
}

TEST_F(AQLWithClauseTest, WithClauseWithSort) {
    auto result = parser.parse(
        "WITH topHotels AS ("
        "  FOR h IN hotels "
        "  SORT h.rating DESC "
        "  LIMIT 10 "
        "  RETURN h"
        ") "
        "FOR doc IN topHotels "
        "RETURN doc.name"
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query->with_clause, nullptr);
    
    const auto& cte = result.query->with_clause->ctes[0];
    ASSERT_NE(cte.subquery->sort, nullptr);
    ASSERT_NE(cte.subquery->limit, nullptr);
}

TEST_F(AQLWithClauseTest, WithClauseWithLet) {
    auto result = parser.parse(
        "WITH enriched AS ("
        "  FOR u IN users "
        "  LET fullName = CONCAT(u.firstName, \" \", u.lastName) "
        "  RETURN {name: fullName, age: u.age}"
        ") "
        "FOR doc IN enriched "
        "RETURN doc"
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query->with_clause, nullptr);
    
    const auto& cte = result.query->with_clause->ctes[0];
    ASSERT_EQ(cte.subquery->let_nodes.size(), 1);
    EXPECT_EQ(cte.subquery->let_nodes[0].variable, "fullName");
}

// ============================================================================
// Error Cases
// ============================================================================

TEST_F(AQLWithClauseTest, MissingAsKeyword) {
    auto result = parser.parse(
        "WITH expensiveHotels (FOR h IN hotels RETURN h) "
        "FOR doc IN expensiveHotels RETURN doc"
    );
    
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.message.find("AS"), std::string::npos);
}

TEST_F(AQLWithClauseTest, MissingParentheses) {
    auto result = parser.parse(
        "WITH expensiveHotels AS FOR h IN hotels RETURN h "
        "FOR doc IN expensiveHotels RETURN doc"
    );
    
    EXPECT_FALSE(result.success);
}

TEST_F(AQLWithClauseTest, MissingCteName) {
    auto result = parser.parse(
        "WITH AS (FOR h IN hotels RETURN h) "
        "FOR doc IN expensiveHotels RETURN doc"
    );
    
    EXPECT_FALSE(result.success);
}

TEST_F(AQLWithClauseTest, EmptyWithClause) {
    auto result = parser.parse(
        "WITH FOR doc IN hotels RETURN doc"
    );
    
    EXPECT_FALSE(result.success);
}

// ============================================================================
// JSON Serialization
// ============================================================================

TEST_F(AQLWithClauseTest, JsonSerialization) {
    auto result = parser.parse(
        "WITH temp AS (FOR h IN hotels FILTER h.price > 100 RETURN h) "
        "FOR doc IN temp RETURN doc"
    );
    
    ASSERT_TRUE(result.success);
    
    auto json = result.query->toJSON();
    EXPECT_TRUE(json.contains("with"));
    EXPECT_TRUE(json["with"].is_object());
    EXPECT_TRUE(json["with"].contains("ctes"));
    EXPECT_TRUE(json["with"]["ctes"].is_array());
    EXPECT_EQ(json["with"]["ctes"].size(), 1);
    
    auto cteJson = json["with"]["ctes"][0];
    EXPECT_EQ(cteJson["name"], "temp");
    EXPECT_TRUE(cteJson.contains("subquery"));
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(AQLWithClauseTest, WithClauseAtEnd) {
    // WITH clause should appear before FOR
    auto result = parser.parse(
        "FOR doc IN hotels RETURN doc "
        "WITH temp AS (FOR h IN hotels RETURN h)"
    );
    
    // This should fail or ignore the WITH (depending on implementation)
    // For now, we expect parse error since WITH must come first
    EXPECT_FALSE(result.success);
}

TEST_F(AQLWithClauseTest, NestedWithInSubquery) {
    // Nested WITH clauses are allowed
    auto result = parser.parse(
        "WITH outer AS ("
        "  WITH inner AS (FOR h IN hotels FILTER h.active == true RETURN h) "
        "  FOR doc IN inner FILTER doc.price > 50 RETURN doc"
        ") "
        "FOR doc IN outer RETURN doc"
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query->with_clause, nullptr);
    
    const auto& outerCte = result.query->with_clause->ctes[0];
    EXPECT_EQ(outerCte.name, "outer");
    
    // Subquery should also have WITH clause
    ASSERT_NE(outerCte.subquery->with_clause, nullptr);
    EXPECT_EQ(outerCte.subquery->with_clause->ctes[0].name, "inner");
}

TEST_F(AQLWithClauseTest, ComplexMultiCteExample) {
    auto result = parser.parse(
        "WITH "
        "  highRated AS ("
        "    FOR h IN hotels "
        "    FILTER h.rating > 4.5 "
        "    SORT h.rating DESC "
        "    LIMIT 100 "
        "    RETURN h"
        "  ), "
        "  nearby AS ("
        "    FOR h IN highRated "
        "    FILTER ST_DISTANCE(h.location, @userLocation) < 5000 "
        "    RETURN h"
        "  ), "
        "  affordable AS ("
        "    FOR h IN nearby "
        "    FILTER h.price < 200 "
        "    SORT h.price ASC "
        "    RETURN h"
        "  ) "
        "FOR doc IN affordable "
        "LIMIT 10 "
        "RETURN {name: doc.name, price: doc.price, rating: doc.rating}"
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query->with_clause, nullptr);
    ASSERT_EQ(result.query->with_clause->ctes.size(), 3);
    
    EXPECT_EQ(result.query->with_clause->ctes[0].name, "highRated");
    EXPECT_EQ(result.query->with_clause->ctes[1].name, "nearby");
    EXPECT_EQ(result.query->with_clause->ctes[2].name, "affordable");
}
