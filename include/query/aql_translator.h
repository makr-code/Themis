﻿#pragma once

#include "aql_parser.h"
#include "query_engine.h"
#include <memory>
#include <string>

namespace themis {

// Bring query types into scope
using query::Query;
using query::Expression;
using query::SortNode;
using query::LimitNode;
using query::FilterNode;
using query::BinaryOpExpr;
using query::FieldAccessExpr;
using query::LiteralExpr;
using query::LiteralValue;
using query::ASTNodeType;
using query::BinaryOperator;

/**
 * Translates AQL AST to QueryEngine ConjunctiveQuery
 * 
 * Example:
 *   FOR user IN users 
 *   FILTER user.age > 18 AND user.city == "Berlin"
 *   SORT user.created_at DESC
 *   LIMIT 10
 *   RETURN user
 * 
 * Translates to:
 *   ConjunctiveQuery {
 *     table: "users",
 *     predicates: [{ column: "city", value: "Berlin" }],
 *     rangePredicates: [{ column: "age", lower: "18", includeLower: false }],
 *     orderBy: { column: "created_at", desc: true, limit: 10 }
 *   }
 */
class AQLTranslator {
public:
    struct TranslationResult {
        bool success = false;
        std::string error_message;
        ConjunctiveQuery query; // für relationale AQL (single-FOR)
        
        // Graph-Traversal Query (optional)
        struct TraversalQuery {
            enum class Direction { Outbound, Inbound, Any };
            std::string variable;
            int minDepth = 1;
            int maxDepth = 1;
            Direction direction = Direction::Outbound;
            std::string startVertex;
            std::string graphName;
        };
        std::optional<TraversalQuery> traversal;
        
        // Join Query (multi-FOR)
        struct JoinQuery {
            std::vector<query::ForNode> for_nodes;                                    // Multiple FOR clauses
            std::vector<std::shared_ptr<query::FilterNode>> filters;                  // JOIN conditions + filters
            std::vector<query::LetNode> let_nodes;                                    // LET bindings
            std::shared_ptr<query::ReturnNode> return_node;                           // RETURN expression
            std::shared_ptr<query::SortNode> sort;                                    // SORT clause
            std::shared_ptr<query::LimitNode> limit;                                  // LIMIT clause
            std::shared_ptr<query::CollectNode> collect;                       // COLLECT/GROUP BY
        };
        std::optional<JoinQuery> join;
        
        static TranslationResult Success(ConjunctiveQuery q) {
            TranslationResult r;
            r.success = true;
            r.query = std::move(q);
            return r;
        }
        
        static TranslationResult SuccessJoin(JoinQuery j) {
            TranslationResult r;
            r.success = true;
            r.join = std::move(j);
            return r;
        }
        
        static TranslationResult SuccessTraversal(TraversalQuery t) {
            TranslationResult r;
            r.success = true;
            r.traversal = std::move(t);
            return r;
        }
        
        static TranslationResult Error(std::string msg) {
            TranslationResult r;
            r.success = false;
            r.error_message = std::move(msg);
            return r;
        }
    };
    
    /**
     * Translate AQL AST to QueryEngine query
     * 
     * Limitations:
     * - Only supports conjunctive queries (AND combinations)
     * - OR requires multiple queries (future enhancement)
     * - Functions in FILTER not yet supported (e.g., LOWER(doc.name))
     */
    static TranslationResult translate(const std::shared_ptr<Query>& ast);

private:
    /**
     * Extract predicates from FILTER conditions
     * Returns false if unsupported expression found (e.g., OR, complex functions)
     */
    static bool extractPredicates(
        const std::shared_ptr<Expression>& expr,
        std::vector<PredicateEq>& eqPredicates,
        std::vector<PredicateRange>& rangePredicates,
        std::string& error
    );
    
    /**
     * Extract column name from field access expression
     * E.g., "user.age" -> "age"
     */
    static std::string extractColumnName(const std::shared_ptr<Expression>& expr);
    
    /**
     * Convert literal value to string for query engine
     */
    static std::string literalToString(const LiteralValue& value);
    
    /**
     * Extract ORDER BY from SORT clause
     */
    static std::optional<OrderBy> extractOrderBy(
        const std::shared_ptr<SortNode>& sort,
        const std::shared_ptr<LimitNode>& limit
    );
};

} // namespace themis
