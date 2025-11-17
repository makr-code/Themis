#include <gtest/gtest.h>
#include "query/window_evaluator.h"
#include "query/aql_parser.h"
#include <nlohmann/json.hpp>

using namespace themis::query;
using json = nlohmann::json;

class WindowEvaluatorTest : public ::testing::Test {
protected:
    WindowEvaluator evaluator;
    
    // Helper: Erstellt einfache Field Access Expression
    std::shared_ptr<Expression> makeFieldAccess(const std::string& field) {
        auto var = std::make_shared<VariableExpr>("doc");
        return std::make_shared<FieldAccessExpr>(var, field);
    }
    
    // Helper: Erstellt Literal Expression
    std::shared_ptr<Expression> makeLiteral(int64_t value) {
        return std::make_shared<LiteralExpr>(value);
    }
    
    std::shared_ptr<Expression> makeLiteralString(const std::string& value) {
        return std::make_shared<LiteralExpr>(value);
    }
};

// ============================================================================
// ROW_NUMBER Tests
// ============================================================================

TEST_F(WindowEvaluatorTest, RowNumberNoPartition) {
    // FOR doc IN sales RETURN ROW_NUMBER() OVER (ORDER BY doc.amount DESC)
    
    std::vector<json> rows = {
        {{"amount", 100}, {"product", "A"}},
        {{"amount", 200}, {"product", "B"}},
        {{"amount", 150}, {"product", "C"}},
        {{"amount", 180}, {"product", "D"}}
    };
    
    WindowSpec spec;
    spec.name = "w";
    spec.partitionBy = {};  // Keine Partitionierung
    
    SortSpec sortSpec;
    sortSpec.expression = makeFieldAccess("amount");
    sortSpec.ascending = false;  // DESC
    spec.orderBy = {sortSpec};
    
    WindowFunctionCall func;
    func.funcType = WindowFunctionType::ROW_NUMBER;
    func.windowName = "w";
    
    auto results = evaluator.evaluate(rows, spec, func, "doc");
    
    ASSERT_EQ(results.size(), 4u);
    
    // Nach amount DESC sortiert: 200 (1), 180 (2), 150 (3), 100 (4)
    EXPECT_EQ(results[0].get<int64_t>(), 4);  // amount=100 → row 4
    EXPECT_EQ(results[1].get<int64_t>(), 1);  // amount=200 → row 1
    EXPECT_EQ(results[2].get<int64_t>(), 3);  // amount=150 → row 3
    EXPECT_EQ(results[3].get<int64_t>(), 2);  // amount=180 → row 2
}

TEST_F(WindowEvaluatorTest, RowNumberWithPartition) {
    // FOR doc IN sales 
    // RETURN ROW_NUMBER() OVER (PARTITION BY doc.category ORDER BY doc.amount DESC)
    
    std::vector<json> rows = {
        {{"amount", 100}, {"category", "A"}},
        {{"amount", 200}, {"category", "B"}},
        {{"amount", 150}, {"category", "A"}},
        {{"amount", 180}, {"category", "B"}},
        {{"amount", 120}, {"category", "A"}}
    };
    
    WindowSpec spec;
    spec.partitionBy = {makeFieldAccess("category")};
    
    SortSpec sortSpec;
    sortSpec.expression = makeFieldAccess("amount");
    sortSpec.ascending = false;
    spec.orderBy = {sortSpec};
    
    WindowFunctionCall func;
    func.funcType = WindowFunctionType::ROW_NUMBER;
    
    auto results = evaluator.evaluate(rows, spec, func, "doc");
    
    ASSERT_EQ(results.size(), 5u);
    
    // Partition A (amount DESC): 150 (1), 120 (2), 100 (3)
    EXPECT_EQ(results[0].get<int64_t>(), 3);  // amount=100, category=A → row 3
    EXPECT_EQ(results[2].get<int64_t>(), 1);  // amount=150, category=A → row 1
    EXPECT_EQ(results[4].get<int64_t>(), 2);  // amount=120, category=A → row 2
    
    // Partition B (amount DESC): 200 (1), 180 (2)
    EXPECT_EQ(results[1].get<int64_t>(), 1);  // amount=200, category=B → row 1
    EXPECT_EQ(results[3].get<int64_t>(), 2);  // amount=180, category=B → row 2
}

// ============================================================================
// RANK Tests
// ============================================================================

TEST_F(WindowEvaluatorTest, RankWithTies) {
    // RANK() mit Ties (gleiche Werte bekommen gleiche Rank, Lücken danach)
    
    std::vector<json> rows = {
        {{"score", 100}},
        {{"score", 100}},  // Tie
        {{"score", 90}},
        {{"score", 90}},   // Tie
        {{"score", 80}}
    };
    
    WindowSpec spec;
    SortSpec sortSpec;
    sortSpec.expression = makeFieldAccess("score");
    sortSpec.ascending = false;  // DESC
    spec.orderBy = {sortSpec};
    
    WindowFunctionCall func;
    func.funcType = WindowFunctionType::RANK;
    
    auto results = evaluator.evaluate(rows, spec, func, "doc");
    
    ASSERT_EQ(results.size(), 5u);
    
    // score DESC: 100 (rank 1), 100 (rank 1), 90 (rank 3), 90 (rank 3), 80 (rank 5)
    EXPECT_EQ(results[0].get<int64_t>(), 1);  // score=100
    EXPECT_EQ(results[1].get<int64_t>(), 1);  // score=100 (tie)
    EXPECT_EQ(results[2].get<int64_t>(), 3);  // score=90 (skip rank 2)
    EXPECT_EQ(results[3].get<int64_t>(), 3);  // score=90 (tie)
    EXPECT_EQ(results[4].get<int64_t>(), 5);  // score=80 (skip ranks 3,4)
}

// ============================================================================
// DENSE_RANK Tests
// ============================================================================

TEST_F(WindowEvaluatorTest, DenseRankWithTies) {
    // DENSE_RANK() mit Ties (keine Lücken)
    
    std::vector<json> rows = {
        {{"score", 100}},
        {{"score", 100}},  // Tie
        {{"score", 90}},
        {{"score", 90}},   // Tie
        {{"score", 80}}
    };
    
    WindowSpec spec;
    SortSpec sortSpec;
    sortSpec.expression = makeFieldAccess("score");
    sortSpec.ascending = false;
    spec.orderBy = {sortSpec};
    
    WindowFunctionCall func;
    func.funcType = WindowFunctionType::DENSE_RANK;
    
    auto results = evaluator.evaluate(rows, spec, func, "doc");
    
    ASSERT_EQ(results.size(), 5u);
    
    // score DESC: 100 (rank 1), 100 (rank 1), 90 (rank 2), 90 (rank 2), 80 (rank 3)
    EXPECT_EQ(results[0].get<int64_t>(), 1);  // score=100
    EXPECT_EQ(results[1].get<int64_t>(), 1);  // score=100 (tie)
    EXPECT_EQ(results[2].get<int64_t>(), 2);  // score=90 (NO gap)
    EXPECT_EQ(results[3].get<int64_t>(), 2);  // score=90 (tie)
    EXPECT_EQ(results[4].get<int64_t>(), 3);  // score=80 (NO gap)
}

// ============================================================================
// LAG Tests
// ============================================================================

TEST_F(WindowEvaluatorTest, LagBasic) {
    // LAG(doc.amount, 1) OVER (ORDER BY doc.id)
    
    std::vector<json> rows = {
        {{"id", 1}, {"amount", 100}},
        {{"id", 2}, {"amount", 200}},
        {{"id", 3}, {"amount", 150}},
        {{"id", 4}, {"amount", 180}}
    };
    
    WindowSpec spec;
    SortSpec sortSpec;
    sortSpec.expression = makeFieldAccess("id");
    sortSpec.ascending = true;
    spec.orderBy = {sortSpec};
    
    WindowFunctionCall func;
    func.funcType = WindowFunctionType::LAG;
    func.argument = makeFieldAccess("amount");
    func.offset = 1;
    
    auto results = evaluator.evaluate(rows, spec, func, "doc");
    
    ASSERT_EQ(results.size(), 4u);
    
    // id=1: LAG(amount, 1) = null (kein Vorgänger)
    EXPECT_TRUE(results[0].is_null());
    
    // id=2: LAG(amount, 1) = 100 (Vorgänger: id=1)
    EXPECT_EQ(results[1].get<int>(), 100);
    
    // id=3: LAG(amount, 1) = 200 (Vorgänger: id=2)
    EXPECT_EQ(results[2].get<int>(), 200);
    
    // id=4: LAG(amount, 1) = 150 (Vorgänger: id=3)
    EXPECT_EQ(results[3].get<int>(), 150);
}

TEST_F(WindowEvaluatorTest, LagWithOffset) {
    // LAG(doc.amount, 2) OVER (ORDER BY doc.id)
    
    std::vector<json> rows = {
        {{"id", 1}, {"amount", 100}},
        {{"id", 2}, {"amount", 200}},
        {{"id", 3}, {"amount", 150}},
        {{"id", 4}, {"amount", 180}}
    };
    
    WindowSpec spec;
    SortSpec sortSpec;
    sortSpec.expression = makeFieldAccess("id");
    sortSpec.ascending = true;
    spec.orderBy = {sortSpec};
    
    WindowFunctionCall func;
    func.funcType = WindowFunctionType::LAG;
    func.argument = makeFieldAccess("amount");
    func.offset = 2;  // 2 Rows zurück
    
    auto results = evaluator.evaluate(rows, spec, func, "doc");
    
    ASSERT_EQ(results.size(), 4u);
    
    // id=1: LAG(amount, 2) = null
    EXPECT_TRUE(results[0].is_null());
    
    // id=2: LAG(amount, 2) = null
    EXPECT_TRUE(results[1].is_null());
    
    // id=3: LAG(amount, 2) = 100 (2 zurück: id=1)
    EXPECT_EQ(results[2].get<int>(), 100);
    
    // id=4: LAG(amount, 2) = 200 (2 zurück: id=2)
    EXPECT_EQ(results[3].get<int>(), 200);
}

TEST_F(WindowEvaluatorTest, LagWithDefault) {
    // LAG(doc.amount, 1, 0) OVER (ORDER BY doc.id) - Default: 0
    
    std::vector<json> rows = {
        {{"id", 1}, {"amount", 100}},
        {{"id", 2}, {"amount", 200}}
    };
    
    WindowSpec spec;
    SortSpec sortSpec;
    sortSpec.expression = makeFieldAccess("id");
    sortSpec.ascending = true;
    spec.orderBy = {sortSpec};
    
    WindowFunctionCall func;
    func.funcType = WindowFunctionType::LAG;
    func.argument = makeFieldAccess("amount");
    func.offset = 1;
    func.defaultValue = makeLiteral(0);  // Default: 0
    
    auto results = evaluator.evaluate(rows, spec, func, "doc");
    
    ASSERT_EQ(results.size(), 2u);
    
    // id=1: LAG(amount, 1) = 0 (default statt null)
    EXPECT_EQ(results[0].get<int64_t>(), 0);
    
    // id=2: LAG(amount, 1) = 100
    EXPECT_EQ(results[1].get<int>(), 100);
}

// ============================================================================
// LEAD Tests
// ============================================================================

TEST_F(WindowEvaluatorTest, LeadBasic) {
    // LEAD(doc.amount, 1) OVER (ORDER BY doc.id)
    
    std::vector<json> rows = {
        {{"id", 1}, {"amount", 100}},
        {{"id", 2}, {"amount", 200}},
        {{"id", 3}, {"amount", 150}},
        {{"id", 4}, {"amount", 180}}
    };
    
    WindowSpec spec;
    SortSpec sortSpec;
    sortSpec.expression = makeFieldAccess("id");
    sortSpec.ascending = true;
    spec.orderBy = {sortSpec};
    
    WindowFunctionCall func;
    func.funcType = WindowFunctionType::LEAD;
    func.argument = makeFieldAccess("amount");
    func.offset = 1;
    
    auto results = evaluator.evaluate(rows, spec, func, "doc");
    
    ASSERT_EQ(results.size(), 4u);
    
    // id=1: LEAD(amount, 1) = 200 (Nachfolger: id=2)
    EXPECT_EQ(results[0].get<int>(), 200);
    
    // id=2: LEAD(amount, 1) = 150 (Nachfolger: id=3)
    EXPECT_EQ(results[1].get<int>(), 150);
    
    // id=3: LEAD(amount, 1) = 180 (Nachfolger: id=4)
    EXPECT_EQ(results[2].get<int>(), 180);
    
    // id=4: LEAD(amount, 1) = null (kein Nachfolger)
    EXPECT_TRUE(results[3].is_null());
}

// ============================================================================
// FIRST_VALUE Tests
// ============================================================================

TEST_F(WindowEvaluatorTest, FirstValueNoPartition) {
    // FIRST_VALUE(doc.product) OVER (ORDER BY doc.amount DESC)
    
    std::vector<json> rows = {
        {{"amount", 100}, {"product", "A"}},
        {{"amount", 200}, {"product", "B"}},
        {{"amount", 150}, {"product", "C"}}
    };
    
    WindowSpec spec;
    SortSpec sortSpec;
    sortSpec.expression = makeFieldAccess("amount");
    sortSpec.ascending = false;
    spec.orderBy = {sortSpec};
    
    WindowFunctionCall func;
    func.funcType = WindowFunctionType::FIRST_VALUE;
    func.argument = makeFieldAccess("product");
    
    auto results = evaluator.evaluate(rows, spec, func, "doc");
    
    ASSERT_EQ(results.size(), 3u);
    
    // Nach amount DESC: 200 (B), 150 (C), 100 (A)
    // FIRST_VALUE ist immer "B" (höchster amount)
    EXPECT_EQ(results[0].get<std::string>(), "B");
    EXPECT_EQ(results[1].get<std::string>(), "B");
    EXPECT_EQ(results[2].get<std::string>(), "B");
}

TEST_F(WindowEvaluatorTest, FirstValueWithPartition) {
    // FIRST_VALUE(doc.amount) OVER (PARTITION BY doc.category ORDER BY doc.amount DESC)
    
    std::vector<json> rows = {
        {{"amount", 100}, {"category", "A"}},
        {{"amount", 200}, {"category", "B"}},
        {{"amount", 150}, {"category", "A"}},
        {{"amount", 180}, {"category", "B"}}
    };
    
    WindowSpec spec;
    spec.partitionBy = {makeFieldAccess("category")};
    
    SortSpec sortSpec;
    sortSpec.expression = makeFieldAccess("amount");
    sortSpec.ascending = false;
    spec.orderBy = {sortSpec};
    
    WindowFunctionCall func;
    func.funcType = WindowFunctionType::FIRST_VALUE;
    func.argument = makeFieldAccess("amount");
    
    auto results = evaluator.evaluate(rows, spec, func, "doc");
    
    ASSERT_EQ(results.size(), 4u);
    
    // Partition A: FIRST_VALUE = 150 (höchster amount in A)
    EXPECT_EQ(results[0].get<int>(), 150);  // amount=100, category=A
    EXPECT_EQ(results[2].get<int>(), 150);  // amount=150, category=A
    
    // Partition B: FIRST_VALUE = 200 (höchster amount in B)
    EXPECT_EQ(results[1].get<int>(), 200);  // amount=200, category=B
    EXPECT_EQ(results[3].get<int>(), 200);  // amount=180, category=B
}

// ============================================================================
// LAST_VALUE Tests
// ============================================================================

TEST_F(WindowEvaluatorTest, LastValueDefaultFrame) {
    // LAST_VALUE(doc.product) OVER (ORDER BY doc.amount DESC)
    // Default Frame: RANGE BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
    // → LAST_VALUE ist der Wert der aktuellen Row
    
    std::vector<json> rows = {
        {{"amount", 100}, {"product", "A"}},
        {{"amount", 200}, {"product", "B"}},
        {{"amount", 150}, {"product", "C"}}
    };
    
    WindowSpec spec;
    SortSpec sortSpec;
    sortSpec.expression = makeFieldAccess("amount");
    sortSpec.ascending = false;
    spec.orderBy = {sortSpec};
    
    // Default Frame: UNBOUNDED PRECEDING AND CURRENT ROW
    spec.frame = WindowFrame();
    
    WindowFunctionCall func;
    func.funcType = WindowFunctionType::LAST_VALUE;
    func.argument = makeFieldAccess("product");
    
    auto results = evaluator.evaluate(rows, spec, func, "doc");
    
    ASSERT_EQ(results.size(), 3u);
    
    // Nach amount DESC: 200 (B), 150 (C), 100 (A)
    // Mit Default Frame (CURRENT ROW): LAST_VALUE = eigener Wert
    EXPECT_EQ(results[0].get<std::string>(), "A");  // amount=100 → product=A
    EXPECT_EQ(results[1].get<std::string>(), "B");  // amount=200 → product=B
    EXPECT_EQ(results[2].get<std::string>(), "C");  // amount=150 → product=C
}

TEST_F(WindowEvaluatorTest, LastValueUnboundedFollowing) {
    // LAST_VALUE(doc.product) OVER (ORDER BY doc.amount DESC 
    //                                RANGE BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING)
    // → LAST_VALUE ist immer der letzte Wert der Partition
    
    std::vector<json> rows = {
        {{"amount", 100}, {"product", "A"}},
        {{"amount", 200}, {"product", "B"}},
        {{"amount", 150}, {"product", "C"}}
    };
    
    WindowSpec spec;
    SortSpec sortSpec;
    sortSpec.expression = makeFieldAccess("amount");
    sortSpec.ascending = false;
    spec.orderBy = {sortSpec};
    
    // Frame: UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING
    spec.frame = WindowFrame(WindowFrameType::RANGE,
                              WindowFrameBound::unboundedPreceding(),
                              WindowFrameBound::unboundedFollowing());
    
    WindowFunctionCall func;
    func.funcType = WindowFunctionType::LAST_VALUE;
    func.argument = makeFieldAccess("product");
    
    auto results = evaluator.evaluate(rows, spec, func, "doc");
    
    ASSERT_EQ(results.size(), 3u);
    
    // Nach amount DESC: 200 (B), 150 (C), 100 (A)
    // LAST_VALUE = immer "A" (niedrigster amount = letzter in DESC)
    EXPECT_EQ(results[0].get<std::string>(), "A");
    EXPECT_EQ(results[1].get<std::string>(), "A");
    EXPECT_EQ(results[2].get<std::string>(), "A");
}

// ============================================================================
// Multi-Column Partitioning & Sorting
// ============================================================================

TEST_F(WindowEvaluatorTest, MultiColumnPartition) {
    // PARTITION BY doc.category, doc.region
    
    std::vector<json> rows = {
        {{"category", "A"}, {"region", "EU"}, {"amount", 100}},
        {{"category", "A"}, {"region", "US"}, {"amount", 200}},
        {{"category", "A"}, {"region", "EU"}, {"amount", 150}},
        {{"category", "B"}, {"region", "EU"}, {"amount", 180}}
    };
    
    WindowSpec spec;
    spec.partitionBy = {makeFieldAccess("category"), makeFieldAccess("region")};
    
    SortSpec sortSpec;
    sortSpec.expression = makeFieldAccess("amount");
    sortSpec.ascending = false;
    spec.orderBy = {sortSpec};
    
    WindowFunctionCall func;
    func.funcType = WindowFunctionType::ROW_NUMBER;
    
    auto results = evaluator.evaluate(rows, spec, func, "doc");
    
    ASSERT_EQ(results.size(), 4u);
    
    // Partition (A, EU): 150 (row 1), 100 (row 2)
    EXPECT_EQ(results[0].get<int64_t>(), 2);  // category=A, region=EU, amount=100
    EXPECT_EQ(results[2].get<int64_t>(), 1);  // category=A, region=EU, amount=150
    
    // Partition (A, US): 200 (row 1)
    EXPECT_EQ(results[1].get<int64_t>(), 1);  // category=A, region=US, amount=200
    
    // Partition (B, EU): 180 (row 1)
    EXPECT_EQ(results[3].get<int64_t>(), 1);  // category=B, region=EU, amount=180
}

TEST_F(WindowEvaluatorTest, MultiColumnSort) {
    // ORDER BY doc.category ASC, doc.amount DESC
    
    std::vector<json> rows = {
        {{"category", "B"}, {"amount", 100}},
        {{"category", "A"}, {"amount", 200}},
        {{"category", "A"}, {"amount", 150}},
        {{"category", "B"}, {"amount", 180}}
    };
    
    WindowSpec spec;
    
    SortSpec sort1;
    sort1.expression = makeFieldAccess("category");
    sort1.ascending = true;
    
    SortSpec sort2;
    sort2.expression = makeFieldAccess("amount");
    sort2.ascending = false;
    
    spec.orderBy = {sort1, sort2};
    
    WindowFunctionCall func;
    func.funcType = WindowFunctionType::ROW_NUMBER;
    
    auto results = evaluator.evaluate(rows, spec, func, "doc");
    
    ASSERT_EQ(results.size(), 4u);
    
    // Sorted: (A, 200) row 1, (A, 150) row 2, (B, 180) row 3, (B, 100) row 4
    EXPECT_EQ(results[0].get<int64_t>(), 4);  // category=B, amount=100 → row 4
    EXPECT_EQ(results[1].get<int64_t>(), 1);  // category=A, amount=200 → row 1
    EXPECT_EQ(results[2].get<int64_t>(), 2);  // category=A, amount=150 → row 2
    EXPECT_EQ(results[3].get<int64_t>(), 3);  // category=B, amount=180 → row 3
}

// ============================================================================
// Empty/Edge Cases
// ============================================================================

TEST_F(WindowEvaluatorTest, EmptyRowset) {
    std::vector<json> rows = {};
    
    WindowSpec spec;
    WindowFunctionCall func;
    func.funcType = WindowFunctionType::ROW_NUMBER;
    
    auto results = evaluator.evaluate(rows, spec, func, "doc");
    
    EXPECT_TRUE(results.empty());
}

TEST_F(WindowEvaluatorTest, SingleRow) {
    std::vector<json> rows = {
        {{"amount", 100}}
    };
    
    WindowSpec spec;
    WindowFunctionCall func;
    func.funcType = WindowFunctionType::ROW_NUMBER;
    
    auto results = evaluator.evaluate(rows, spec, func, "doc");
    
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].get<int64_t>(), 1);
}
