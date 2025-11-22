#include <gtest/gtest.h>
#include "query/aql_parser.h"
#include "query/subquery_optimizer.h"

using namespace themis::query;

class SubqueryTest : public ::testing::Test {
protected:
    AQLParser parser;
};

// ============================================================================
// Phase 3.2: Scalar Subquery Tests
// ============================================================================

TEST_F(SubqueryTest, ScalarSubqueryInLet) {
    auto result = parser.parse(
        "FOR doc IN users "
        "LET avgAge = (FOR u IN users RETURN u.age) "
        "RETURN {user: doc.name, avgAge: avgAge[0]}"
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_EQ(result.query->let_nodes.size(), 1);
    
    const auto& letNode = result.query->let_nodes[0];
    EXPECT_EQ(letNode.variable, "avgAge");
    ASSERT_NE(letNode.expression, nullptr);
    EXPECT_EQ(letNode.expression->getType(), ASTNodeType::SubqueryExpr);
}

TEST_F(SubqueryTest, NestedSubquery) {
    auto result = parser.parse(
        "FOR doc IN orders "
        "LET userCount = (FOR u IN users FILTER u.country == doc.country RETURN u) "
        "RETURN doc"
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
}

// ============================================================================
// Phase 3.3: ANY/ALL Quantifier Tests
// ============================================================================

TEST_F(SubqueryTest, AnyQuantifier) {
    auto result = parser.parse(
        "FOR doc IN users "
        "FILTER ANY tag IN doc.tags SATISFIES tag == \"premium\" "
        "RETURN doc"
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_EQ(result.query->filters.size(), 1);
    
    auto filterExpr = result.query->filters[0]->condition;
    EXPECT_EQ(filterExpr->getType(), ASTNodeType::AnyExpr);
    
    auto anyExpr = std::static_pointer_cast<AnyExpr>(filterExpr);
    EXPECT_EQ(anyExpr->variable, "tag");
}

TEST_F(SubqueryTest, AllQuantifier) {
    auto result = parser.parse(
        "FOR doc IN products "
        "FILTER ALL price IN doc.prices SATISFIES price > 0 "
        "RETURN doc"
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_EQ(result.query->filters.size(), 1);
    
    auto filterExpr = result.query->filters[0]->condition;
    EXPECT_EQ(filterExpr->getType(), ASTNodeType::AllExpr);
    
    auto allExpr = std::static_pointer_cast<AllExpr>(filterExpr);
    EXPECT_EQ(allExpr->variable, "price");
}

TEST_F(SubqueryTest, AnyWithComplexCondition) {
    auto result = parser.parse(
        "FOR doc IN users "
        "FILTER ANY order IN doc.orders SATISFIES order.total > 100 AND order.status == \"completed\" "
        "RETURN doc"
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
    
    auto anyExpr = std::static_pointer_cast<AnyExpr>(result.query->filters[0]->condition);
    EXPECT_EQ(anyExpr->variable, "order");
    EXPECT_EQ(anyExpr->condition->getType(), ASTNodeType::BinaryOp);
}

TEST_F(SubqueryTest, AllWithFieldAccess) {
    auto result = parser.parse(
        "FOR doc IN hotels "
        "FILTER ALL room IN doc.rooms SATISFIES room.available == true "
        "RETURN doc"
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
}

// ============================================================================
// Phase 3.4: Correlated Subquery Tests (Parsing Only)
// ============================================================================

TEST_F(SubqueryTest, CorrelatedSubqueryPattern) {
    auto result = parser.parse(
        "FOR doc IN orders "
        "LET userEmail = (FOR u IN users FILTER u._key == doc.userId RETURN u.email) "
        "RETURN {order: doc._key, email: userEmail[0]}"
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
    
    // Verify subquery structure
    auto subqueryExpr = std::static_pointer_cast<SubqueryExpr>(result.query->let_nodes[0].expression);
    ASSERT_NE(subqueryExpr->subquery, nullptr);
    ASSERT_EQ(subqueryExpr->subquery->filters.size(), 1);
}

// ============================================================================
// Combined Features
// ============================================================================

TEST_F(SubqueryTest, WithClauseAndAnyQuantifier) {
    auto result = parser.parse(
        "WITH activeUsers AS ("
        "  FOR u IN users FILTER u.active == true RETURN u"
        ") "
        "FOR doc IN activeUsers "
        "FILTER ANY tag IN doc.tags SATISFIES tag == \"verified\" "
        "RETURN doc"
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query->with_clause, nullptr);
    ASSERT_EQ(result.query->filters.size(), 1);
}

TEST_F(SubqueryTest, NestedAnyAll) {
    auto result = parser.parse(
        "FOR doc IN products "
        "FILTER ANY category IN doc.categories "
        "  SATISFIES ALL tag IN category.tags SATISFIES tag != \"deprecated\" "
        "RETURN doc"
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
}

// ============================================================================
// Optimization Analyzer Tests
// ============================================================================

TEST_F(SubqueryTest, OptimizationSingleReference) {
    auto result = parser.parse(
        "WITH temp AS (FOR h IN hotels FILTER h.price > 100 RETURN h) "
        "FOR doc IN temp RETURN doc"
    );
    
    ASSERT_TRUE(result.success);
    
    const auto& cte = result.query->with_clause->ctes[0];
    
    // Single reference, no aggregation/sorting -> should inline
    bool shouldMat = SubqueryOptimizer::shouldMaterializeCTE(cte, 1);
    EXPECT_FALSE(shouldMat);
}

TEST_F(SubqueryTest, OptimizationMultipleReferences) {
    auto result = parser.parse(
        "WITH temp AS (FOR h IN hotels RETURN h) "
        "FOR doc IN temp RETURN doc"
    );
    
    ASSERT_TRUE(result.success);
    
    const auto& cte = result.query->with_clause->ctes[0];
    
    // Multiple references -> should materialize
    bool shouldMat = SubqueryOptimizer::shouldMaterializeCTE(cte, 3);
    EXPECT_TRUE(shouldMat);
}

TEST_F(SubqueryTest, OptimizationWithAggregation) {
    auto result = parser.parse(
        "WITH stats AS ("
        "  FOR h IN hotels "
        "  COLLECT city = h.city AGGREGATE avgPrice = AVG(h.price) "
        "  RETURN {city: city, avgPrice: avgPrice}"
        ") "
        "FOR doc IN stats RETURN doc"
    );
    
    ASSERT_TRUE(result.success);
    
    const auto& cte = result.query->with_clause->ctes[0];
    
    // Contains aggregation -> should materialize even with single reference
    bool shouldMat = SubqueryOptimizer::shouldMaterializeCTE(cte, 1);
    EXPECT_TRUE(shouldMat);
}

TEST_F(SubqueryTest, OptimizationCostEstimation) {
    auto simpleQuery = parser.parse("FOR h IN hotels RETURN h");
    ASSERT_TRUE(simpleQuery.success);
    
    auto complexQuery = parser.parse(
        "FOR u IN users "
        "FOR o IN orders "
        "FILTER o.userId == u._key "
        "COLLECT city = u.city AGGREGATE total = SUM(o.amount) "
        "SORT total DESC "
        "LIMIT 10 "
        "RETURN {city: city, total: total}"
    );
    ASSERT_TRUE(complexQuery.success);
    
    int simpleCost = SubqueryOptimizer::estimateQueryCost(simpleQuery.query);
    int complexCost = SubqueryOptimizer::estimateQueryCost(complexQuery.query);
    
    // Complex query should have higher cost
    EXPECT_GT(complexCost, simpleCost);
}

// ============================================================================
// JSON Serialization
// ============================================================================

TEST_F(SubqueryTest, AnyExprJsonSerialization) {
    auto result = parser.parse(
        "FOR doc IN users "
        "FILTER ANY tag IN doc.tags SATISFIES tag == \"admin\" "
        "RETURN doc"
    );
    
    ASSERT_TRUE(result.success);
    
    auto json = result.query->toJSON();
    auto filterJson = json["filters"][0]["condition"];
    
    EXPECT_EQ(filterJson["type"], "any");
    EXPECT_EQ(filterJson["variable"], "tag");
    EXPECT_TRUE(filterJson.contains("array"));
    EXPECT_TRUE(filterJson.contains("condition"));
}

TEST_F(SubqueryTest, SubqueryExprJsonSerialization) {
    auto result = parser.parse(
        "FOR doc IN users "
        "LET sub = (FOR h IN hotels RETURN h) "
        "RETURN doc"
    );
    
    ASSERT_TRUE(result.success);
    
    auto json = result.query->toJSON();
    auto letJson = json["lets"][0];
    
    EXPECT_EQ(letJson["expression"]["type"], "subquery");
    EXPECT_TRUE(letJson["expression"].contains("query"));
}

// ============================================================================
// Error Cases
// ============================================================================

TEST_F(SubqueryTest, AnyMissingVariable) {
    auto result = parser.parse(
        "FOR doc IN users "
        "FILTER ANY IN doc.tags SATISFIES tag == \"admin\" "
        "RETURN doc"
    );
    
    EXPECT_FALSE(result.success);
}

TEST_F(SubqueryTest, AnyMissingSatisfies) {
    auto result = parser.parse(
        "FOR doc IN users "
        "FILTER ANY tag IN doc.tags tag == \"admin\" "
        "RETURN doc"
    );
    
    EXPECT_FALSE(result.success);
}

TEST_F(SubqueryTest, AllMissingIn) {
    auto result = parser.parse(
        "FOR doc IN products "
        "FILTER ALL price doc.prices SATISFIES price > 0 "
        "RETURN doc"
    );
    
    EXPECT_FALSE(result.success);
}

// ============================================================================
// Phase 4.2: Subquery Execution Integration Tests
// ============================================================================

TEST_F(SubqueryTest, SubqueryExecution_ScalarResult) {
    // Test scalar subquery returning single value
    auto result = parser.parse(
        "FOR doc IN orders "
        "LET total = (FOR p IN products FILTER p.id == doc.product_id RETURN p.price) "
        "RETURN {order: doc, price: total}"
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_EQ(result.query->let_nodes.size(), 1);
    
    const auto& letNode = result.query->let_nodes[0];
    EXPECT_EQ(letNode.variable, "total");
    EXPECT_EQ(letNode.expression->getType(), ASTNodeType::SubqueryExpr);
    
    // Verify subquery structure
    auto subq = std::static_pointer_cast<SubqueryExpr>(letNode.expression);
    ASSERT_NE(subq->subquery, nullptr);
    EXPECT_EQ(subq->subquery->for_node.collection, "products");
}

TEST_F(SubqueryTest, SubqueryExecution_ArrayResult) {
    // Test subquery returning multiple values (array)
    auto result = parser.parse(
        "FOR doc IN categories "
        "LET items = (FOR p IN products FILTER p.category == doc.name RETURN p) "
        "RETURN {category: doc.name, items: items}"
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
}

TEST_F(SubqueryTest, SubqueryExecution_NestedSubqueries) {
    // Test nested subqueries
    auto result = parser.parse(
        "FOR doc IN users "
        "LET orderCount = (FOR o IN orders FILTER o.user_id == doc.id RETURN 1) "
        "FILTER (FOR a IN admins FILTER a.id == doc.id RETURN a) != null "
        "RETURN {user: doc, orders: orderCount}"
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
}

TEST_F(SubqueryTest, SubqueryExecution_WithCTE) {
    // Test subquery that contains CTEs
    auto result = parser.parse(
        "FOR doc IN orders "
        "LET enriched = (WITH expensive AS (FOR p IN products FILTER p.price > 100 RETURN p) "
        "                FOR ep IN expensive FILTER ep.id == doc.product_id RETURN ep) "
        "RETURN {order: doc, product: enriched}"
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
}

TEST_F(SubqueryTest, SubqueryExecution_CorrelatedSubquery) {
    // Test correlated subquery referencing outer variable
    auto result = parser.parse(
        "FOR user IN users "
        "FILTER (FOR o IN orders FILTER o.user_id == user.id RETURN o) != [] "
        "RETURN user"
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
    
    // Verify filter contains subquery
    ASSERT_EQ(result.query->filters.size(), 1);
    auto filterExpr = result.query->filters[0]->condition;
    EXPECT_EQ(filterExpr->getType(), ASTNodeType::BinaryOp);
}

TEST_F(SubqueryTest, SubqueryExecution_InReturnExpression) {
    // Test subquery directly in RETURN
    auto result = parser.parse(
        "FOR doc IN users "
        "RETURN {name: doc.name, orders: (FOR o IN orders FILTER o.user_id == doc.id RETURN o.total)}"
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
}

