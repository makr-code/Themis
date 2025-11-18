#include <gtest/gtest.h>
#include "query/let_evaluator.h"
#include "query/aql_parser.h"
#include <nlohmann/json.hpp>

using namespace themis;
using namespace themis::query;
using json = nlohmann::json;

class LetSTFunctionsTest : public ::testing::Test {
protected:
    LetEvaluator evaluator;

    void SetUp() override {
        evaluator.clear();
    }

    json callFunction(const std::string& funcName, const std::vector<json>& args) {
        LetNode letNode;
        letNode.variable = "result";
        
        auto funcCall = std::make_shared<Expression::FunctionCallExpression>();
        funcCall->functionName = funcName;
        
        for (const auto& arg : args) {
            funcCall->arguments.push_back(
                std::make_shared<Expression::LiteralExpression>(arg)
            );
        }
        
        letNode.expression = funcCall;
        
        json emptyDoc = json::object();
        if (!evaluator.evaluateLet(letNode, emptyDoc)) {
            return json::object({{"error", "evaluation_failed"}});
        }
        
        auto result = evaluator.resolveVariable("result");
        return result.has_value() ? result.value() : json();
    }
};

// LET with ST_Point
TEST_F(LetSTFunctionsTest, LET_ST_Point_Creates2DPoint) {
    json result = callFunction("ST_Point", {13.405, 52.52});
    
    ASSERT_TRUE(result.is_object());
    EXPECT_EQ(result["type"], "Point");
    EXPECT_DOUBLE_EQ(result["coordinates"][0], 13.405);
    EXPECT_DOUBLE_EQ(result["coordinates"][1], 52.52);
}

// LET with ST_Buffer
TEST_F(LetSTFunctionsTest, LET_ST_Buffer_Point) {
    json point = callFunction("ST_Point", {1.0, 2.0});
    
    // Manually create FunctionCallExpression for nested call
    LetNode letNode;
    letNode.variable = "buffered";
    
    auto funcCall = std::make_shared<Expression::FunctionCallExpression>();
    funcCall->functionName = "ST_Buffer";
    funcCall->arguments.push_back(
        std::make_shared<Expression::LiteralExpression>(point)
    );
    funcCall->arguments.push_back(
        std::make_shared<Expression::LiteralExpression>(0.5)
    );
    
    letNode.expression = funcCall;
    
    json emptyDoc = json::object();
    ASSERT_TRUE(evaluator.evaluateLet(letNode, emptyDoc));
    
    auto result = evaluator.resolveVariable("buffered");
    ASSERT_TRUE(result.has_value());
    
    ASSERT_TRUE(result->is_object());
    EXPECT_EQ((*result)["type"], "Polygon");
    ASSERT_TRUE((*result)["coordinates"].is_array());
    ASSERT_EQ((*result)["coordinates"][0].size(), 5u);
}

// LET with ST_Distance calculation
TEST_F(LetSTFunctionsTest, LET_ST_Distance_BetweenPoints) {
    json p1 = callFunction("ST_Point", {0.0, 0.0});
    json p2 = callFunction("ST_Point", {3.0, 4.0});
    
    LetNode letNode;
    letNode.variable = "distance";
    
    auto funcCall = std::make_shared<Expression::FunctionCallExpression>();
    funcCall->functionName = "ST_Distance";
    funcCall->arguments.push_back(
        std::make_shared<Expression::LiteralExpression>(p1)
    );
    funcCall->arguments.push_back(
        std::make_shared<Expression::LiteralExpression>(p2)
    );
    
    letNode.expression = funcCall;
    
    json emptyDoc = json::object();
    ASSERT_TRUE(evaluator.evaluateLet(letNode, emptyDoc));
    
    auto result = evaluator.resolveVariable("distance");
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->is_number());
    EXPECT_DOUBLE_EQ(result->get<double>(), 5.0); // 3-4-5 triangle
}

// LET with ST_AsText conversion
TEST_F(LetSTFunctionsTest, LET_ST_AsText_WKT_Output) {
    json point = callFunction("ST_Point", {13.405, 52.52});
    
    LetNode letNode;
    letNode.variable = "wkt";
    
    auto funcCall = std::make_shared<Expression::FunctionCallExpression>();
    funcCall->functionName = "ST_AsText";
    funcCall->arguments.push_back(
        std::make_shared<Expression::LiteralExpression>(point)
    );
    
    letNode.expression = funcCall;
    
    json emptyDoc = json::object();
    ASSERT_TRUE(evaluator.evaluateLet(letNode, emptyDoc));
    
    auto result = evaluator.resolveVariable("wkt");
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->is_string());
    EXPECT_EQ(result->get<std::string>(), "POINT(13.405 52.52)");
}

// LET with ST_DWithin predicate
TEST_F(LetSTFunctionsTest, LET_ST_DWithin_Proximity) {
    json center = callFunction("ST_Point", {0.0, 0.0});
    json nearby = callFunction("ST_Point", {0.5, 0.5});
    
    LetNode letNode;
    letNode.variable = "is_within";
    
    auto funcCall = std::make_shared<Expression::FunctionCallExpression>();
    funcCall->functionName = "ST_DWithin";
    funcCall->arguments.push_back(
        std::make_shared<Expression::LiteralExpression>(center)
    );
    funcCall->arguments.push_back(
        std::make_shared<Expression::LiteralExpression>(nearby)
    );
    funcCall->arguments.push_back(
        std::make_shared<Expression::LiteralExpression>(1.0)
    );
    
    letNode.expression = funcCall;
    
    json emptyDoc = json::object();
    ASSERT_TRUE(evaluator.evaluateLet(letNode, emptyDoc));
    
    auto result = evaluator.resolveVariable("is_within");
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->is_boolean());
    EXPECT_TRUE(result->get<bool>());
}

// LET with ST_Union combining geometries
TEST_F(LetSTFunctionsTest, LET_ST_Union_MBR) {
    json p1 = callFunction("ST_Point", {0.0, 0.0});
    json p2 = callFunction("ST_Point", {2.0, 2.0});
    
    LetNode letNode;
    letNode.variable = "union_result";
    
    auto funcCall = std::make_shared<Expression::FunctionCallExpression>();
    funcCall->functionName = "ST_Union";
    funcCall->arguments.push_back(
        std::make_shared<Expression::LiteralExpression>(p1)
    );
    funcCall->arguments.push_back(
        std::make_shared<Expression::LiteralExpression>(p2)
    );
    
    letNode.expression = funcCall;
    
    json emptyDoc = json::object();
    ASSERT_TRUE(evaluator.evaluateLet(letNode, emptyDoc));
    
    auto result = evaluator.resolveVariable("union_result");
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->is_object());
    EXPECT_EQ((*result)["type"], "Polygon");
}
