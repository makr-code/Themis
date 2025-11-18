#include <gtest/gtest.h>
#include "query/aql_parser.h"
#include "query/aql_translator.h"

using namespace themis; using namespace themis::query;

TEST(AQLProximityLetTest, TranslateLetProximitySortVariable) {
    std::string aql = R"(
        FOR doc IN places
        FILTER FULLTEXT(doc.description, "coffee", 20)
        LET prox = PROXIMITY(doc.location, [13.4,52.5])
        SORT prox ASC
        LIMIT 10
        RETURN doc
    )";
    AQLParser p; auto pr = p.parse(aql); ASSERT_TRUE(pr.success) << pr.error.toString();
    auto tr = AQLTranslator::translate(pr.query); ASSERT_TRUE(tr.success) << tr.error_message;
    ASSERT_TRUE(tr.content_geo.has_value());
    EXPECT_EQ(tr.content_geo->limit, 10u);
    EXPECT_TRUE(tr.content_geo->center_point.has_value());
}
