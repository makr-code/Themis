// Phase 3.5: CTE and Subquery Optimization Utilities
#pragma once

#include "query/aql_parser.h"
#include <memory>
#include <string>
#include <unordered_set>

namespace themis {
namespace query {

/**
 * @brief CTE Optimization Analyzer
 * 
 * Provides heuristics for deciding when to materialize CTEs vs inline them,
 * and when to convert subqueries to JOINs.
 */
class SubqueryOptimizer {
public:
    /**
     * @brief Determines if a CTE should be materialized or inlined
     * 
     * Materialization is preferred when:
     * - CTE is referenced multiple times (> 1)
     * - CTE contains expensive operations (aggregation, sorting)
     * - CTE result set is expected to be small
     * 
     * Inlining is preferred when:
     * - CTE is referenced only once
     * - CTE is simple (no aggregation, no sorting)
     * - Inlining enables predicate pushdown
     * 
     * @param cte The CTE definition to analyze
     * @param referenceCount Number of times CTE is referenced in main query
     * @return true if should materialize, false if should inline
     */
    static bool shouldMaterializeCTE(
        const CTEDefinition& cte,
        size_t referenceCount
    ) {
        // Always materialize if referenced more than once
        if (referenceCount > 1) {
            return true;
        }
        
        // Single reference - check complexity
        if (!cte.subquery) {
            return false; // Empty CTE, inline
        }
        
        // Materialize if contains aggregation
        if (cte.subquery->collect) {
            return true;
        }
        
        // Materialize if contains sorting (expensive)
        if (cte.subquery->sort) {
            return true;
        }
        
        // Materialize if contains LIMIT (indicates small result set)
        if (cte.subquery->limit) {
            return true;
        }
        
        // Materialize if contains nested WITH clause
        if (cte.subquery->with_clause) {
            return true;
        }
        
        // Default: inline simple CTEs for predicate pushdown
        return false;
    }
    
    /**
     * @brief Detects if a subquery can be converted to a JOIN
     * 
     * Conditions for JOIN conversion:
     * - Subquery is correlated (references outer variables)
     * - Subquery is in WHERE clause (IN/EXISTS pattern)
     * - No aggregation in subquery
     * 
     * @param subquery The subquery to analyze
     * @param outerVariables Variables from outer query scope
     * @return true if can convert to JOIN
     */
    static bool canConvertToJoin(
        const std::shared_ptr<Query>& subquery,
        const std::unordered_set<std::string>& outerVariables
    ) {
        if (!subquery) {
            return false;
        }
        
        // Cannot convert if contains aggregation
        if (subquery->collect) {
            return false;
        }
        
        // Check if subquery references outer variables (correlation)
        bool isCorrelated = hasCorrelation(subquery, outerVariables);
        
        // JOIN conversion beneficial for correlated subqueries
        return isCorrelated;
    }
    
    /**
     * @brief Estimates the cost of executing a subquery
     * 
     * Simple heuristic based on query structure:
     * - Base cost: 10
     * - +50 for each JOIN (multi-FOR)
     * - +30 for aggregation
     * - +20 for sorting
     * - +10 for each filter
     * - -20 if has LIMIT (reduces result set)
     * 
     * @param query The query to estimate
     * @return Estimated cost (arbitrary units)
     */
    static int estimateQueryCost(const std::shared_ptr<Query>& query) {
        if (!query) return 0;
        
        int cost = 10; // Base cost
        
        // Multiple FORs (JOINs) are expensive
        if (query->for_nodes.size() > 1) {
            cost += 50 * static_cast<int>(query->for_nodes.size() - 1);
        }
        
        // Aggregation is expensive
        if (query->collect) {
            cost += 30;
        }
        
        // Sorting is expensive
        if (query->sort) {
            cost += 20;
        }
        
        // Each filter adds cost
        cost += 10 * static_cast<int>(query->filters.size());
        
        // LIMIT reduces result set (cost reduction)
        if (query->limit) {
            cost -= 20;
        }
        
        // Nested WITH clauses compound cost
        if (query->with_clause) {
            for (const auto& cte : query->with_clause->ctes) {
                cost += estimateQueryCost(cte.subquery);
            }
        }
        
        return std::max(1, cost); // Minimum cost of 1
    }

private:
    /**
     * @brief Checks if query references any outer variables
     */
    static bool hasCorrelation(
        const std::shared_ptr<Query>& query,
        const std::unordered_set<std::string>& outerVariables
    ) {
        if (!query || outerVariables.empty()) {
            return false;
        }
        
        // Check filters for outer variable references
        for (const auto& filter : query->filters) {
            if (filter && filter->condition) {
                if (expressionReferencesVariables(filter->condition, outerVariables)) {
                    return true;
                }
            }
        }
        
        // Check RETURN clause
        if (query->return_node && query->return_node->expression) {
            if (expressionReferencesVariables(query->return_node->expression, outerVariables)) {
                return true;
            }
        }
        
        return false;
    }
    
    /**
     * @brief Recursively checks if expression references any outer variables
     */
    static bool expressionReferencesVariables(
        const std::shared_ptr<Expression>& expr,
        const std::unordered_set<std::string>& variables
    ) {
        if (!expr) return false;
        
        switch (expr->getType()) {
            case ASTNodeType::Variable: {
                auto var = std::static_pointer_cast<VariableExpr>(expr);
                return variables.count(var->name) > 0;
            }
            
            case ASTNodeType::FieldAccess: {
                auto field = std::static_pointer_cast<FieldAccessExpr>(expr);
                return expressionReferencesVariables(field->object, variables);
            }
            
            case ASTNodeType::BinaryOp: {
                auto binOp = std::static_pointer_cast<BinaryOpExpr>(expr);
                return expressionReferencesVariables(binOp->left, variables) ||
                       expressionReferencesVariables(binOp->right, variables);
            }
            
            case ASTNodeType::UnaryOp: {
                auto unOp = std::static_pointer_cast<UnaryOpExpr>(expr);
                return expressionReferencesVariables(unOp->operand, variables);
            }
            
            case ASTNodeType::FunctionCall: {
                auto func = std::static_pointer_cast<FunctionCallExpr>(expr);
                for (const auto& arg : func->arguments) {
                    if (expressionReferencesVariables(arg, variables)) {
                        return true;
                    }
                }
                return false;
            }
            
            case ASTNodeType::ArrayLiteral: {
                auto arr = std::static_pointer_cast<ArrayLiteralExpr>(expr);
                for (const auto& elem : arr->elements) {
                    if (expressionReferencesVariables(elem, variables)) {
                        return true;
                    }
                }
                return false;
            }
            
            case ASTNodeType::ObjectConstruct: {
                auto obj = std::static_pointer_cast<ObjectConstructExpr>(expr);
                for (const auto& [key, val] : obj->fields) {
                    if (expressionReferencesVariables(val, variables)) {
                        return true;
                    }
                }
                return false;
            }
            
            default:
                return false;
        }
    }
};

} // namespace query
} // namespace themis
