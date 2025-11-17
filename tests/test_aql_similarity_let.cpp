#include <gtest/gtest.h>
#include "query/aql_parser.h"
#include "query/aql_translator.h"

using namespace themis; using namespace themis::query;

TEST(AQLSimilarityLetTest, TranslateLetSimilaritySortVariable) {
    std::string aql = R"(
        FOR doc IN hotels
        LET sim = SIMILARITY(doc.embedding, [0.1,0.2,0.3], 5)
        SORT sim DESC
        RETURN doc
    )";
    AQLParser p; auto pr = p.parse(aql); ASSERT_TRUE(pr.success) << pr.error.toString();
    auto tr = AQLTranslator::translate(pr.query); ASSERT_TRUE(tr.success) << tr.error_message;
    ASSERT_TRUE(tr.vector_geo.has_value());
    EXPECT_EQ(tr.vector_geo->k, 5u);
    EXPECT_FALSE(tr.vector_geo->spatial_filter);
}
