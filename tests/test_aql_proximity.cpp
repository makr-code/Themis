// AQL PROXIMITY (Content+Geo Syntax Sugar) Tests

#include <gtest/gtest.h>
#include "query/aql_parser.h"
#include "query/aql_translator.h"

using namespace themis;
using namespace themis::query;

class AQLProximityTest : public ::testing::Test { protected: AQLParser parser; };

TEST_F(AQLProximityTest, TranslateProximityWithFulltextAndSpatial) {
    std::string aql = R"(
        FOR doc IN places
        FILTER FULLTEXT(doc.description, "coffee", 50)
        FILTER ST_Within(doc.location, [13.4,52.5,13.5,52.6])
        SORT PROXIMITY(doc.location, [13.45,52.55]) ASC
        LIMIT 20
        RETURN doc
    )";
    auto parseResult = parser.parse(aql); ASSERT_TRUE(parseResult.success);
    ASSERT_TRUE(parseResult.query->sort);
    ASSERT_EQ(parseResult.query->sort->specifications.size(),1u);
    auto expr = parseResult.query->sort->specifications[0].expression;
    ASSERT_EQ(expr->getType(), ASTNodeType::ProximityCall) << "Expected ProximityCallExpr AST node";
    auto tr = AQLTranslator::translate(parseResult.query); ASSERT_TRUE(tr.success) << tr.error_message;
    ASSERT_TRUE(tr.content_geo.has_value());
    const auto &cq = *tr.content_geo;
    EXPECT_EQ(cq.table, "places");
    EXPECT_EQ(cq.text_field, "description");
    EXPECT_EQ(cq.fulltext_query, "coffee");
    EXPECT_TRUE(cq.spatial_filter);
    EXPECT_TRUE(cq.center_point.has_value());
    EXPECT_TRUE(cq.boost_by_distance);
    EXPECT_EQ(cq.limit, 20u);
}

TEST_F(AQLProximityTest, ProximityRequiresFulltext) {
    std::string aql = R"(
        FOR doc IN places
        SORT PROXIMITY(doc.location, [13.45,52.55]) ASC
        LIMIT 10
        RETURN doc
    )";
    auto parseResult = parser.parse(aql); ASSERT_TRUE(parseResult.success);
    ASSERT_TRUE(parseResult.query->sort);
    auto expr = parseResult.query->sort->specifications[0].expression;
    ASSERT_EQ(expr->getType(), ASTNodeType::ProximityCall);
    auto tr = AQLTranslator::translate(parseResult.query); EXPECT_FALSE(tr.success);
    EXPECT_NE(tr.error_message.find("requires a FULLTEXT"), std::string::npos);
}
