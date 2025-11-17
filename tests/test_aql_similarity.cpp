// AQL SIMILARITY (Vector+Geo Syntax Sugar) Tests

#include <gtest/gtest.h>
#include "query/aql_parser.h"
#include "query/aql_translator.h"

using namespace themis;
using namespace themis::query;

class AQLSimilarityTest : public ::testing::Test {
protected:
    AQLParser parser; // stateless
};

TEST_F(AQLSimilarityTest, TranslateSimilarityBasicWithLimit) {
    std::string aql = R"(
        FOR doc IN hotels
        SORT SIMILARITY(doc.embedding, [0.1, 0.2, 0.3]) DESC
        LIMIT 5
        RETURN doc
    )";
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success) << parseResult.error.toString();
    // Prüfe spezialisierte AST Node
    ASSERT_TRUE(parseResult.query->sort);
    ASSERT_EQ(parseResult.query->sort->specifications.size(), 1u);
    auto expr = parseResult.query->sort->specifications[0].expression;
    ASSERT_EQ(expr->getType(), ASTNodeType::SimilarityCall) << "Expected SimilarityCallExpr AST node";
    auto tr = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(tr.success) << tr.error_message;
    ASSERT_TRUE(tr.vector_geo.has_value()) << "Expected VectorGeo translation";
    const auto &vq = *tr.vector_geo;
    EXPECT_EQ(vq.table, "hotels");
    EXPECT_EQ(vq.vector_field, "embedding");
    EXPECT_EQ(vq.query_vector.size(), 3u);
    EXPECT_EQ(vq.k, 5u); // LIMIT überschreibt default k
    EXPECT_FALSE(vq.spatial_filter) << "No spatial filter expected";
}

TEST_F(AQLSimilarityTest, TranslateSimilarityExplicitKIgnoresLimit) {
    std::string aql = R"(
        FOR doc IN hotels
        SORT SIMILARITY(doc.embedding, [1,2], 7) DESC
        LIMIT 3
        RETURN doc
    )";
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    auto tr = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(tr.success);
    ASSERT_TRUE(tr.vector_geo.has_value());
    const auto &vq = *tr.vector_geo;
    EXPECT_EQ(vq.k, 7u) << "Explicit k must not be overridden by LIMIT";
}

TEST_F(AQLSimilarityTest, TranslateSimilarityWithSpatialFilter) {
    std::string aql = R"(
        FOR doc IN hotels
        FILTER ST_Within(doc.location, [13.4,52.5,13.5,52.6])
        FILTER doc.city == "Berlin"
        SORT SIMILARITY(doc.embedding, [0.9,0.8,0.1]) DESC
        LIMIT 10
        RETURN doc
    )";
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    ASSERT_TRUE(parseResult.query->sort);
    auto expr = parseResult.query->sort->specifications[0].expression;
    ASSERT_EQ(expr->getType(), ASTNodeType::SimilarityCall);
    auto tr = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(tr.success) << tr.error_message;
    ASSERT_TRUE(tr.vector_geo.has_value());
    const auto &vq = *tr.vector_geo;
    EXPECT_TRUE(vq.spatial_filter) << "Spatial filter should be attached";
    EXPECT_EQ(vq.extra_filters.size(), 1u) << "One extra predicate expected";
}

TEST_F(AQLSimilarityTest, TranslateSimilarityRejectsExtraNonSpatialFilter) {
    std::string aql = R"(
        FOR doc IN hotels
        FILTER doc.city == "Berlin"
        SORT SIMILARITY(doc.embedding, [0.1,0.2]) DESC
        RETURN doc
    )";
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    ASSERT_TRUE(parseResult.query->sort);
    auto expr = parseResult.query->sort->specifications[0].expression;
    ASSERT_EQ(expr->getType(), ASTNodeType::SimilarityCall);
    auto tr = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(tr.success) << tr.error_message;
    ASSERT_TRUE(tr.vector_geo.has_value());
    EXPECT_EQ(tr.vector_geo->extra_filters.size(), 1u);
}

TEST_F(AQLSimilarityTest, TranslateSimilarityErrorWrongArgCount) {
    std::string aql = R"(
        FOR doc IN hotels
        SORT SIMILARITY(doc.embedding) DESC
        RETURN doc
    )";
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    auto tr = AQLTranslator::translate(parseResult.query);
    EXPECT_FALSE(tr.success);
    EXPECT_NE(tr.error_message.find("requires 2-3 arguments"), std::string::npos);
}

TEST_F(AQLSimilarityTest, TranslateSimilarityErrorNonArrayVector) {
    std::string aql = R"(
        FOR doc IN hotels
        SORT SIMILARITY(doc.embedding, 42) DESC
        RETURN doc
    )";
    auto parseResult = parser.parse(aql);
    ASSERT_TRUE(parseResult.success);
    auto tr = AQLTranslator::translate(parseResult.query);
    EXPECT_FALSE(tr.success);
    EXPECT_NE(tr.error_message.find("array literal"), std::string::npos);
}
