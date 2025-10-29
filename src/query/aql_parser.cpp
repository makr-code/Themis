﻿#include "query/aql_parser.h"
#include <cctype>
#include <sstream>
#include <algorithm>
#include <stdexcept>

namespace themis {
namespace query {

// ============================================================================
// Tokenizer (Lexer)
// ============================================================================

enum class TokenType {
    // Keywords
    FOR, IN, FILTER, SORT, LIMIT, RETURN,
    ASC, DESC, AND, OR, XOR, NOT,
    GRAPH, OUTBOUND, INBOUND, ANY,
    COLLECT, AGGREGATE,
    TRUE, FALSE, NULL_LITERAL,
    
    // Operators
    EQ, NEQ, LT, LTE, GT, GTE,
    PLUS, MINUS, STAR, SLASH,
    ASSIGN,  // Single '=' for assignments (COLLECT var = expr, AGGREGATE var = func)
    
    // Literals
    IDENTIFIER, STRING, INTEGER, FLOAT,
    
    // Punctuation
    DOT, COMMA, LPAREN, RPAREN, LBRACE, RBRACE,
    
    // Special
    END_OF_FILE, INVALID
};

struct Token {
    TokenType type;
    std::string value;
    size_t line;
    size_t column;
    
    Token(TokenType t, std::string v, size_t l, size_t c)
        : type(t), value(std::move(v)), line(l), column(c) {}
};

class Tokenizer {
public:
    explicit Tokenizer(const std::string& input)
        : input_(input), pos_(0), line_(1), column_(1) {}
    
    std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        
        while (pos_ < input_.size()) {
            skipWhitespace();
            if (pos_ >= input_.size()) break;
            
            Token token = nextToken();
            if (token.type != TokenType::INVALID) {
                tokens.push_back(token);
            }
        }
        
        tokens.emplace_back(TokenType::END_OF_FILE, "", line_, column_);
        return tokens;
    }
    
private:
    std::string input_;
    size_t pos_;
    size_t line_;
    size_t column_;
    
    char peek(size_t offset = 0) const {
        size_t p = pos_ + offset;
        return (p < input_.size()) ? input_[p] : '\0';
    }
    
    char advance() {
        if (pos_ >= input_.size()) return '\0';
        char ch = input_[pos_++];
        if (ch == '\n') {
            line_++;
            column_ = 1;
        } else {
            column_++;
        }
        return ch;
    }
    
    void skipWhitespace() {
        while (pos_ < input_.size() && std::isspace(peek())) {
            advance();
        }
    }
    
    Token nextToken() {
        size_t start_line = line_;
        size_t start_column = column_;
        char ch = peek();
        
        // String literal (unterstützt ' und ")
        if (ch == '"' || ch == '\'') {
            return readString(start_line, start_column);
        }
        
        // Number
        if (std::isdigit(ch) || (ch == '-' && std::isdigit(peek(1)))) {
            return readNumber(start_line, start_column);
        }
        
        // Identifier or keyword
        if (std::isalpha(ch) || ch == '_') {
            return readIdentifierOrKeyword(start_line, start_column);
        }
        
        // Operators and punctuation
        return readOperatorOrPunctuation(start_line, start_column);
    }
    
    Token readString(size_t line, size_t col) {
        char quote = peek();
        // Support both double and single quotes
        if (quote != '"' && quote != '\'') {
            // Fallback (shouldn't happen): treat as invalid
            return Token(TokenType::INVALID, std::string(1, advance()), line, col);
        }
        advance(); // Skip opening quote
        std::string value;
        
        while (peek() != quote && peek() != '\0') {
            if (peek() == '\\') {
                advance();
                char next = advance();
                switch (next) {
                    case 'n': value += '\n'; break;
                    case 't': value += '\t'; break;
                    case 'r': value += '\r'; break;
                    case '"': value += '"'; break;
                    case '\'': value += '\''; break;
                    case '\\': value += '\\'; break;
                    default: value += next; break;
                }
            } else {
                value += advance();
            }
        }
        
        if (peek() == quote) {
            advance(); // Skip closing quote
        }
        
        return Token(TokenType::STRING, value, line, col);
    }
    
    Token readNumber(size_t line, size_t col) {
        std::string value;
        bool is_float = false;
        
        if (peek() == '-') {
            value += advance();
        }
        
        while (std::isdigit(peek())) {
            value += advance();
        }
        
        // Only treat as float if dot is followed by a digit (e.g., 1.23)
        if (peek() == '.' && std::isdigit(peek(1))) {
            is_float = true;
            value += advance();
            while (std::isdigit(peek())) {
                value += advance();
            }
        }
        
        return Token(is_float ? TokenType::FLOAT : TokenType::INTEGER, value, line, col);
    }
    
    Token readIdentifierOrKeyword(size_t line, size_t col) {
        std::string value;
        
        while (std::isalnum(peek()) || peek() == '_') {
            value += advance();
        }
        
        // Convert to lowercase for keyword matching
        std::string lower = value;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        
        // Check keywords
        if (lower == "for") return Token(TokenType::FOR, value, line, col);
        if (lower == "in") return Token(TokenType::IN, value, line, col);
        if (lower == "filter") return Token(TokenType::FILTER, value, line, col);
        if (lower == "sort") return Token(TokenType::SORT, value, line, col);
        if (lower == "limit") return Token(TokenType::LIMIT, value, line, col);
        if (lower == "return") return Token(TokenType::RETURN, value, line, col);
        if (lower == "asc") return Token(TokenType::ASC, value, line, col);
        if (lower == "desc") return Token(TokenType::DESC, value, line, col);
    if (lower == "and") return Token(TokenType::AND, value, line, col);
    if (lower == "or") return Token(TokenType::OR, value, line, col);
    if (lower == "xor") return Token(TokenType::XOR, value, line, col);
        if (lower == "not") return Token(TokenType::NOT, value, line, col);
        if (lower == "true") return Token(TokenType::TRUE, value, line, col);
        if (lower == "false") return Token(TokenType::FALSE, value, line, col);
        if (lower == "null") return Token(TokenType::NULL_LITERAL, value, line, col);
        if (lower == "graph") return Token(TokenType::GRAPH, value, line, col);
        if (lower == "outbound") return Token(TokenType::OUTBOUND, value, line, col);
        if (lower == "inbound") return Token(TokenType::INBOUND, value, line, col);
        if (lower == "any") return Token(TokenType::ANY, value, line, col);
        if (lower == "collect") return Token(TokenType::COLLECT, value, line, col);
        if (lower == "aggregate") return Token(TokenType::AGGREGATE, value, line, col);
        
        return Token(TokenType::IDENTIFIER, value, line, col);
    }
    
    Token readOperatorOrPunctuation(size_t line, size_t col) {
        char ch = peek();
        
        // Two-character operators
        if (ch == '=' && peek(1) == '=') {
            advance(); advance();
            // Check for invalid === operator
            if (peek() == '=') {
                return Token(TokenType::INVALID, "===", line, col);
            }
            return Token(TokenType::EQ, "==", line, col);
        }
        if (ch == '!' && peek(1) == '=') {
            advance(); advance();
            return Token(TokenType::NEQ, "!=", line, col);
        }
        if (ch == '<' && peek(1) == '=') {
            advance(); advance();
            return Token(TokenType::LTE, "<=", line, col);
        }
        if (ch == '>' && peek(1) == '=') {
            advance(); advance();
            return Token(TokenType::GTE, ">=", line, col);
        }
        
        // Single-character operators/punctuation
        advance();
        switch (ch) {
            case '=': return Token(TokenType::ASSIGN, "=", line, col);  // Single = for assignments
            case '<': return Token(TokenType::LT, "<", line, col);
            case '>': return Token(TokenType::GT, ">", line, col);
            case '+': return Token(TokenType::PLUS, "+", line, col);
            case '-': return Token(TokenType::MINUS, "-", line, col);
            case '*': return Token(TokenType::STAR, "*", line, col);
            case '/': return Token(TokenType::SLASH, "/", line, col);
            case '.': return Token(TokenType::DOT, ".", line, col);
            case ',': return Token(TokenType::COMMA, ",", line, col);
            case '(': return Token(TokenType::LPAREN, "(", line, col);
            case ')': return Token(TokenType::RPAREN, ")", line, col);
            case '{': return Token(TokenType::LBRACE, "{", line, col);
            case '}': return Token(TokenType::RBRACE, "}", line, col);
            default: return Token(TokenType::INVALID, std::string(1, ch), line, col);
        }
    }
};

// ============================================================================
// Parser
// ============================================================================

class Parser {
public:
    explicit Parser(std::vector<Token> tokens)
        : tokens_(std::move(tokens)), pos_(0) {}
    
    ParseResult parse() {
        try {
            // Check for invalid tokens first
            for (const auto& token : tokens_) {
                if (token.type == TokenType::INVALID) {
                    return ParseResult::Failure(
                        "Invalid token: " + token.value,
                        token.line,
                        token.column
                    );
                }
            }
            
            auto query = parseQuery();
            return ParseResult::Success(query);
        } catch (const std::runtime_error& e) {
            const auto& tok = current();
            return ParseResult::Failure(e.what(), tok.line, tok.column);
        }
    }
    
private:
    std::vector<Token> tokens_;
    size_t pos_;
    std::shared_ptr<Query::TraversalNode> lastTraversal_;
    
    const Token& current() const {
        return (pos_ < tokens_.size()) ? tokens_[pos_] : tokens_.back();
    }
    
    const Token& peek(size_t offset = 1) const {
        size_t p = pos_ + offset;
        return (p < tokens_.size()) ? tokens_[p] : tokens_.back();
    }
    
    void advance() {
        if (pos_ < tokens_.size()) pos_++;
    }
    
    bool match(TokenType type) const {
        return current().type == type;
    }
    
    void expect(TokenType type, const std::string& msg) {
        if (!match(type)) {
            throw std::runtime_error(msg);
        }
        advance();
    }
    
    std::shared_ptr<Query> parseQuery() {
        auto query = std::make_shared<Query>();
        
        // FOR clause (required)
        query->for_node = parseForClause();
        if (lastTraversal_) {
            query->traversal = lastTraversal_;
            lastTraversal_.reset();
        }
        
        // FILTER clauses (optional, multiple)
        while (match(TokenType::FILTER)) {
            query->filters.push_back(parseFilterClause());
        }
        
        // SORT clause (optional)
        if (match(TokenType::SORT)) {
            query->sort = parseSortClause();
        }
        
        // LIMIT clause (optional)
        if (match(TokenType::LIMIT)) {
            query->limit = parseLimitClause();
        }

        // COLLECT/GROUP BY clause (optional, MVP)
        if (match(TokenType::COLLECT)) {
            query->collect = parseCollectClause();
        }
        
        // RETURN clause (optional)
        if (match(TokenType::RETURN)) {
            query->return_node = parseReturnClause();
        }
        
        // End of query
        expect(TokenType::END_OF_FILE, "Expected end of query");
        
        return query;
    }
    
    ForNode parseForClause() {
        expect(TokenType::FOR, "Expected FOR");
        
        if (!match(TokenType::IDENTIFIER)) {
            throw std::runtime_error("Expected variable name after FOR");
        }
        std::string varVertex = current().value;
        advance();

        // Optional: ", e" und ", p" vor IN
        std::optional<std::string> varEdge;
        std::optional<std::string> varPath;
        if (match(TokenType::COMMA)) {
            advance();
            if (!match(TokenType::IDENTIFIER)) {
                throw std::runtime_error("Expected edge variable name after ','");
            }
            varEdge = current().value;
            advance();
            if (match(TokenType::COMMA)) {
                advance();
                if (!match(TokenType::IDENTIFIER)) {
                    throw std::runtime_error("Expected path variable name after second ','");
                }
                varPath = current().value;
                advance();
            }
        }

        expect(TokenType::IN, "Expected IN");

        // Two alternatives after IN:
        // 1) Collection name (IDENTIFIER) => relational
        // 2) Graph traversal: INTEGER .. INTEGER <DIRECTION> STRING GRAPH STRING
        if (match(TokenType::IDENTIFIER)) {
            std::string collection = current().value;
            advance();
            ForNode node;
            node.variable = varVertex;
            node.collection = collection;
            return node;
        }

        if (match(TokenType::INTEGER)) {
            // Parse min..max
            int minDepth = std::stoi(current().value);
            advance();
            // Expect '..' as two DOT tokens
            if (!match(TokenType::DOT) || peek().type != TokenType::DOT) {
                throw std::runtime_error("Expected '..' in traversal depth range");
            }
            advance(); // first '.'
            advance(); // second '.'
            if (!match(TokenType::INTEGER)) {
                throw std::runtime_error("Expected max depth integer after '..'");
            }
            int maxDepth = std::stoi(current().value);
            advance();

            // Direction
            Query::TraversalNode::Direction dir;
            if (match(TokenType::OUTBOUND)) { dir = Query::TraversalNode::Direction::Outbound; advance(); }
            else if (match(TokenType::INBOUND)) { dir = Query::TraversalNode::Direction::Inbound; advance(); }
            else if (match(TokenType::ANY)) { dir = Query::TraversalNode::Direction::Any; advance(); }
            else { throw std::runtime_error("Expected OUTBOUND, INBOUND or ANY in traversal"); }

            // Start vertex (STRING)
            if (!match(TokenType::STRING)) {
                throw std::runtime_error("Expected start vertex string literal in traversal");
            }
            std::string startVertex = current().value;
            advance();

            // GRAPH keyword and graph name
            expect(TokenType::GRAPH, "Expected GRAPH keyword in traversal");
            if (!match(TokenType::STRING)) {
                throw std::runtime_error("Expected graph name string literal after GRAPH");
            }
            std::string graphName = current().value;
            advance();

            // Build traversal node and stash it for parseQuery
            auto trav = std::make_shared<Query::TraversalNode>();
            trav->varVertex = varVertex;
            if (varEdge) trav->varEdge = *varEdge;
            if (varPath) trav->varPath = *varPath;
            trav->minDepth = minDepth;
            trav->maxDepth = maxDepth;
            trav->direction = dir;
            trav->startVertex = startVertex;
            trav->graphName = graphName;
            lastTraversal_ = trav;

            // Still return a ForNode for compatibility (collection = "graph")
            ForNode node;
            node.variable = varVertex;
            node.collection = "graph";
            return node;
        }

        throw std::runtime_error("Expected collection name or traversal after IN");
    }
    
    std::shared_ptr<FilterNode> parseFilterClause() {
        expect(TokenType::FILTER, "Expected FILTER");
        
        auto condition = parseExpression();
        return std::make_shared<FilterNode>(condition);
    }
    
    std::shared_ptr<SortNode> parseSortClause() {
        expect(TokenType::SORT, "Expected SORT");
        
        std::vector<SortSpec> specs;
        
        do {
            if (!specs.empty()) {
                expect(TokenType::COMMA, "Expected comma");
            }
            
            SortSpec spec;
            spec.expression = parseExpression();
            spec.ascending = true; // Default ASC
            
            if (match(TokenType::ASC)) {
                advance();
                spec.ascending = true;
            } else if (match(TokenType::DESC)) {
                advance();
                spec.ascending = false;
            }
            
            specs.push_back(std::move(spec));
            
        } while (match(TokenType::COMMA));
        
        return std::make_shared<SortNode>(std::move(specs));
    }
    
    std::shared_ptr<LimitNode> parseLimitClause() {
        expect(TokenType::LIMIT, "Expected LIMIT");
        
        if (!match(TokenType::INTEGER)) {
            throw std::runtime_error("Expected integer after LIMIT");
        }
        int64_t first = std::stoll(current().value);
        advance();
        
        if (match(TokenType::COMMA)) {
            advance();
            if (!match(TokenType::INTEGER)) {
                throw std::runtime_error("Expected integer after comma in LIMIT");
            }
            int64_t second = std::stoll(current().value);
            advance();
            return std::make_shared<LimitNode>(first, second); // offset, count
        }
        
        return std::make_shared<LimitNode>(0, first); // count only
    }
    
    std::shared_ptr<ReturnNode> parseReturnClause() {
        expect(TokenType::RETURN, "Expected RETURN");
        
        auto expr = parseExpression();
        return std::make_shared<ReturnNode>(expr);
    }

    std::shared_ptr<CollectNode> parseCollectClause() {
        expect(TokenType::COLLECT, "Expected COLLECT");
        auto node = std::make_shared<CollectNode>();

        // Optional group variable(s): var = expr
        if (!match(TokenType::AGGREGATE)) {
            if (!match(TokenType::IDENTIFIER)) {
                throw std::runtime_error("Expected variable name after COLLECT");
            }
            std::string var = current().value;
            advance();
            expect(TokenType::ASSIGN, "Expected '=' after group variable in COLLECT");
            auto expr = parseExpression();
            node->groups.emplace_back(var, expr);
            // MVP: allow only one group; additional groups via comma could be added later
        }

        // Optional AGGREGATE section
        if (match(TokenType::AGGREGATE)) {
            advance();
            // Parse list: var = FUNC(expr?) [, var = FUNC(expr?)]*
            bool first = true;
            while (first || match(TokenType::COMMA)) {
                if (!first) advance();
                first = false;
                if (!match(TokenType::IDENTIFIER)) {
                    throw std::runtime_error("Expected aggregation variable name after AGGREGATE");
                }
                std::string outVar = current().value;
                advance();
                expect(TokenType::ASSIGN, "Expected '=' in aggregation assignment");
                // Expect function call
                if (!match(TokenType::IDENTIFIER)) {
                    throw std::runtime_error("Expected aggregation function name (COUNT, SUM, AVG, MIN, MAX)");
                }
                std::string funcName = current().value;
                advance();
                expect(TokenType::LPAREN, "Expected '(' after aggregation function");
                std::shared_ptr<Expression> arg;
                if (!match(TokenType::RPAREN)) {
                    arg = parseExpression();
                }
                expect(TokenType::RPAREN, "Expected ')' to close aggregation function");
                CollectNode::Aggregation ag{outVar, funcName, arg};
                node->aggregations.push_back(std::move(ag));
            }
        }

        return node;
    }
    
    std::shared_ptr<Expression> parseExpression() {
        return parseLogicalOr();
    }
    
    std::shared_ptr<Expression> parseLogicalOr() {
        auto left = parseLogicalAnd();
        
        while (match(TokenType::OR) || match(TokenType::XOR)) {
            bool isXor = match(TokenType::XOR);
            advance();
            auto right = parseLogicalAnd();
            left = std::make_shared<BinaryOpExpr>(isXor ? BinaryOperator::Xor : BinaryOperator::Or, left, right);
        }
        
        return left;
    }
    
    std::shared_ptr<Expression> parseLogicalAnd() {
        auto left = parseComparison();
        
        while (match(TokenType::AND)) {
            advance();
            auto right = parseComparison();
            left = std::make_shared<BinaryOpExpr>(BinaryOperator::And, left, right);
        }
        
        return left;
    }
    
    std::shared_ptr<Expression> parseComparison() {
        auto left = parseAdditive();
        
        if (match(TokenType::EQ)) {
            advance();
            auto right = parseAdditive();
            return std::make_shared<BinaryOpExpr>(BinaryOperator::Eq, left, right);
        }
        if (match(TokenType::NEQ)) {
            advance();
            auto right = parseAdditive();
            return std::make_shared<BinaryOpExpr>(BinaryOperator::Neq, left, right);
        }
        if (match(TokenType::LT)) {
            advance();
            auto right = parseAdditive();
            return std::make_shared<BinaryOpExpr>(BinaryOperator::Lt, left, right);
        }
        if (match(TokenType::LTE)) {
            advance();
            auto right = parseAdditive();
            return std::make_shared<BinaryOpExpr>(BinaryOperator::Lte, left, right);
        }
        if (match(TokenType::GT)) {
            advance();
            auto right = parseAdditive();
            return std::make_shared<BinaryOpExpr>(BinaryOperator::Gt, left, right);
        }
        if (match(TokenType::GTE)) {
            advance();
            auto right = parseAdditive();
            return std::make_shared<BinaryOpExpr>(BinaryOperator::Gte, left, right);
        }
        
        return left;
    }
    
    std::shared_ptr<Expression> parseAdditive() {
        auto left = parseMultiplicative();
        
        while (match(TokenType::PLUS) || match(TokenType::MINUS)) {
            BinaryOperator op = match(TokenType::PLUS) ? BinaryOperator::Add : BinaryOperator::Sub;
            advance();
            auto right = parseMultiplicative();
            left = std::make_shared<BinaryOpExpr>(op, left, right);
        }
        
        return left;
    }
    
    std::shared_ptr<Expression> parseMultiplicative() {
        auto left = parseUnary();
        
        while (match(TokenType::STAR) || match(TokenType::SLASH)) {
            BinaryOperator op = match(TokenType::STAR) ? BinaryOperator::Mul : BinaryOperator::Div;
            advance();
            auto right = parseUnary();
            left = std::make_shared<BinaryOpExpr>(op, left, right);
        }
        
        return left;
    }
    
    std::shared_ptr<Expression> parseUnary() {
        if (match(TokenType::NOT)) {
            advance();
            auto operand = parseUnary();
            return std::make_shared<UnaryOpExpr>(UnaryOperator::Not, operand);
        }
        if (match(TokenType::MINUS)) {
            advance();
            auto operand = parseUnary();
            return std::make_shared<UnaryOpExpr>(UnaryOperator::Minus, operand);
        }
        
        return parsePostfix();
    }
    
    std::shared_ptr<Expression> parsePostfix() {
        auto expr = parsePrimary();
        
        // Handle field access: doc.field or doc.field1.field2
        while (match(TokenType::DOT)) {
            advance();
            if (!match(TokenType::IDENTIFIER)) {
                throw std::runtime_error("Expected field name after '.'");
            }
            std::string field = current().value;
            advance();
            expr = std::make_shared<FieldAccessExpr>(expr, field);
        }
        
        return expr;
    }
    
    std::shared_ptr<Expression> parsePrimary() {
        // Parenthesized expression
        if (match(TokenType::LPAREN)) {
            advance();
            auto expr = parseExpression();
            expect(TokenType::RPAREN, "Expected ')'");
            return expr;
        }
        
        // Literals
        if (match(TokenType::STRING)) {
            std::string value = current().value;
            advance();
            return std::make_shared<LiteralExpr>(value);
        }
        if (match(TokenType::INTEGER)) {
            int64_t value = std::stoll(current().value);
            advance();
            return std::make_shared<LiteralExpr>(value);
        }
        if (match(TokenType::FLOAT)) {
            double value = std::stod(current().value);
            advance();
            return std::make_shared<LiteralExpr>(value);
        }
        if (match(TokenType::TRUE)) {
            advance();
            return std::make_shared<LiteralExpr>(true);
        }
        if (match(TokenType::FALSE)) {
            advance();
            return std::make_shared<LiteralExpr>(false);
        }
        if (match(TokenType::NULL_LITERAL)) {
            advance();
            return std::make_shared<LiteralExpr>(nullptr);
        }
        
        // Identifier or function call
        if (match(TokenType::IDENTIFIER)) {
            std::string name = current().value;
            advance();
            
            // Function call
            if (match(TokenType::LPAREN)) {
                advance();
                std::vector<std::shared_ptr<Expression>> args;
                
                if (!match(TokenType::RPAREN)) {
                    do {
                        if (!args.empty()) {
                            expect(TokenType::COMMA, "Expected comma");
                        }
                        args.push_back(parseExpression());
                    } while (match(TokenType::COMMA));
                }
                
                expect(TokenType::RPAREN, "Expected ')'");
                return std::make_shared<FunctionCallExpr>(name, std::move(args));
            }
            
            // Simple variable
            return std::make_shared<VariableExpr>(name);
        }
        
        throw std::runtime_error("Unexpected token: " + current().value);
    }
};

// ============================================================================
// Parser Implementation
// ============================================================================

ParseResult AQLParser::parse(const std::string& query_string) {
    try {
        // Tokenize
        Tokenizer tokenizer(query_string);
        auto tokens = tokenizer.tokenize();
        
        // Parse
        Parser parser(std::move(tokens));
        return parser.parse();
        
    } catch (const std::exception& e) {
        return ParseResult::Failure(e.what(), 0, 0, query_string);
    }
}

// ============================================================================
// JSON Serialization for AST Nodes
// ============================================================================

nlohmann::json LiteralExpr::toJSON() const {
    nlohmann::json j = {{"type", "literal"}};
    
    std::visit([&j](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
            j["value"] = nullptr;
        } else if constexpr (std::is_same_v<T, bool>) {
            j["value"] = arg;
        } else if constexpr (std::is_same_v<T, int64_t>) {
            j["value"] = arg;
        } else if constexpr (std::is_same_v<T, double>) {
            j["value"] = arg;
        } else if constexpr (std::is_same_v<T, std::string>) {
            j["value"] = arg;
        }
    }, value);
    
    return j;
}

nlohmann::json FieldAccessExpr::toJSON() const {
    return {
        {"type", "field_access"},
        {"object", object->toJSON()},
        {"field", field}
    };
}

nlohmann::json BinaryOpExpr::toJSON() const {
    const char* op_str = "unknown";
    switch (op) {
        case BinaryOperator::Eq: op_str = "=="; break;
        case BinaryOperator::Neq: op_str = "!="; break;
        case BinaryOperator::Lt: op_str = "<"; break;
        case BinaryOperator::Lte: op_str = "<="; break;
        case BinaryOperator::Gt: op_str = ">"; break;
        case BinaryOperator::Gte: op_str = ">="; break;
        case BinaryOperator::And: op_str = "AND"; break;
        case BinaryOperator::Or: op_str = "OR"; break;
    case BinaryOperator::Xor: op_str = "XOR"; break;
        case BinaryOperator::Add: op_str = "+"; break;
        case BinaryOperator::Sub: op_str = "-"; break;
        case BinaryOperator::Mul: op_str = "*"; break;
        case BinaryOperator::Div: op_str = "/"; break;
        case BinaryOperator::Mod: op_str = "%"; break;
        case BinaryOperator::In: op_str = "IN"; break;
    }
    
    return {
        {"type", "binary_op"},
        {"operator", op_str},
        {"left", left->toJSON()},
        {"right", right->toJSON()}
    };
}

nlohmann::json UnaryOpExpr::toJSON() const {
    const char* op_str = "unknown";
    switch (op) {
        case UnaryOperator::Not: op_str = "NOT"; break;
        case UnaryOperator::Minus: op_str = "-"; break;
        case UnaryOperator::Plus: op_str = "+"; break;
    }
    
    return {
        {"type", "unary_op"},
        {"operator", op_str},
        {"operand", operand->toJSON()}
    };
}

nlohmann::json FunctionCallExpr::toJSON() const {
    nlohmann::json args_json = nlohmann::json::array();
    for (const auto& arg : arguments) {
        args_json.push_back(arg->toJSON());
    }
    
    return {
        {"type", "function_call"},
        {"name", name},
        {"arguments", args_json}
    };
}

nlohmann::json ArrayLiteralExpr::toJSON() const {
    nlohmann::json elems_json = nlohmann::json::array();
    for (const auto& elem : elements) {
        elems_json.push_back(elem->toJSON());
    }
    
    return {
        {"type", "array_literal"},
        {"elements", elems_json}
    };
}

nlohmann::json ObjectConstructExpr::toJSON() const {
    nlohmann::json fields_json = nlohmann::json::object();
    for (const auto& [key, value] : fields) {
        fields_json[key] = value->toJSON();
    }
    
    return {
        {"type", "object_construct"},
        {"fields", fields_json}
    };
}

}  // namespace query
}  // namespace themis
