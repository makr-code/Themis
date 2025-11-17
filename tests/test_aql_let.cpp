#include <gtest/gtest.h>
#include "query/let_evaluator.h"
#include "query/aql_parser.h"
#include <nlohmann/json.hpp>

using namespace themis;
using namespace themis::query;

class LetEvaluatorTest : public ::testing::Test {
protected:
    LetEvaluator evaluator;
    nlohmann::json testDoc;

    void SetUp() override {
        evaluator.clear();
        testDoc = {
            {"name", "Alice"},
            {"age", 30},
            {"city", "Berlin"},
            {"salary", 50000.0},
            {"address", {
                {"street", "Main St"},
                {"number", 42},
                {"zip", "10115"}
            }},
            {"tags", nlohmann::json::array({"developer", "senior", "backend"})}
        };
    }
};

// ============================================================================
// Basic LET Evaluation Tests
// ============================================================================

TEST_F(LetEvaluatorTest, SimpleLiteralAssignment) {
    LetNode letNode;
    letNode.variable = "x";
    letNode.expression = std::make_shared<Expression::LiteralExpression>(42);

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("x");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 42);
}

TEST_F(LetEvaluatorTest, StringLiteralAssignment) {
    LetNode letNode;
    letNode.variable = "greeting";
    letNode.expression = std::make_shared<Expression::LiteralExpression>("Hello World");

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("greeting");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "Hello World");
}

TEST_F(LetEvaluatorTest, BooleanLiteralAssignment) {
    LetNode letNode;
    letNode.variable = "isActive";
    letNode.expression = std::make_shared<Expression::LiteralExpression>(true);

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("isActive");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), true);
}

TEST_F(LetEvaluatorTest, NullLiteralAssignment) {
    LetNode letNode;
    letNode.variable = "nullValue";
    letNode.expression = std::make_shared<Expression::LiteralExpression>(nullptr);

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("nullValue");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result.value().is_null());
}

// ============================================================================
// Field Access Tests
// ============================================================================

TEST_F(LetEvaluatorTest, SimpleFieldAccess) {
    LetNode letNode;
    letNode.variable = "personAge";
    auto fieldAccess = std::make_shared<Expression::FieldAccessExpression>();
    fieldAccess->path = {"doc", "age"};
    letNode.expression = fieldAccess;

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("personAge");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 30);
}

TEST_F(LetEvaluatorTest, NestedFieldAccess) {
    LetNode letNode;
    letNode.variable = "streetName";
    auto fieldAccess = std::make_shared<Expression::FieldAccessExpression>();
    fieldAccess->path = {"doc", "address", "street"};
    letNode.expression = fieldAccess;

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("streetName");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "Main St");
}

TEST_F(LetEvaluatorTest, ArrayIndexAccess) {
    LetNode letNode;
    letNode.variable = "firstTag";
    auto fieldAccess = std::make_shared<Expression::FieldAccessExpression>();
    fieldAccess->path = {"doc", "tags", "0"};
    letNode.expression = fieldAccess;

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("firstTag");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "developer");
}

TEST_F(LetEvaluatorTest, NonExistentField) {
    LetNode letNode;
    letNode.variable = "missing";
    auto fieldAccess = std::make_shared<Expression::FieldAccessExpression>();
    fieldAccess->path = {"doc", "nonexistent"};
    letNode.expression = fieldAccess;

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("missing");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result.value().is_null());
}

// ============================================================================
// Arithmetic Operations Tests
// ============================================================================

TEST_F(LetEvaluatorTest, Addition) {
    LetNode letNode;
    letNode.variable = "nextYear";
    auto binOp = std::make_shared<Expression::BinaryOpExpression>();
    binOp->op = "+";
    auto fieldAccess = std::make_shared<Expression::FieldAccessExpression>();
    fieldAccess->path = {"doc", "age"};
    binOp->left = fieldAccess;
    binOp->right = std::make_shared<Expression::LiteralExpression>(1);
    letNode.expression = binOp;

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("nextYear");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 31);
}

TEST_F(LetEvaluatorTest, Subtraction) {
    LetNode letNode;
    letNode.variable = "halfAge";
    auto binOp = std::make_shared<Expression::BinaryOpExpression>();
    binOp->op = "-";
    auto fieldAccess = std::make_shared<Expression::FieldAccessExpression>();
    fieldAccess->path = {"doc", "age"};
    binOp->left = fieldAccess;
    binOp->right = std::make_shared<Expression::LiteralExpression>(15);
    letNode.expression = binOp;

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("halfAge");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 15);
}

TEST_F(LetEvaluatorTest, Multiplication) {
    LetNode letNode;
    letNode.variable = "doubleAge";
    auto binOp = std::make_shared<Expression::BinaryOpExpression>();
    binOp->op = "*";
    auto fieldAccess = std::make_shared<Expression::FieldAccessExpression>();
    fieldAccess->path = {"doc", "age"};
    binOp->left = fieldAccess;
    binOp->right = std::make_shared<Expression::LiteralExpression>(2);
    letNode.expression = binOp;

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("doubleAge");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 60);
}

TEST_F(LetEvaluatorTest, Division) {
    LetNode letNode;
    letNode.variable = "halfSalary";
    auto binOp = std::make_shared<Expression::BinaryOpExpression>();
    binOp->op = "/";
    auto fieldAccess = std::make_shared<Expression::FieldAccessExpression>();
    fieldAccess->path = {"doc", "salary"};
    binOp->left = fieldAccess;
    binOp->right = std::make_shared<Expression::LiteralExpression>(2.0);
    letNode.expression = binOp;

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("halfSalary");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 25000.0);
}

TEST_F(LetEvaluatorTest, Modulo) {
    LetNode letNode;
    letNode.variable = "remainder";
    auto binOp = std::make_shared<Expression::BinaryOpExpression>();
    binOp->op = "%";
    auto fieldAccess = std::make_shared<Expression::FieldAccessExpression>();
    fieldAccess->path = {"doc", "age"};
    binOp->left = fieldAccess;
    binOp->right = std::make_shared<Expression::LiteralExpression>(7);
    letNode.expression = binOp;

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("remainder");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 2); // 30 % 7 = 2
}

TEST_F(LetEvaluatorTest, ComplexArithmetic) {
    // LET x = (doc.age + 10) * 2
    LetNode letNode;
    letNode.variable = "x";
    
    auto mult = std::make_shared<Expression::BinaryOpExpression>();
    mult->op = "*";
    
    auto add = std::make_shared<Expression::BinaryOpExpression>();
    add->op = "+";
    auto fieldAccess = std::make_shared<Expression::FieldAccessExpression>();
    fieldAccess->path = {"doc", "age"};
    add->left = fieldAccess;
    add->right = std::make_shared<Expression::LiteralExpression>(10);
    
    mult->left = add;
    mult->right = std::make_shared<Expression::LiteralExpression>(2);
    letNode.expression = mult;

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("x");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 80); // (30 + 10) * 2 = 80
}

// ============================================================================
// String Operations Tests
// ============================================================================

TEST_F(LetEvaluatorTest, StringConcatenation) {
    LetNode letNode;
    letNode.variable = "greeting";
    auto binOp = std::make_shared<Expression::BinaryOpExpression>();
    binOp->op = "+";
    binOp->left = std::make_shared<Expression::LiteralExpression>("Hello, ");
    auto fieldAccess = std::make_shared<Expression::FieldAccessExpression>();
    fieldAccess->path = {"doc", "name"};
    binOp->right = fieldAccess;
    letNode.expression = binOp;

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("greeting");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "Hello, Alice");
}

// ============================================================================
// Function Call Tests
// ============================================================================

TEST_F(LetEvaluatorTest, LengthFunction) {
    LetNode letNode;
    letNode.variable = "nameLength";
    auto funcCall = std::make_shared<Expression::FunctionCallExpression>();
    funcCall->functionName = "LENGTH";
    auto fieldAccess = std::make_shared<Expression::FieldAccessExpression>();
    fieldAccess->path = {"doc", "name"};
    funcCall->arguments.push_back(fieldAccess);
    letNode.expression = funcCall;

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("nameLength");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 5); // "Alice" has 5 characters
}

TEST_F(LetEvaluatorTest, ConcatFunction) {
    LetNode letNode;
    letNode.variable = "fullName";
    auto funcCall = std::make_shared<Expression::FunctionCallExpression>();
    funcCall->functionName = "CONCAT";
    auto fieldAccess = std::make_shared<Expression::FieldAccessExpression>();
    fieldAccess->path = {"doc", "name"};
    funcCall->arguments.push_back(fieldAccess);
    funcCall->arguments.push_back(std::make_shared<Expression::LiteralExpression>(" from "));
    auto cityAccess = std::make_shared<Expression::FieldAccessExpression>();
    cityAccess->path = {"doc", "city"};
    funcCall->arguments.push_back(cityAccess);
    letNode.expression = funcCall;

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("fullName");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "Alice from Berlin");
}

TEST_F(LetEvaluatorTest, UpperFunction) {
    LetNode letNode;
    letNode.variable = "upperCity";
    auto funcCall = std::make_shared<Expression::FunctionCallExpression>();
    funcCall->functionName = "UPPER";
    auto fieldAccess = std::make_shared<Expression::FieldAccessExpression>();
    fieldAccess->path = {"doc", "city"};
    funcCall->arguments.push_back(fieldAccess);
    letNode.expression = funcCall;

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("upperCity");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "BERLIN");
}

TEST_F(LetEvaluatorTest, LowerFunction) {
    LetNode letNode;
    letNode.variable = "lowerName";
    auto funcCall = std::make_shared<Expression::FunctionCallExpression>();
    funcCall->functionName = "LOWER";
    auto fieldAccess = std::make_shared<Expression::FieldAccessExpression>();
    fieldAccess->path = {"doc", "name"};
    funcCall->arguments.push_back(fieldAccess);
    letNode.expression = funcCall;

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("lowerName");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "alice");
}

TEST_F(LetEvaluatorTest, SubstringFunction) {
    LetNode letNode;
    letNode.variable = "substring";
    auto funcCall = std::make_shared<Expression::FunctionCallExpression>();
    funcCall->functionName = "SUBSTRING";
    auto fieldAccess = std::make_shared<Expression::FieldAccessExpression>();
    fieldAccess->path = {"doc", "name"};
    funcCall->arguments.push_back(fieldAccess);
    funcCall->arguments.push_back(std::make_shared<Expression::LiteralExpression>(0));
    funcCall->arguments.push_back(std::make_shared<Expression::LiteralExpression>(2));
    letNode.expression = funcCall;

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("substring");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "Al");
}

// ============================================================================
// Math Function Tests
// ============================================================================

TEST_F(LetEvaluatorTest, AbsFunction) {
    LetNode letNode;
    letNode.variable = "absValue";
    auto funcCall = std::make_shared<Expression::FunctionCallExpression>();
    funcCall->functionName = "ABS";
    funcCall->arguments.push_back(std::make_shared<Expression::LiteralExpression>(-42));
    letNode.expression = funcCall;

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("absValue");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 42);
}

TEST_F(LetEvaluatorTest, MinFunction) {
    LetNode letNode;
    letNode.variable = "minimum";
    auto funcCall = std::make_shared<Expression::FunctionCallExpression>();
    funcCall->functionName = "MIN";
    funcCall->arguments.push_back(std::make_shared<Expression::LiteralExpression>(10));
    funcCall->arguments.push_back(std::make_shared<Expression::LiteralExpression>(5));
    funcCall->arguments.push_back(std::make_shared<Expression::LiteralExpression>(20));
    letNode.expression = funcCall;

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("minimum");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 5);
}

TEST_F(LetEvaluatorTest, MaxFunction) {
    LetNode letNode;
    letNode.variable = "maximum";
    auto funcCall = std::make_shared<Expression::FunctionCallExpression>();
    funcCall->functionName = "MAX";
    funcCall->arguments.push_back(std::make_shared<Expression::LiteralExpression>(10));
    funcCall->arguments.push_back(std::make_shared<Expression::LiteralExpression>(5));
    funcCall->arguments.push_back(std::make_shared<Expression::LiteralExpression>(20));
    letNode.expression = funcCall;

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("maximum");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 20);
}

// ============================================================================
// Chained LET Tests (LET referencing previous LET)
// ============================================================================

TEST_F(LetEvaluatorTest, ChainedLets) {
    // LET x = doc.age
    LetNode let1;
    let1.variable = "x";
    auto fieldAccess1 = std::make_shared<Expression::FieldAccessExpression>();
    fieldAccess1->path = {"doc", "age"};
    let1.expression = fieldAccess1;
    
    ASSERT_TRUE(evaluator.evaluateLet(let1, testDoc));
    
    // LET y = x * 2
    LetNode let2;
    let2.variable = "y";
    auto binOp = std::make_shared<Expression::BinaryOpExpression>();
    binOp->op = "*";
    auto xAccess = std::make_shared<Expression::FieldAccessExpression>();
    xAccess->path = {"x"};
    binOp->left = xAccess;
    binOp->right = std::make_shared<Expression::LiteralExpression>(2);
    let2.expression = binOp;
    
    ASSERT_TRUE(evaluator.evaluateLet(let2, testDoc));
    
    auto resultX = evaluator.resolveVariable("x");
    ASSERT_TRUE(resultX.has_value());
    EXPECT_EQ(resultX.value(), 30);
    
    auto resultY = evaluator.resolveVariable("y");
    ASSERT_TRUE(resultY.has_value());
    EXPECT_EQ(resultY.value(), 60);
}

TEST_F(LetEvaluatorTest, TripleChainedLets) {
    // LET x = doc.age
    LetNode let1;
    let1.variable = "x";
    auto fieldAccess = std::make_shared<Expression::FieldAccessExpression>();
    fieldAccess->path = {"doc", "age"};
    let1.expression = fieldAccess;
    ASSERT_TRUE(evaluator.evaluateLet(let1, testDoc));
    
    // LET y = x + 10
    LetNode let2;
    let2.variable = "y";
    auto add = std::make_shared<Expression::BinaryOpExpression>();
    add->op = "+";
    auto xAccess = std::make_shared<Expression::FieldAccessExpression>();
    xAccess->path = {"x"};
    add->left = xAccess;
    add->right = std::make_shared<Expression::LiteralExpression>(10);
    let2.expression = add;
    ASSERT_TRUE(evaluator.evaluateLet(let2, testDoc));
    
    // LET z = y * 2
    LetNode let3;
    let3.variable = "z";
    auto mult = std::make_shared<Expression::BinaryOpExpression>();
    mult->op = "*";
    auto yAccess = std::make_shared<Expression::FieldAccessExpression>();
    yAccess->path = {"y"};
    mult->left = yAccess;
    mult->right = std::make_shared<Expression::LiteralExpression>(2);
    let3.expression = mult;
    ASSERT_TRUE(evaluator.evaluateLet(let3, testDoc));
    
    auto resultZ = evaluator.resolveVariable("z");
    ASSERT_TRUE(resultZ.has_value());
    EXPECT_EQ(resultZ.value(), 80); // (30 + 10) * 2 = 80
}

// ============================================================================
// Comparison Operations Tests
// ============================================================================

TEST_F(LetEvaluatorTest, EqualityComparison) {
    LetNode letNode;
    letNode.variable = "isThirty";
    auto binOp = std::make_shared<Expression::BinaryOpExpression>();
    binOp->op = "==";
    auto fieldAccess = std::make_shared<Expression::FieldAccessExpression>();
    fieldAccess->path = {"doc", "age"};
    binOp->left = fieldAccess;
    binOp->right = std::make_shared<Expression::LiteralExpression>(30);
    letNode.expression = binOp;

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("isThirty");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), true);
}

TEST_F(LetEvaluatorTest, LessThanComparison) {
    LetNode letNode;
    letNode.variable = "isYoung";
    auto binOp = std::make_shared<Expression::BinaryOpExpression>();
    binOp->op = "<";
    auto fieldAccess = std::make_shared<Expression::FieldAccessExpression>();
    fieldAccess->path = {"doc", "age"};
    binOp->left = fieldAccess;
    binOp->right = std::make_shared<Expression::LiteralExpression>(40);
    letNode.expression = binOp;

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("isYoung");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), true);
}

// ============================================================================
// Logical Operations Tests
// ============================================================================

TEST_F(LetEvaluatorTest, AndOperation) {
    LetNode letNode;
    letNode.variable = "condition";
    auto binOp = std::make_shared<Expression::BinaryOpExpression>();
    binOp->op = "AND";
    binOp->left = std::make_shared<Expression::LiteralExpression>(true);
    binOp->right = std::make_shared<Expression::LiteralExpression>(false);
    letNode.expression = binOp;

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("condition");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), false);
}

TEST_F(LetEvaluatorTest, OrOperation) {
    LetNode letNode;
    letNode.variable = "condition";
    auto binOp = std::make_shared<Expression::BinaryOpExpression>();
    binOp->op = "OR";
    binOp->left = std::make_shared<Expression::LiteralExpression>(true);
    binOp->right = std::make_shared<Expression::LiteralExpression>(false);
    letNode.expression = binOp;

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("condition");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), true);
}

TEST_F(LetEvaluatorTest, NotOperation) {
    LetNode letNode;
    letNode.variable = "negated";
    auto unaryOp = std::make_shared<Expression::UnaryOpExpression>();
    unaryOp->op = "NOT";
    unaryOp->operand = std::make_shared<Expression::LiteralExpression>(true);
    letNode.expression = unaryOp;

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("negated");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), false);
}

// ============================================================================
// Edge Cases and Error Handling
// ============================================================================

TEST_F(LetEvaluatorTest, DivisionByZero) {
    LetNode letNode;
    letNode.variable = "result";
    auto binOp = std::make_shared<Expression::BinaryOpExpression>();
    binOp->op = "/";
    binOp->left = std::make_shared<Expression::LiteralExpression>(10);
    binOp->right = std::make_shared<Expression::LiteralExpression>(0);
    letNode.expression = binOp;

    // Should fail or return error
    EXPECT_FALSE(evaluator.evaluateLet(letNode, testDoc));
}

TEST_F(LetEvaluatorTest, ClearBindings) {
    LetNode letNode;
    letNode.variable = "x";
    letNode.expression = std::make_shared<Expression::LiteralExpression>(42);
    
    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    EXPECT_TRUE(evaluator.hasVariable("x"));
    
    evaluator.clear();
    EXPECT_FALSE(evaluator.hasVariable("x"));
}

TEST_F(LetEvaluatorTest, OverwriteVariable) {
    LetNode let1;
    let1.variable = "x";
    let1.expression = std::make_shared<Expression::LiteralExpression>(10);
    ASSERT_TRUE(evaluator.evaluateLet(let1, testDoc));
    
    LetNode let2;
    let2.variable = "x";
    let2.expression = std::make_shared<Expression::LiteralExpression>(20);
    ASSERT_TRUE(evaluator.evaluateLet(let2, testDoc));
    
    auto result = evaluator.resolveVariable("x");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 20); // Should be overwritten
}

TEST_F(LetEvaluatorTest, UndefinedVariableReference) {
    // Try to reference undefined variable
    LetNode letNode;
    letNode.variable = "y";
    auto fieldAccess = std::make_shared<Expression::FieldAccessExpression>();
    fieldAccess->path = {"undefinedVar"};
    letNode.expression = fieldAccess;

    ASSERT_TRUE(evaluator.evaluateLet(letNode, testDoc));
    auto result = evaluator.resolveVariable("y");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result.value().is_null()); // Should return null for undefined
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
