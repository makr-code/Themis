#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "query/aql_parser.h"

namespace themis {
namespace query {

/**
 * @brief Common Table Expression (CTE) Support für AQL
 * 
 * Unterstützt WITH-Clause für temporary named result sets:
 * 
 * WITH high_earners AS (
 *   FOR u IN users
 *   FILTER u.salary > 100000
 *   RETURN u
 * ),
 * avg_salaries AS (
 *   FOR h IN high_earners
 *   COLLECT city = h.city
 *   AGGREGATE avg_salary = AVG(h.salary)
 *   RETURN {city, avg_salary}
 * )
 * FOR a IN avg_salaries
 *   FILTER a.avg_salary > 120000
 *   RETURN a
 * 
 * Recursive CTEs (Phase 2):
 * WITH RECURSIVE org_tree AS (
 *   FOR e IN employees
 *   FILTER e.manager_id == null
 *   RETURN e
 *   UNION
 *   FOR e IN employees, o IN org_tree
 *   FILTER e.manager_id == o.id
 *   RETURN e
 * )
 * FOR o IN org_tree RETURN o
 */

/**
 * @brief CTE Definition
 */
struct CTEDefinition {
    std::string name;                          // CTE name (e.g., "high_earners")
    std::shared_ptr<Query> query;              // CTE query (full AQL query)
    bool recursive = false;                    // Recursive CTE (Phase 2)
    
    nlohmann::json toJSON() const;
};

/**
 * @brief CTE Evaluator
 */
class CTEEvaluator {
public:
    CTEEvaluator() = default;
    
    /**
     * @brief Evaluiert eine CTE und speichert Resultate
     * @param cte Die CTE-Definition
     * @param queryEngine Query Engine für Sub-Query Execution
     * @return true wenn erfolgreich
     */
    bool evaluateCTE(
        const CTEDefinition& cte,
        class QueryEngine& queryEngine
    );
    
    /**
     * @brief Holt gespeicherte CTE-Resultate
     * @param cteName CTE Name
     * @return CTE Results (JSON array) oder empty wenn nicht vorhanden
     */
    std::vector<nlohmann::json> getCTEResults(const std::string& cteName) const;
    
    /**
     * @brief Prüft ob CTE existiert
     * @param cteName CTE Name
     * @return true wenn CTE evaluiert wurde
     */
    bool hasCTE(const std::string& cteName) const;
    
    /**
     * @brief Löscht alle CTE Results (nach Query-Completion)
     */
    void clear();
    
private:
    // CTE Name → Results (materialized as JSON array)
    std::unordered_map<std::string, std::vector<nlohmann::json>> cteResults_;
};

/**
 * @brief Subquery Support für AQL
 * 
 * Unterstützt:
 * 1. Scalar Subqueries:
 *    FOR u IN users
 *    FILTER u.salary > (FOR a IN salaries RETURN AVG(a.value))
 *    RETURN u
 * 
 * 2. IN Subqueries:
 *    FOR u IN users
 *    FILTER u.id IN (FOR o IN orders FILTER o.status == "active" RETURN o.user_id)
 *    RETURN u
 * 
 * 3. EXISTS Subqueries:
 *    FOR u IN users
 *    FILTER EXISTS(FOR o IN orders FILTER o.user_id == u.id RETURN 1)
 *    RETURN u
 * 
 * 4. Correlated Subqueries:
 *    FOR u IN users
 *    RETURN {
 *      name: u.name,
 *      order_count: (FOR o IN orders FILTER o.user_id == u.id RETURN COUNT())
 *    }
 */

/**
 * @brief Subquery Type
 */
enum class SubqueryType {
    SCALAR,        // Returns single value: (SELECT ...)
    IN,            // Returns set: x IN (SELECT ...)
    EXISTS,        // Existence check: EXISTS (SELECT ...)
    NOT_EXISTS     // Non-existence: NOT EXISTS (SELECT ...)
};

/**
 * @brief Subquery Expression
 */
struct SubqueryExpr : Expression {
    SubqueryType type;
    std::shared_ptr<Query> query;         // Subquery (full AQL query)
    bool correlated = false;              // Correlated (references outer query variables)
    
    // For correlated subqueries: outer variable bindings
    std::unordered_map<std::string, nlohmann::json> outerBindings;
    
    ASTNodeType getType() const override {
        // Reuse existing type or define new one
        return ASTNodeType::FunctionCall;  // Temporary: use FunctionCall type
    }
    
    nlohmann::json toJSON() const override;
};

/**
 * @brief Subquery Evaluator
 */
class SubqueryEvaluator {
public:
    SubqueryEvaluator() = default;
    
    /**
     * @brief Evaluiert eine Subquery
     * @param subquery Subquery Definition
     * @param queryEngine Query Engine für Execution
     * @param outerRow Outer Row (für correlated subqueries)
     * @return Subquery Result (scalar value, array, or boolean)
     */
    nlohmann::json evaluateSubquery(
        const SubqueryExpr& subquery,
        class QueryEngine& queryEngine,
        const nlohmann::json& outerRow = nlohmann::json()
    );
    
    /**
     * @brief Evaluiert SCALAR Subquery (returns single value)
     * @param query Subquery
     * @param queryEngine Query Engine
     * @param outerRow Outer Row
     * @return Scalar value oder null
     */
    nlohmann::json evaluateScalarSubquery(
        const std::shared_ptr<Query>& query,
        class QueryEngine& queryEngine,
        const nlohmann::json& outerRow
    );
    
    /**
     * @brief Evaluiert IN Subquery (returns set)
     * @param value Value to check
     * @param query Subquery
     * @param queryEngine Query Engine
     * @param outerRow Outer Row
     * @return true wenn value in result set
     */
    bool evaluateInSubquery(
        const nlohmann::json& value,
        const std::shared_ptr<Query>& query,
        class QueryEngine& queryEngine,
        const nlohmann::json& outerRow
    );
    
    /**
     * @brief Evaluiert EXISTS Subquery
     * @param query Subquery
     * @param queryEngine Query Engine
     * @param outerRow Outer Row
     * @return true wenn Subquery mindestens ein Result liefert
     */
    bool evaluateExistsSubquery(
        const std::shared_ptr<Query>& query,
        class QueryEngine& queryEngine,
        const nlohmann::json& outerRow
    );
    
private:
    /**
     * @brief Bindet Outer Variables in Subquery Context
     * @param query Subquery
     * @param outerRow Outer Row
     */
    void bindOuterVariables(
        const std::shared_ptr<Query>& query,
        const nlohmann::json& outerRow
    );
};

} // namespace query
} // namespace themis
