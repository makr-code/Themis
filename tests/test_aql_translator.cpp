#include <gtest/gtest.h>
#include "query/aql_parser.h"
#include "query/aql_translator.h"

using namespace themis;
using namespace themis::query;

// ============================================================================
// Basic Translation Tests
// ============================================================================

TEST(AQLTranslatorTest, SimpleEquality) {
    AQLParser parser;
    auto parseResult = parser.parse("FOR user IN users FILTER user.age == 25 RETURN user");
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success) << translateResult.error_message;
    
    EXPECT_EQ(translateResult.query.table, "users");
    ASSERT_EQ(translateResult.query.predicates.size(), 1);
    EXPECT_EQ(translateResult.query.predicates[0].column, "age");
    EXPECT_EQ(translateResult.query.predicates[0].value, "25");
}

TEST(AQLTranslatorTest, MultipleEqualityPredicates) {
    AQLParser parser;
    auto parseResult = parser.parse(
        "FOR user IN users "
        "FILTER user.age == 25 AND user.city == \"Berlin\" "
        "RETURN user"
    );
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success) << translateResult.error_message;
    
    EXPECT_EQ(translateResult.query.table, "users");
    ASSERT_EQ(translateResult.query.predicates.size(), 2);
    
    // Order may vary due to AST traversal
    bool hasAge = false, hasCity = false;
    for (const auto& pred : translateResult.query.predicates) {
        if (pred.column == "age" && pred.value == "25") hasAge = true;
        if (pred.column == "city" && pred.value == "Berlin") hasCity = true;
    }
    EXPECT_TRUE(hasAge);
    EXPECT_TRUE(hasCity);
}

TEST(AQLTranslatorTest, RangePredicateGreaterThan) {
    AQLParser parser;
    auto parseResult = parser.parse("FOR user IN users FILTER user.age > 18 RETURN user");
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success) << translateResult.error_message;
    
    ASSERT_EQ(translateResult.query.rangePredicates.size(), 1);
    const auto& range = translateResult.query.rangePredicates[0];
    EXPECT_EQ(range.column, "age");
    EXPECT_EQ(range.lower, "18");
    EXPECT_FALSE(range.includeLower); // > not >=
    EXPECT_EQ(range.upper, std::nullopt);
}

TEST(AQLTranslatorTest, RangePredicateGreaterThanOrEqual) {
    AQLParser parser;
    auto parseResult = parser.parse("FOR user IN users FILTER user.age >= 18 RETURN user");
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success) << translateResult.error_message;
    
    ASSERT_EQ(translateResult.query.rangePredicates.size(), 1);
    const auto& range = translateResult.query.rangePredicates[0];
    EXPECT_EQ(range.column, "age");
    EXPECT_EQ(range.lower, "18");
    EXPECT_TRUE(range.includeLower); // >=
    EXPECT_EQ(range.upper, std::nullopt);
}

TEST(AQLTranslatorTest, RangePredicateLessThan) {
    AQLParser parser;
    auto parseResult = parser.parse("FOR user IN users FILTER user.age < 65 RETURN user");
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success) << translateResult.error_message;
    
    ASSERT_EQ(translateResult.query.rangePredicates.size(), 1);
    const auto& range = translateResult.query.rangePredicates[0];
    EXPECT_EQ(range.column, "age");
    EXPECT_EQ(range.lower, std::nullopt);
    EXPECT_EQ(range.upper, "65");
    EXPECT_FALSE(range.includeUpper); // < not <=
}

TEST(AQLTranslatorTest, RangePredicateLessThanOrEqual) {
    AQLParser parser;
    auto parseResult = parser.parse("FOR user IN users FILTER user.age <= 65 RETURN user");
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success) << translateResult.error_message;
    
    ASSERT_EQ(translateResult.query.rangePredicates.size(), 1);
    const auto& range = translateResult.query.rangePredicates[0];
    EXPECT_EQ(range.column, "age");
    EXPECT_EQ(range.lower, std::nullopt);
    EXPECT_EQ(range.upper, "65");
    EXPECT_TRUE(range.includeUpper); // <=
}

TEST(AQLTranslatorTest, MixedEqualityAndRange) {
    AQLParser parser;
    auto parseResult = parser.parse(
        "FOR user IN users "
        "FILTER user.age > 18 AND user.city == \"Berlin\" "
        "RETURN user"
    );
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success) << translateResult.error_message;
    
    ASSERT_EQ(translateResult.query.predicates.size(), 1);
    EXPECT_EQ(translateResult.query.predicates[0].column, "city");
    EXPECT_EQ(translateResult.query.predicates[0].value, "Berlin");
    
    ASSERT_EQ(translateResult.query.rangePredicates.size(), 1);
    EXPECT_EQ(translateResult.query.rangePredicates[0].column, "age");
    EXPECT_EQ(translateResult.query.rangePredicates[0].lower, "18");
    EXPECT_FALSE(translateResult.query.rangePredicates[0].includeLower);
}

// ============================================================================
// ORDER BY Tests
// ============================================================================

TEST(AQLTranslatorTest, SortAscending) {
    AQLParser parser;
    auto parseResult = parser.parse(
        "FOR user IN users "
        "SORT user.created_at ASC "
        "RETURN user"
    );
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success) << translateResult.error_message;
    
    ASSERT_TRUE(translateResult.query.orderBy.has_value());
    EXPECT_EQ(translateResult.query.orderBy->column, "created_at");
    EXPECT_FALSE(translateResult.query.orderBy->desc); // ASC
    EXPECT_EQ(translateResult.query.orderBy->limit, 1000); // default
}

TEST(AQLTranslatorTest, SortDescending) {
    AQLParser parser;
    auto parseResult = parser.parse(
        "FOR user IN users "
        "SORT user.created_at DESC "
        "RETURN user"
    );
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success) << translateResult.error_message;
    
    ASSERT_TRUE(translateResult.query.orderBy.has_value());
    EXPECT_EQ(translateResult.query.orderBy->column, "created_at");
    EXPECT_TRUE(translateResult.query.orderBy->desc); // DESC
}

TEST(AQLTranslatorTest, SortWithLimit) {
    AQLParser parser;
    auto parseResult = parser.parse(
        "FOR user IN users "
        "SORT user.created_at DESC "
        "LIMIT 10 "
        "RETURN user"
    );
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success) << translateResult.error_message;
    
    ASSERT_TRUE(translateResult.query.orderBy.has_value());
    EXPECT_EQ(translateResult.query.orderBy->column, "created_at");
    EXPECT_TRUE(translateResult.query.orderBy->desc);
    EXPECT_EQ(translateResult.query.orderBy->limit, 10); // from LIMIT clause
}

TEST(AQLTranslatorTest, SortWithLimitOffset) {
    AQLParser parser;
    auto parseResult = parser.parse(
        "FOR user IN users "
        "SORT user.created_at ASC "
        "LIMIT 5, 10 "
        "RETURN user"
    );
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success) << translateResult.error_message;
    
    ASSERT_TRUE(translateResult.query.orderBy.has_value());
    EXPECT_EQ(translateResult.query.orderBy->column, "created_at");
    EXPECT_FALSE(translateResult.query.orderBy->desc);
    // Expect limit to be offset+count to allow post-slicing
    EXPECT_EQ(translateResult.query.orderBy->limit, 15);
}

// ============================================================================
// Complete Query Tests
// ============================================================================

TEST(AQLTranslatorTest, CompleteQuery) {
    AQLParser parser;
    auto parseResult = parser.parse(
        "FOR user IN users "
        "FILTER user.age > 18 AND user.city == \"Berlin\" "
        "SORT user.created_at DESC "
        "LIMIT 10 "
        "RETURN user"
    );
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success) << translateResult.error_message;
    
    // Validate table
    EXPECT_EQ(translateResult.query.table, "users");
    
    // Validate predicates
    ASSERT_EQ(translateResult.query.predicates.size(), 1);
    EXPECT_EQ(translateResult.query.predicates[0].column, "city");
    EXPECT_EQ(translateResult.query.predicates[0].value, "Berlin");
    
    // Validate range predicates
    ASSERT_EQ(translateResult.query.rangePredicates.size(), 1);
    EXPECT_EQ(translateResult.query.rangePredicates[0].column, "age");
    EXPECT_EQ(translateResult.query.rangePredicates[0].lower, "18");
    EXPECT_FALSE(translateResult.query.rangePredicates[0].includeLower);
    
    // Validate ORDER BY
    ASSERT_TRUE(translateResult.query.orderBy.has_value());
    EXPECT_EQ(translateResult.query.orderBy->column, "created_at");
    EXPECT_TRUE(translateResult.query.orderBy->desc);
    EXPECT_EQ(translateResult.query.orderBy->limit, 10);
}

// ============================================================================
// Nested Field Access Tests
// ============================================================================

TEST(AQLTranslatorTest, NestedFieldAccess) {
    AQLParser parser;
    auto parseResult = parser.parse(
        "FOR doc IN users "
        "FILTER doc.address.city == \"Berlin\" "
        "RETURN doc"
    );
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success) << translateResult.error_message;
    
    ASSERT_EQ(translateResult.query.predicates.size(), 1);
    EXPECT_EQ(translateResult.query.predicates[0].column, "address.city");
    EXPECT_EQ(translateResult.query.predicates[0].value, "Berlin");
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST(AQLTranslatorTest, OrOperatorNowSupported) {
    // OR is now supported via DisjunctiveQuery and DNF conversion
    AQLParser parser;
    auto parseResult = parser.parse(
        "FOR user IN users "
        "FILTER user.age > 18 OR user.city == \"Berlin\" "
        "RETURN user"
    );
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    EXPECT_TRUE(translateResult.success) << translateResult.error_message;
    EXPECT_TRUE(translateResult.disjunctive.has_value());
    EXPECT_EQ(translateResult.disjunctive->disjuncts.size(), 2u);
}

TEST(AQLTranslatorTest, NullASTError) {
    auto translateResult = AQLTranslator::translate(nullptr);
    EXPECT_FALSE(translateResult.success);
    EXPECT_NE(translateResult.error_message.find("Null"), std::string::npos);
}

// ============================================================================
// String Literal Value Tests
// ============================================================================

TEST(AQLTranslatorTest, StringLiteralValue) {
    AQLParser parser;
    auto parseResult = parser.parse(
        "FOR user IN users "
        "FILTER user.name == \"John Doe\" "
        "RETURN user"
    );
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success) << translateResult.error_message;
    
    ASSERT_EQ(translateResult.query.predicates.size(), 1);
    EXPECT_EQ(translateResult.query.predicates[0].column, "name");
    EXPECT_EQ(translateResult.query.predicates[0].value, "John Doe");
}

TEST(AQLTranslatorTest, BooleanLiteralValue) {
    AQLParser parser;
    auto parseResult = parser.parse(
        "FOR user IN users "
        "FILTER user.active == true "
        "RETURN user"
    );
    ASSERT_TRUE(parseResult.success);
    
    auto translateResult = AQLTranslator::translate(parseResult.query);
    ASSERT_TRUE(translateResult.success) << translateResult.error_message;
    
    ASSERT_EQ(translateResult.query.predicates.size(), 1);
    EXPECT_EQ(translateResult.query.predicates[0].column, "active");
    EXPECT_EQ(translateResult.query.predicates[0].value, "true");
}
