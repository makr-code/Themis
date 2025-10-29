#pragma once

#include <memory>
#include <string>
#include <vector>
#include <variant>
#include <nlohmann/json.hpp>

namespace themis {
namespace query {

// Forward declarations
struct ASTNode;
struct Expression;

// ============================================================================
// AST Node Types
// ============================================================================

enum class ASTNodeType {
    // Query Nodes
    Query,              // Root node
    ForNode,            // FOR variable IN collection
    FilterNode,         // FILTER condition
    SortNode,           // SORT expr [ASC|DESC]
    LimitNode,          // LIMIT offset, count
    ReturnNode,         // RETURN expression
    LetNode,            // LET variable = expression (Phase 2)
    CollectNode,        // COLLECT ... AGGREGATE ... (Phase 2)
    
    // Expressions
    BinaryOp,           // ==, !=, >, <, >=, <=, AND, OR, +, -, *, /
    UnaryOp,            // NOT, -, +
    FunctionCall,       // CONCAT, SUM, LOWER, etc.
    FieldAccess,        // doc.field, doc.nested.field
    Literal,            // "string", 123, true, false, null
    Variable,           // doc, user, etc.
    ArrayLiteral,       // [1, 2, 3] or ["a", "b"]
    ObjectConstruct     // {name: doc.name, age: doc.age}
};

// ============================================================================
// Literal Value Types
// ============================================================================

using LiteralValue = std::variant<
    std::nullptr_t,     // null
    bool,               // true/false
    int64_t,            // integers
    double,             // floats
    std::string         // strings
>;

// ============================================================================
// Operators
// ============================================================================

enum class BinaryOperator {
    // Comparison
    Eq,                 // ==
    Neq,                // !=
    Lt,                 // <
    Lte,                // <=
    Gt,                 // >
    Gte,                // >=
    
    // Logical
    And,                // AND
    Or,                 // OR
    Xor,                // XOR
    
    // Arithmetic
    Add,                // +
    Sub,                // -
    Mul,                // *
    Div,                // /
    Mod,                // %
    
    // String
    In                  // IN (for membership tests)
};

enum class UnaryOperator {
    Not,                // NOT
    Minus,              // - (unary minus)
    Plus                // + (unary plus)
};

// ============================================================================
// Expression Nodes
// ============================================================================

struct Expression {
    virtual ~Expression() = default;
    virtual ASTNodeType getType() const = 0;
    virtual nlohmann::json toJSON() const = 0;
};

struct LiteralExpr : Expression {
    LiteralValue value;
    
    explicit LiteralExpr(LiteralValue val) : value(std::move(val)) {}
    
    ASTNodeType getType() const override { return ASTNodeType::Literal; }
    nlohmann::json toJSON() const override;
};

struct VariableExpr : Expression {
    std::string name;
    
    explicit VariableExpr(std::string n) : name(std::move(n)) {}
    
    ASTNodeType getType() const override { return ASTNodeType::Variable; }
    nlohmann::json toJSON() const override {
        return {{"type", "variable"}, {"name", name}};
    }
};

struct FieldAccessExpr : Expression {
    std::shared_ptr<Expression> object;  // Variable or nested FieldAccess
    std::string field;
    
    FieldAccessExpr(std::shared_ptr<Expression> obj, std::string f)
        : object(std::move(obj)), field(std::move(f)) {}
    
    ASTNodeType getType() const override { return ASTNodeType::FieldAccess; }
    nlohmann::json toJSON() const override;
};

struct BinaryOpExpr : Expression {
    BinaryOperator op;
    std::shared_ptr<Expression> left;
    std::shared_ptr<Expression> right;
    
    BinaryOpExpr(BinaryOperator o, std::shared_ptr<Expression> l, std::shared_ptr<Expression> r)
        : op(o), left(std::move(l)), right(std::move(r)) {}
    
    ASTNodeType getType() const override { return ASTNodeType::BinaryOp; }
    nlohmann::json toJSON() const override;
};

struct UnaryOpExpr : Expression {
    UnaryOperator op;
    std::shared_ptr<Expression> operand;
    
    UnaryOpExpr(UnaryOperator o, std::shared_ptr<Expression> e)
        : op(o), operand(std::move(e)) {}
    
    ASTNodeType getType() const override { return ASTNodeType::UnaryOp; }
    nlohmann::json toJSON() const override;
};

struct FunctionCallExpr : Expression {
    std::string name;
    std::vector<std::shared_ptr<Expression>> arguments;
    
    FunctionCallExpr(std::string n, std::vector<std::shared_ptr<Expression>> args)
        : name(std::move(n)), arguments(std::move(args)) {}
    
    ASTNodeType getType() const override { return ASTNodeType::FunctionCall; }
    nlohmann::json toJSON() const override;
};

struct ArrayLiteralExpr : Expression {
    std::vector<std::shared_ptr<Expression>> elements;
    
    explicit ArrayLiteralExpr(std::vector<std::shared_ptr<Expression>> elems)
        : elements(std::move(elems)) {}
    
    ASTNodeType getType() const override { return ASTNodeType::ArrayLiteral; }
    nlohmann::json toJSON() const override;
};

struct ObjectConstructExpr : Expression {
    std::vector<std::pair<std::string, std::shared_ptr<Expression>>> fields;
    
    explicit ObjectConstructExpr(std::vector<std::pair<std::string, std::shared_ptr<Expression>>> f)
        : fields(std::move(f)) {}
    
    ASTNodeType getType() const override { return ASTNodeType::ObjectConstruct; }
    nlohmann::json toJSON() const override;
};

// ============================================================================
// Query Nodes
// ============================================================================

struct SortSpec {
    std::shared_ptr<Expression> expression;
    bool ascending = true;  // true = ASC, false = DESC
    
    nlohmann::json toJSON() const {
        return {
            {"expression", expression->toJSON()},
            {"ascending", ascending}
        };
    }
};

struct ForNode {
    std::string variable;           // Loop variable (e.g., "doc", "user")
    std::string collection;         // Collection name (e.g., "users")
    
    nlohmann::json toJSON() const {
        return {
            {"type", "for"},
            {"variable", variable},
            {"collection", collection}
        };
    }
};

struct FilterNode {
    std::shared_ptr<Expression> condition;
    
    explicit FilterNode(std::shared_ptr<Expression> cond)
        : condition(std::move(cond)) {}
    
    nlohmann::json toJSON() const {
        return {
            {"type", "filter"},
            {"condition", condition->toJSON()}
        };
    }
};

struct SortNode {
    std::vector<SortSpec> specifications;
    
    explicit SortNode(std::vector<SortSpec> specs)
        : specifications(std::move(specs)) {}
    
    nlohmann::json toJSON() const {
        nlohmann::json specs_json = nlohmann::json::array();
        for (const auto& spec : specifications) {
            specs_json.push_back(spec.toJSON());
        }
        return {
            {"type", "sort"},
            {"specifications", specs_json}
        };
    }
};

struct LimitNode {
    int64_t offset = 0;
    int64_t count = 0;
    
    LimitNode(int64_t off, int64_t cnt) : offset(off), count(cnt) {}
    
    nlohmann::json toJSON() const {
        return {
            {"type", "limit"},
            {"offset", offset},
            {"count", count}
        };
    }
};

struct ReturnNode {
    std::shared_ptr<Expression> expression;
    
    explicit ReturnNode(std::shared_ptr<Expression> expr)
        : expression(std::move(expr)) {}
    
    nlohmann::json toJSON() const {
        return {
            {"type", "return"},
            {"expression", expression->toJSON()}
        };
    }
};

// ============================================================================
// Collect/GROUP BY Nodes (MVP)
// ============================================================================

struct CollectNode {
    // Group-by variables: varName = expression (MVP: typically a single field access like doc.city)
    std::vector<std::pair<std::string, std::shared_ptr<Expression>>> groups;

    struct Aggregation {
        std::string varName;                               // output variable name, e.g., "cnt"
        std::string funcName;                              // COUNT, SUM, AVG, MIN, MAX (case-insensitive)
        std::shared_ptr<Expression> argument;              // may be null (COUNT())
    };
    std::vector<Aggregation> aggregations;                 // optional

    nlohmann::json toJSON() const {
        nlohmann::json j;
        j["type"] = "collect";
        nlohmann::json g = nlohmann::json::array();
        for (const auto& [v, e] : groups) {
            g.push_back({{"var", v}, {"expr", e ? e->toJSON() : nlohmann::json()}});
        }
        j["groups"] = g;
        nlohmann::json a = nlohmann::json::array();
        for (const auto& ag : aggregations) {
            a.push_back({{"var", ag.varName}, {"func", ag.funcName}, {"arg", ag.argument ? ag.argument->toJSON() : nlohmann::json()}});
        }
        j["aggregations"] = a;
        return j;
    }
};

// ============================================================================
// Query AST (Root)
// ============================================================================

struct Query {
    ForNode for_node;
    std::vector<std::shared_ptr<FilterNode>> filters;
    std::shared_ptr<SortNode> sort;
    std::shared_ptr<LimitNode> limit;
    std::shared_ptr<ReturnNode> return_node;
    std::shared_ptr<CollectNode> collect; // optional GROUP BY/AGGREGATE
    // Optional: Graph Traversal-Klausel (FOR v[,e[,p]] IN min..max OUTBOUND|INBOUND|ANY start GRAPH name)
    // Wenn gesetzt, beschreibt sie eine Traversal-Query statt einer Collection-Iteration.
    struct TraversalNode {
        enum class Direction { Outbound, Inbound, Any };
        std::string varVertex; // v
        std::string varEdge;   // optional, leer wenn nicht gesetzt (Phase 2)
        std::string varPath;   // optional, leer wenn nicht gesetzt (Phase 2)
        int minDepth = 1;
        int maxDepth = 1;
        Direction direction = Direction::Outbound;
        std::string startVertex; // Primary Key des Startknotens
        std::string graphName;   // Graph-Name (aktuell informativ)

        nlohmann::json toJSON() const {
            const char* dir = direction == Direction::Outbound ? "OUTBOUND" : (direction == Direction::Inbound ? "INBOUND" : "ANY");
            return {
                {"type", "traversal"},
                {"varVertex", varVertex},
                {"varEdge", varEdge},
                {"varPath", varPath},
                {"minDepth", minDepth},
                {"maxDepth", maxDepth},
                {"direction", dir},
                {"startVertex", startVertex},
                {"graphName", graphName}
            };
        }
    };
    std::shared_ptr<TraversalNode> traversal;
    
    nlohmann::json toJSON() const {
        nlohmann::json j = {
            {"type", "query"},
            {"for", for_node.toJSON()}
        };
        
        if (!filters.empty()) {
            nlohmann::json filters_json = nlohmann::json::array();
            for (const auto& filter : filters) {
                filters_json.push_back(filter->toJSON());
            }
            j["filters"] = filters_json;
        }
        
        if (sort) {
            j["sort"] = sort->toJSON();
        }
        
        if (limit) {
            j["limit"] = limit->toJSON();
        }
        
        if (return_node) {
            j["return"] = return_node->toJSON();
        }
        if (collect) {
            j["collect"] = collect->toJSON();
        }
        if (traversal) {
            j["traversal"] = traversal->toJSON();
        }
        
        return j;
    }
};

// ============================================================================
// Parser Error
// ============================================================================

struct ParseError {
    std::string message;
    size_t line = 0;
    size_t column = 0;
    std::string context;  // Snippet of the query around the error
    
    std::string toString() const {
        std::string result = "Parse error at line " + std::to_string(line) 
                           + ", column " + std::to_string(column) + ": " + message;
        if (!context.empty()) {
            result += "\n  " + context;
        }
        return result;
    }
};

// ============================================================================
// Parser Result
// ============================================================================

struct ParseResult {
    bool success = false;
    std::shared_ptr<Query> query;
    ParseError error;
    
    static ParseResult Success(std::shared_ptr<Query> q) {
        ParseResult result;
        result.success = true;
        result.query = std::move(q);
        return result;
    }
    
    static ParseResult Failure(std::string msg, size_t line = 0, size_t col = 0, std::string ctx = "") {
        ParseResult result;
        result.success = false;
        result.error.message = std::move(msg);
        result.error.line = line;
        result.error.column = col;
        result.error.context = std::move(ctx);
        return result;
    }
};

// ============================================================================
// AQL Parser
// ============================================================================

class AQLParser {
public:
    AQLParser() = default;
    
    /**
     * Parse an AQL query string into an AST.
     * 
     * @param query_string The AQL query to parse
     * @return ParseResult containing either the AST or an error
     * 
     * Example:
     *   auto result = parser.parse("FOR doc IN users FILTER doc.age > 18 RETURN doc");
     *   if (result.success) {
     *       // Use result.query
     *   } else {
     *       // Handle result.error
     *   }
     */
    ParseResult parse(const std::string& query_string);
    
private:
    // Helper methods (implemented in aql_parser.cpp)
    std::shared_ptr<Expression> parseExpression(const std::string& expr_str);
    std::shared_ptr<Expression> parsePrimaryExpression(const std::string& expr_str);
    BinaryOperator stringToOperator(const std::string& op_str);
};

}  // namespace query
}  // namespace themis
