#include <gtest/gtest.h>
#include "query/aql_parser.h"
#include <iostream>

using namespace themis::query;

// ============================================================================
// Basic Syntax Tests
// ============================================================================

TEST(AQLParserTest, SimpleForClause) {
    AQLParser parser;
    auto result = parser.parse("FOR doc IN users RETURN doc");
    
    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query, nullptr);
    
    EXPECT_EQ(result.query->for_node.variable, "doc");
    EXPECT_EQ(result.query->for_node.collection, "users");
    EXPECT_TRUE(result.query->filters.empty());
    EXPECT_EQ(result.query->sort, nullptr);
    EXPECT_EQ(result.query->limit, nullptr);
    EXPECT_NE(result.query->return_node, nullptr);
}

TEST(AQLParserTest, ForWithEqualityFilter) {
    AQLParser parser;
    auto result = parser.parse("FOR user IN users FILTER user.age == 25 RETURN user");
    
    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query, nullptr);
    
    EXPECT_EQ(result.query->for_node.variable, "user");
    EXPECT_EQ(result.query->for_node.collection, "users");
    EXPECT_EQ(result.query->filters.size(), 1);
    
    // Print AST as JSON for debugging
    std::cout << "AST: " << result.query->toJSON().dump(2) << std::endl;
}

TEST(AQLParserTest, ForWithRangeFilter) {
    AQLParser parser;
    auto result = parser.parse("FOR doc IN products FILTER doc.price > 100.0 RETURN doc");
    
    ASSERT_TRUE(result.success) << result.error.toString();
    EXPECT_EQ(result.query->filters.size(), 1);
}

TEST(AQLParserTest, ForWithMultipleFilters) {
    AQLParser parser;
    auto result = parser.parse(
        "FOR u IN users "
        "FILTER u.age > 18 "
        "FILTER u.city == \"Berlin\" "
        "RETURN u"
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
    EXPECT_EQ(result.query->filters.size(), 2);
}

TEST(AQLParserTest, ForWithAndFilter) {
    AQLParser parser;
    auto result = parser.parse(
        "FOR u IN users FILTER u.age > 18 AND u.city == \"Berlin\" RETURN u"
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
    EXPECT_EQ(result.query->filters.size(), 1);
    
    // Verify it's a binary AND operation
    auto& filter = result.query->filters[0];
    EXPECT_EQ(filter->condition->getType(), ASTNodeType::BinaryOp);
}

TEST(AQLParserTest, ForWithSort) {
    AQLParser parser;
    auto result = parser.parse(
        "FOR doc IN users SORT doc.age DESC RETURN doc"
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query->sort, nullptr);
    EXPECT_EQ(result.query->sort->specifications.size(), 1);
    EXPECT_FALSE(result.query->sort->specifications[0].ascending);
}

TEST(AQLParserTest, ForWithMultiColumnSort) {
    AQLParser parser;
    auto result = parser.parse(
        "FOR doc IN users SORT doc.city ASC, doc.age DESC RETURN doc"
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query->sort, nullptr);
    EXPECT_EQ(result.query->sort->specifications.size(), 2);
    EXPECT_TRUE(result.query->sort->specifications[0].ascending);
    EXPECT_FALSE(result.query->sort->specifications[1].ascending);
}

TEST(AQLParserTest, ForWithLimitCount) {
    AQLParser parser;
    auto result = parser.parse("FOR doc IN users LIMIT 10 RETURN doc");
    
    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query->limit, nullptr);
    EXPECT_EQ(result.query->limit->offset, 0);
    EXPECT_EQ(result.query->limit->count, 10);
}

TEST(AQLParserTest, ForWithLimitOffsetCount) {
    AQLParser parser;
    auto result = parser.parse("FOR doc IN users LIMIT 20, 10 RETURN doc");
    
    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query->limit, nullptr);
    EXPECT_EQ(result.query->limit->offset, 20);
    EXPECT_EQ(result.query->limit->count, 10);
}

TEST(AQLParserTest, CompleteQuery) {
    AQLParser parser;
    auto result = parser.parse(
        "FOR user IN users "
        "FILTER user.age > 18 AND user.city == \"Berlin\" "
        "SORT user.created_at DESC "
        "LIMIT 10 "
        "RETURN user"
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
    
    EXPECT_EQ(result.query->for_node.variable, "user");
    EXPECT_EQ(result.query->for_node.collection, "users");
    EXPECT_EQ(result.query->filters.size(), 1);
    EXPECT_NE(result.query->sort, nullptr);
    EXPECT_NE(result.query->limit, nullptr);
    EXPECT_NE(result.query->return_node, nullptr);
    
    std::cout << "Complete Query AST:\n" << result.query->toJSON().dump(2) << std::endl;
}

// ============================================================================
// Literal Tests
// ============================================================================

TEST(AQLParserTest, StringLiteral) {
    AQLParser parser;
    auto result = parser.parse("FOR doc IN users FILTER doc.name == \"Alice\" RETURN doc");
    
    ASSERT_TRUE(result.success) << result.error.toString();
}

TEST(AQLParserTest, IntegerLiteral) {
    AQLParser parser;
    auto result = parser.parse("FOR doc IN users FILTER doc.age == 25 RETURN doc");
    
    ASSERT_TRUE(result.success) << result.error.toString();
}

TEST(AQLParserTest, FloatLiteral) {
    AQLParser parser;
    auto result = parser.parse("FOR doc IN products FILTER doc.price == 99.99 RETURN doc");
    
    ASSERT_TRUE(result.success) << result.error.toString();
}

TEST(AQLParserTest, BooleanLiteral) {
    AQLParser parser;
    auto result = parser.parse("FOR doc IN users FILTER doc.active == true RETURN doc");
    
    ASSERT_TRUE(result.success) << result.error.toString();
}

TEST(AQLParserTest, NullLiteral) {
    AQLParser parser;
    auto result = parser.parse("FOR doc IN users FILTER doc.deleted_at == null RETURN doc");
    
    ASSERT_TRUE(result.success) << result.error.toString();
}

// ============================================================================
// Operator Tests
// ============================================================================

TEST(AQLParserTest, ComparisonOperators) {
    AQLParser parser;
    
    std::vector<std::string> queries = {
        "FOR doc IN users FILTER doc.age == 25 RETURN doc",
        "FOR doc IN users FILTER doc.age != 25 RETURN doc",
        "FOR doc IN users FILTER doc.age < 25 RETURN doc",
        "FOR doc IN users FILTER doc.age <= 25 RETURN doc",
        "FOR doc IN users FILTER doc.age > 25 RETURN doc",
        "FOR doc IN users FILTER doc.age >= 25 RETURN doc"
    };
    
    for (const auto& query : queries) {
        auto result = parser.parse(query);
        EXPECT_TRUE(result.success) << "Failed to parse: " << query 
                                     << "\nError: " << result.error.toString();
    }
}

TEST(AQLParserTest, LogicalOperators) {
    AQLParser parser;
    
    auto result1 = parser.parse("FOR doc IN users FILTER doc.age > 18 AND doc.active == true RETURN doc");
    ASSERT_TRUE(result1.success) << result1.error.toString();
    
    auto result2 = parser.parse("FOR doc IN users FILTER doc.age < 18 OR doc.age > 65 RETURN doc");
    ASSERT_TRUE(result2.success) << result2.error.toString();
}

TEST(AQLParserTest, MembershipInArray) {
    AQLParser parser;
    auto result = parser.parse("FOR doc IN users FILTER doc.role IN [\"admin\", \"analyst\"] RETURN doc");
    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_EQ(result.query->filters.size(), 1);
    // Expect BinaryOp IN at filter root
    auto cond = result.query->filters[0]->condition;
    ASSERT_EQ(cond->getType(), ASTNodeType::BinaryOp);
}

TEST(AQLParserTest, MembershipInVariable) {
    AQLParser parser;
    auto result = parser.parse("FOR u IN users LET allowed = [\"de\", \"us\"] FILTER u.country IN allowed RETURN u");
    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_EQ(result.query->filters.size(), 1);
}

// ============================================================================
// Field Access Tests
// ============================================================================

TEST(AQLParserTest, SimpleFieldAccess) {
    AQLParser parser;
    auto result = parser.parse("FOR doc IN users FILTER doc.age > 18 RETURN doc");
    
    ASSERT_TRUE(result.success) << result.error.toString();
}

TEST(AQLParserTest, NestedFieldAccess) {
    AQLParser parser;
    auto result = parser.parse("FOR doc IN users FILTER doc.address.city == \"Berlin\" RETURN doc");
    
    ASSERT_TRUE(result.success) << result.error.toString();
    
    std::cout << "Nested Field AST:\n" << result.query->toJSON().dump(2) << std::endl;
}

// ============================================================================
// Error Tests
// ============================================================================

TEST(AQLParserTest, EmptyQuery) {
    AQLParser parser;
    auto result = parser.parse("");
    
    EXPECT_FALSE(result.success);
}

TEST(AQLParserTest, MissingINKeyword) {
    AQLParser parser;
    auto result = parser.parse("FOR doc users RETURN doc");
    
    EXPECT_FALSE(result.success);
}

TEST(AQLParserTest, MissingCollection) {
    AQLParser parser;
    auto result = parser.parse("FOR doc IN RETURN doc");
    
    EXPECT_FALSE(result.success);
}

TEST(AQLParserTest, InvalidOperator) {
    AQLParser parser;
    auto result = parser.parse("FOR doc IN users FILTER doc.age === 25 RETURN doc");
    
    EXPECT_FALSE(result.success);
}

// ============================================================================
// Whitespace Tests
// ============================================================================

TEST(AQLParserTest, MinimalWhitespace) {
    AQLParser parser;
    auto result = parser.parse("FOR doc IN users FILTER doc.age>18 RETURN doc");
    
    ASSERT_TRUE(result.success) << result.error.toString();
}

TEST(AQLParserTest, ExtraWhitespace) {
    AQLParser parser;
    auto result = parser.parse(
        "  FOR   doc   IN   users   "
        "FILTER   doc.age   >   18   "
        "RETURN   doc  "
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
}

TEST(AQLParserTest, Newlines) {
    AQLParser parser;
    auto result = parser.parse(
        "FOR doc IN users\n"
        "FILTER doc.age > 18\n"
        "SORT doc.created_at DESC\n"
        "LIMIT 10\n"
        "RETURN doc"
    );
    
    ASSERT_TRUE(result.success) << result.error.toString();
}

// ============================================================================
// Case Insensitivity Tests
// ============================================================================

TEST(AQLParserTest, KeywordsCaseInsensitive) {
    AQLParser parser;
    
    auto result1 = parser.parse("for doc in users filter doc.age > 18 return doc");
    EXPECT_TRUE(result1.success) << result1.error.toString();
    
    auto result2 = parser.parse("For Doc In Users Filter Doc.Age > 18 Return Doc");
    EXPECT_TRUE(result2.success) << result2.error.toString();
    
    auto result3 = parser.parse("FOR DOC IN USERS FILTER DOC.AGE > 18 RETURN DOC");
    EXPECT_TRUE(result3.success) << result3.error.toString();
}

// ============================================================================
// LET & Projection Tests (MVP)
// ============================================================================

TEST(AQLParserTest, LetSimpleBindingVariable) {
    AQLParser parser;
    auto result = parser.parse("FOR u IN users LET c = u.city RETURN c");

    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query, nullptr);
    ASSERT_EQ(result.query->let_nodes.size(), 1);
    EXPECT_EQ(result.query->let_nodes[0].variable, "c");
    ASSERT_NE(result.query->return_node, nullptr);
    EXPECT_EQ(result.query->return_node->expression->getType(), ASTNodeType::Variable);
}

TEST(AQLParserTest, ReturnObjectConstructWithLets) {
    AQLParser parser;
    auto result = parser.parse("FOR u IN users LET c = u.city RETURN {name: u.name, city: c}");

    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query, nullptr);
    ASSERT_EQ(result.query->let_nodes.size(), 1);
    ASSERT_NE(result.query->return_node, nullptr);
    EXPECT_EQ(result.query->return_node->expression->getType(), ASTNodeType::ObjectConstruct);
}

TEST(AQLParserTest, ReturnArrayLiteral) {
    AQLParser parser;
    auto result = parser.parse("FOR u IN users RETURN [u.name, u.age]");

    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query->return_node, nullptr);
    EXPECT_EQ(result.query->return_node->expression->getType(), ASTNodeType::ArrayLiteral);
}

TEST(AQLParserTest, MultipleLetsOrder) {
    AQLParser parser;
    auto result = parser.parse("FOR u IN users LET a = u.name LET b = a RETURN b");

    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_EQ(result.query->let_nodes.size(), 2);
    EXPECT_EQ(result.query->let_nodes[0].variable, "a");
    EXPECT_EQ(result.query->let_nodes[1].variable, "b");
    ASSERT_NE(result.query->return_node, nullptr);
    EXPECT_EQ(result.query->return_node->expression->getType(), ASTNodeType::Variable);
}

TEST(AQLParserTest, LetUsedInFilter) {
    AQLParser parser;
    auto result = parser.parse("FOR u IN users LET c = u.city FILTER c == \"Berlin\" RETURN u");

    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_EQ(result.query->let_nodes.size(), 1);
    ASSERT_EQ(result.query->filters.size(), 1);
}

TEST(AQLParserTest, DoubleForEqualityJoinParsing) {
    AQLParser parser;
    auto result = parser.parse("FOR u IN users FOR o IN orders FILTER u._key == o.user_id RETURN u");

    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_FALSE(result.query->for_nodes.empty());
    ASSERT_EQ(result.query->for_nodes.size(), 2);
}

// ============================================================================
// Graph Traversal Tests
// ============================================================================

TEST(AQLParserTest, GraphTraversalWithTypeFilter) {
    AQLParser parser;
    auto result = parser.parse("FOR v IN 1..2 OUTBOUND \"users/1\" TYPE \"follows\" GRAPH \"social\" RETURN v");

    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query, nullptr);
    // For compatibility, collection is set to "graph"
    EXPECT_EQ(result.query->for_node.collection, "graph");
    ASSERT_NE(result.query->traversal, nullptr);
    EXPECT_EQ(result.query->traversal->minDepth, 1);
    EXPECT_EQ(result.query->traversal->maxDepth, 2);
    EXPECT_EQ(result.query->traversal->startVertex, "users/1");
    EXPECT_EQ(result.query->traversal->graphName, "social");
    EXPECT_EQ(result.query->traversal->edgeType, "follows");
}

TEST(AQLParserTest, GraphTraversalWithoutType) {
    AQLParser parser;
    auto result = parser.parse("FOR v IN 2..3 INBOUND \"users/42\" GRAPH \"g\" RETURN v");

    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query->traversal, nullptr);
    EXPECT_EQ(result.query->traversal->minDepth, 2);
    EXPECT_EQ(result.query->traversal->maxDepth, 3);
    EXPECT_EQ(result.query->traversal->edgeType, "");
}

// ============================================================================
// Vector Search Function Tests
// ============================================================================

TEST(AQLParserTest, VectorSearchFunctionCall) {
    AQLParser parser;
    auto result = parser.parse("FOR doc IN products LET similar = VECTOR_SEARCH(\"products\", doc.embedding, 10) RETURN similar");

    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query, nullptr);
    ASSERT_EQ(result.query->let_nodes.size(), 1);
    EXPECT_EQ(result.query->let_nodes[0].variable, "similar");
    ASSERT_NE(result.query->let_nodes[0].expression, nullptr);
    EXPECT_EQ(result.query->let_nodes[0].expression->getType(), ASTNodeType::FunctionCall);
}

TEST(AQLParserTest, VectorSearchInLet) {
    AQLParser parser;
    auto result = parser.parse("FOR doc IN products LET results = VECTOR_SEARCH(\"products\", doc.embedding, 5) RETURN results");

    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_EQ(result.query->let_nodes.size(), 1);
    EXPECT_EQ(result.query->let_nodes[0].variable, "results");
    ASSERT_NE(result.query->let_nodes[0].expression, nullptr);
    // Verify the expression is a function call
    EXPECT_EQ(result.query->let_nodes[0].expression->getType(), ASTNodeType::FunctionCall);
}

// ============================================================================
// Content/File Helper Function Tests
// ============================================================================

TEST(AQLParserTest, ContentMetaFunction) {
    AQLParser parser;
    auto result = parser.parse("FOR doc IN documents LET meta = CONTENT_META(doc._key) RETURN meta");

    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_EQ(result.query->let_nodes.size(), 1);
    EXPECT_EQ(result.query->let_nodes[0].variable, "meta");
    ASSERT_NE(result.query->let_nodes[0].expression, nullptr);
    EXPECT_EQ(result.query->let_nodes[0].expression->getType(), ASTNodeType::FunctionCall);
}

TEST(AQLParserTest, ContentChunksFunction) {
    AQLParser parser;
    auto result = parser.parse("FOR doc IN documents LET chunks = CONTENT_CHUNKS(doc._key) RETURN chunks");

    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_EQ(result.query->let_nodes.size(), 1);
    EXPECT_EQ(result.query->let_nodes[0].variable, "chunks");
    ASSERT_NE(result.query->let_nodes[0].expression, nullptr);
    EXPECT_EQ(result.query->let_nodes[0].expression->getType(), ASTNodeType::FunctionCall);
}

TEST(AQLParserTest, ContentFunctionsInReturn) {
    AQLParser parser;
    auto result = parser.parse("FOR doc IN documents RETURN {meta: CONTENT_META(doc._key), chunks: CONTENT_CHUNKS(doc._key)}");

    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query->return_node, nullptr);
    EXPECT_EQ(result.query->return_node->expression->getType(), ASTNodeType::ObjectConstruct);
}

TEST(AQLParserTest, ModulusOperator) {
    AQLParser parser;
    auto result = parser.parse("FOR doc IN numbers FILTER doc.value % 2 == 0 RETURN doc");

    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_EQ(result.query->filters.size(), 1);
    // Verify the filter contains a modulus operation
    auto cond = result.query->filters[0]->condition;
    ASSERT_EQ(cond->getType(), ASTNodeType::BinaryOp);
}

TEST(AQLParserTest, ModulusInReturn) {
    AQLParser parser;
    auto result = parser.parse("FOR doc IN numbers RETURN {value: doc.num, remainder: doc.num % 10}");

    ASSERT_TRUE(result.success) << result.error.toString();
    ASSERT_NE(result.query->return_node, nullptr);
    EXPECT_EQ(result.query->return_node->expression->getType(), ASTNodeType::ObjectConstruct);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
