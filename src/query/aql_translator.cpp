#include "query/aql_translator.h"
#include <sstream>
#include <variant>

namespace themis {

AQLTranslator::TranslationResult AQLTranslator::translate(const std::shared_ptr<Query>& ast) {
    if (!ast) {
        return TranslationResult::Error("Null AST provided");
    }
    // Graph-Traversal Unterstützung: Wenn Traversal-Klausel vorhanden ist,
    // übersetzen wir in eine TraversalQuery und umgehen die relationale Pfadlogik.
    if (ast->traversal) {
        TranslationResult::TraversalQuery tq;
        tq.variable = ast->traversal->varVertex;
        tq.minDepth = ast->traversal->minDepth;
        tq.maxDepth = ast->traversal->maxDepth;
        // Mappe Richtung
        switch (ast->traversal->direction) {
            case Query::TraversalNode::Direction::Outbound: tq.direction = TranslationResult::TraversalQuery::Direction::Outbound; break;
            case Query::TraversalNode::Direction::Inbound:  tq.direction = TranslationResult::TraversalQuery::Direction::Inbound;  break;
            case Query::TraversalNode::Direction::Any:      tq.direction = TranslationResult::TraversalQuery::Direction::Any;      break;
        }
        tq.startVertex = ast->traversal->startVertex;
        tq.graphName = ast->traversal->graphName;
        return TranslationResult::SuccessTraversal(std::move(tq));
    }

    ConjunctiveQuery query;
    query.table = ast->for_node.collection;
    
    // Process FILTER clauses
    std::string error;
    for (const auto& filter : ast->filters) {
        if (!filter || !filter->condition) {
            return TranslationResult::Error("Invalid filter node");
        }
        
        if (!extractPredicates(filter->condition, query.predicates, query.rangePredicates, error)) {
            return TranslationResult::Error("Filter translation failed: " + error);
        }
    }
    
    // Process SORT + LIMIT
    if (ast->sort) {
        query.orderBy = extractOrderBy(ast->sort, ast->limit);
    }
    
    return TranslationResult::Success(std::move(query));
}

bool AQLTranslator::extractPredicates(
    const std::shared_ptr<Expression>& expr,
    std::vector<PredicateEq>& eqPredicates,
    std::vector<PredicateRange>& rangePredicates,
    std::string& error
) {
    if (!expr) {
        error = "Null expression";
        return false;
    }
    
    // Check expression type
    if (expr->getType() == ASTNodeType::BinaryOp) {
        auto binOp = std::static_pointer_cast<BinaryOpExpr>(expr);
        
        // Handle AND: Recursively extract from both sides
        if (binOp->op == BinaryOperator::And) {
            return extractPredicates(binOp->left, eqPredicates, rangePredicates, error) &&
                   extractPredicates(binOp->right, eqPredicates, rangePredicates, error);
        }
        
        // Handle OR: Not supported in ConjunctiveQuery
        if (binOp->op == BinaryOperator::Or) {
            error = "OR operator not supported in conjunctive queries (use multiple queries instead)";
            return false;
        }
        
        // Extract column name from left side (must be field access)
        if (binOp->left->getType() != ASTNodeType::FieldAccess) {
            error = "Left side of comparison must be field access (e.g., doc.age)";
            return false;
        }
        
        std::string column = extractColumnName(binOp->left);
        
        // Extract value from right side (must be literal)
        if (binOp->right->getType() != ASTNodeType::Literal) {
            error = "Right side of comparison must be literal value";
            return false;
        }
        
        auto literal = std::static_pointer_cast<LiteralExpr>(binOp->right);
        std::string value = literalToString(literal->value);
        
        // Map operator to predicate type
        switch (binOp->op) {
            case BinaryOperator::Eq:
                eqPredicates.push_back(PredicateEq{column, value});
                break;
                
            case BinaryOperator::Neq:
                error = "NEQ operator not yet supported";
                return false;
                
            case BinaryOperator::Lt:
                rangePredicates.push_back(PredicateRange{
                    column,
                    std::nullopt,   // no lower bound
                    value,          // upper bound
                    true,           // include lower (doesn't matter)
                    false           // exclude upper (< not <=)
                });
                break;
                
            case BinaryOperator::Lte:
                rangePredicates.push_back(PredicateRange{
                    column,
                    std::nullopt,   // no lower bound
                    value,          // upper bound
                    true,           // include lower (doesn't matter)
                    true            // include upper (<=)
                });
                break;
                
            case BinaryOperator::Gt:
                rangePredicates.push_back(PredicateRange{
                    column,
                    value,          // lower bound
                    std::nullopt,   // no upper bound
                    false,          // exclude lower (> not >=)
                    true            // include upper (doesn't matter)
                });
                break;
                
            case BinaryOperator::Gte:
                rangePredicates.push_back(PredicateRange{
                    column,
                    value,          // lower bound
                    std::nullopt,   // no upper bound
                    true,           // include lower (>=)
                    true            // include upper (doesn't matter)
                });
                break;
                
            default:
                error = "Unsupported operator in filter";
                return false;
        }
        
        return true;
    }
    
    error = "Unsupported expression type in filter (only binary operators supported)";
    return false;
}

std::string AQLTranslator::extractColumnName(const std::shared_ptr<Expression>& expr) {
    if (expr->getType() == ASTNodeType::FieldAccess) {
        auto fieldAccess = std::static_pointer_cast<FieldAccessExpr>(expr);
        
        // Handle nested field access: doc.address.city -> "address.city"
        std::string result;
        
        // Recursively extract parent field names
        if (fieldAccess->object->getType() == ASTNodeType::FieldAccess) {
            result = extractColumnName(fieldAccess->object) + ".";
        }
        // Skip the variable name (e.g., "doc") at the root
        
        result += fieldAccess->field;
        return result;
    }
    
    return "";
}

std::string AQLTranslator::literalToString(const LiteralValue& value) {
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
            return "null";
        } else if constexpr (std::is_same_v<T, bool>) {
            return arg ? "true" : "false";
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, double>) {
            return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return arg;
        }
        
        return "";
    }, value);
}

std::optional<OrderBy> AQLTranslator::extractOrderBy(
    const std::shared_ptr<SortNode>& sort,
    const std::shared_ptr<LimitNode>& limit
) {
    if (!sort || sort->specifications.empty()) {
        return std::nullopt;
    }
    
    // Only support single-column sorting for now
    const auto& spec = sort->specifications[0];
    
    OrderBy orderBy;
    orderBy.column = extractColumnName(spec.expression);
    orderBy.desc = !spec.ascending;
    
    // Apply limit if present
    if (limit) {
        orderBy.limit = limit->count;
        // Note: offset not supported in OrderBy (would need post-processing)
    } else {
        orderBy.limit = 1000; // default limit
    }
    
    return orderBy;
}

} // namespace themis
