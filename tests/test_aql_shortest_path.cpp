// Tests for SHORTEST_PATH TO syntax sugar

#include <gtest/gtest.h>
#include "query/aql_parser.h"
#include "query/aql_translator.h"

using namespace themis; using namespace themis::query;

TEST(AQLShortestPathTest, ParseAndTranslateShortestPath) {
    std::string aql = R"(
        FOR v IN 1..5 OUTBOUND "city:berlin" GRAPH "cities"
        SHORTEST_PATH TO "city:dresden"
        RETURN v
    )";
    AQLParser parser; auto pr = parser.parse(aql); ASSERT_TRUE(pr.success) << pr.error.toString();
    ASSERT_TRUE(pr.query->traversal); ASSERT_TRUE(pr.query->traversal->shortestPath);
    EXPECT_EQ(pr.query->traversal->shortestPathTarget, "city:dresden");
    auto tr = AQLTranslator::translate(pr.query); ASSERT_TRUE(tr.success);
    ASSERT_TRUE(tr.traversal.has_value());
    EXPECT_TRUE(tr.traversal->shortestPath);
    EXPECT_EQ(tr.traversal->endVertex, "city:dresden");
}
