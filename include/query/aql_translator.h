#pragma once

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
using query::FunctionCallExpr;
using query::BinaryOperator;
using query::UnaryOperator;
using query::UnaryOpExpr;
using query::VariableExpr;
using query::SimilarityCallExpr;
using query::ProximityCallExpr;
using query::ArrayLiteralExpr;
using query::SubqueryExpr;
using query::AnyExpr;
using query::AllExpr;

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
            bool shortestPath = false;
            std::string endVertex; // gesetzt wenn shortestPath
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
        
        // Disjunctive Query (OR support)
        std::optional<DisjunctiveQuery> disjunctive;

        // Hybrid Vector+Geo Query (SIMILARITY + ST_*)
        std::optional<VectorGeoQuery> vector_geo;
        // Hybrid Content+Geo Query (FULLTEXT + PROXIMITY + optional ST_*)
        std::optional<ContentGeoQuery> content_geo;
        
        // Phase 4: CTE execution metadata
        struct CTEExecution {
            std::string name;                          // CTE name
            std::shared_ptr<query::Query> subquery;    // AST for execution
            bool should_materialize;                   // Based on heuristic
        };
        std::vector<CTEExecution> ctes;                // CTEs to execute before main query
        
        static TranslationResult Success(ConjunctiveQuery q) {
            TranslationResult r;
            r.success = true;
            r.query = std::move(q);
            return r;
        }
        
        static TranslationResult SuccessDisjunctive(DisjunctiveQuery d) {
            TranslationResult r;
            r.success = true;
            r.disjunctive = std::move(d);
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

        static TranslationResult SuccessVectorGeo(VectorGeoQuery v) {
            TranslationResult r;
            r.success = true;
            r.vector_geo = std::move(v);
            return r;
        }
        static TranslationResult SuccessContentGeo(ContentGeoQuery c) {
            TranslationResult r; r.success = true; r.content_geo = std::move(c); return r; }
        
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
     * Supported:
     * - Conjunctive queries (AND combinations)
     * - Disjunctive queries (OR combinations in DNF)
     * - Mixed AND/OR expressions
     * 
     * Limitations:
     * - Functions in FILTER limited (FULLTEXT supported)
     */
    static TranslationResult translate(const std::shared_ptr<Query>& ast);

private:
    /**
     * Extract predicates from FILTER conditions
     * Supports AND/OR and converts to Disjunctive Normal Form (DNF)
     * Returns false if unsupported expression found
     */
    static bool extractPredicates(
        const std::shared_ptr<Expression>& expr,
        std::vector<PredicateEq>& eqPredicates,
        std::vector<PredicateRange>& rangePredicates,
        std::string& error
    );
    
    /**
     * Check if expression contains OR operator (requires DisjunctiveQuery)
     */
    static bool containsOr(const std::shared_ptr<Expression>& expr);
    
    /**
     * Convert expression to Disjunctive Normal Form (DNF)
     * Returns list of conjunctive clauses (disjuncts)
     * Example: (A AND B) OR (C AND D) -> [[A,B], [C,D]]
     */
    static std::vector<ConjunctiveQuery> convertToDNF(
        const std::shared_ptr<Expression>& expr,
        const std::string& table,
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
    
    /**
     * Count CTE references in AST (Phase 4.1)
     * Scans FOR nodes to see how many times a CTE name appears as collection
     */
    static size_t countCTEReferences(
        const std::shared_ptr<Query>& ast,
        const std::string& cte_name
    );
    
    /**
     * Count CTE references recursively in expressions (Phase 4.1)
     * Used for subqueries in FILTER, LET, etc.
     */
    static size_t countCTEReferencesInExpr(
        const std::shared_ptr<Expression>& expr,
        const std::string& cte_name
    );
    
    /**
     * Attach CTE execution metadata to translation result (Phase 4.1)
     * Helper to avoid duplicating CTE attachment logic across all return paths
     */
    static void attachCTEs(
        TranslationResult& result,
        std::vector<TranslationResult::CTEExecution> ctes
    );
};

} // namespace themis

