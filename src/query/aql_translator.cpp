#include "query/aql_translator.h"
#include <sstream>
#include <variant>

namespace themis {

namespace {

bool isComparisonOperator(BinaryOperator op) {
    switch (op) {
        case BinaryOperator::Eq:
        case BinaryOperator::Neq:
        case BinaryOperator::Lt:
        case BinaryOperator::Lte:
        case BinaryOperator::Gt:
        case BinaryOperator::Gte:
            return true;
        default:
            return false;
    }
}

std::shared_ptr<Expression> negateComparison(
    BinaryOperator op,
    const std::shared_ptr<Expression>& left,
    const std::shared_ptr<Expression>& right,
    bool& unsupported
) {
    switch (op) {
        case BinaryOperator::Eq: {
            auto less = std::make_shared<BinaryOpExpr>(BinaryOperator::Lt, left, right);
            auto greater = std::make_shared<BinaryOpExpr>(BinaryOperator::Gt, left, right);
            return std::make_shared<BinaryOpExpr>(BinaryOperator::Or, less, greater);
        }
        case BinaryOperator::Lt:
            return std::make_shared<BinaryOpExpr>(BinaryOperator::Gte, left, right);
        case BinaryOperator::Lte:
            return std::make_shared<BinaryOpExpr>(BinaryOperator::Gt, left, right);
        case BinaryOperator::Gt:
            return std::make_shared<BinaryOpExpr>(BinaryOperator::Lte, left, right);
        case BinaryOperator::Gte:
            return std::make_shared<BinaryOpExpr>(BinaryOperator::Lt, left, right);
        case BinaryOperator::Neq:
            return std::make_shared<BinaryOpExpr>(BinaryOperator::Eq, left, right);
        default:
            unsupported = true;
            return nullptr;
    }
}

std::shared_ptr<Expression> rewriteNegationsImpl(
    const std::shared_ptr<Expression>& expr,
    bool negate,
    bool& unsupported
) {
    if (unsupported || !expr) {
        if (!expr) unsupported = true;
        return nullptr;
    }

    switch (expr->getType()) {
        case ASTNodeType::Literal: {
            if (!negate) return expr;
            auto literal = std::static_pointer_cast<LiteralExpr>(expr);
            if (std::holds_alternative<bool>(literal->value)) {
                bool value = std::get<bool>(literal->value);
                return std::make_shared<LiteralExpr>(LiteralValue(!value));
            }
            unsupported = true;
            return nullptr;
        }
        case ASTNodeType::Variable:
        case ASTNodeType::FieldAccess:
            if (negate) {
                unsupported = true;
                return nullptr;
            }
            return expr;
        case ASTNodeType::UnaryOp: {
            auto unary = std::static_pointer_cast<UnaryOpExpr>(expr);
            if (unary->op == UnaryOperator::Not) {
                return rewriteNegationsImpl(unary->operand, !negate, unsupported);
            }
            auto operand = rewriteNegationsImpl(unary->operand, false, unsupported);
            if (unsupported || !operand) return nullptr;
            if (negate) {
                unsupported = true;
                return nullptr;
            }
            if (operand == unary->operand) return expr;
            return std::make_shared<UnaryOpExpr>(unary->op, operand);
        }
        case ASTNodeType::BinaryOp: {
            auto bin = std::static_pointer_cast<BinaryOpExpr>(expr);

            if (bin->op == BinaryOperator::And || bin->op == BinaryOperator::Or) {
                if (negate) {
                    auto left = rewriteNegationsImpl(bin->left, true, unsupported);
                    auto right = rewriteNegationsImpl(bin->right, true, unsupported);
                    if (unsupported || !left || !right) return nullptr;
                    auto newOp = (bin->op == BinaryOperator::And) ? BinaryOperator::Or : BinaryOperator::And;
                    return std::make_shared<BinaryOpExpr>(newOp, left, right);
                }
                auto left = rewriteNegationsImpl(bin->left, false, unsupported);
                auto right = rewriteNegationsImpl(bin->right, false, unsupported);
                if (unsupported || !left || !right) return nullptr;
                if (left == bin->left && right == bin->right) return expr;
                return std::make_shared<BinaryOpExpr>(bin->op, left, right);
            }

            if (isComparisonOperator(bin->op)) {
                auto left = rewriteNegationsImpl(bin->left, false, unsupported);
                auto right = rewriteNegationsImpl(bin->right, false, unsupported);
                if (unsupported || !left || !right) return nullptr;
                if (!negate) {
                    if (left == bin->left && right == bin->right) return expr;
                    return std::make_shared<BinaryOpExpr>(bin->op, left, right);
                }
                return negateComparison(bin->op, left, right, unsupported);
            }

            auto left = rewriteNegationsImpl(bin->left, false, unsupported);
            auto right = rewriteNegationsImpl(bin->right, false, unsupported);
            if (unsupported || !left || !right) return nullptr;
            if (negate) {
                unsupported = true;
                return nullptr;
            }
            if (left == bin->left && right == bin->right) return expr;
            return std::make_shared<BinaryOpExpr>(bin->op, left, right);
        }
        case ASTNodeType::FunctionCall: {
            if (negate) {
                unsupported = true;
                return nullptr;
            }
            auto fn = std::static_pointer_cast<FunctionCallExpr>(expr);
            bool changed = false;
            std::vector<std::shared_ptr<Expression>> newArgs;
            newArgs.reserve(fn->arguments.size());
            for (const auto& arg : fn->arguments) {
                auto rewrittenArg = rewriteNegationsImpl(arg, false, unsupported);
                if (unsupported || !rewrittenArg) return nullptr;
                changed = changed || (rewrittenArg != arg);
                newArgs.push_back(rewrittenArg);
            }
            if (!changed) return expr;
            return std::make_shared<FunctionCallExpr>(fn->name, std::move(newArgs));
        }
        case ASTNodeType::ArrayLiteral: {
            if (negate) {
                unsupported = true;
                return nullptr;
            }
            auto arr = std::static_pointer_cast<ArrayLiteralExpr>(expr);
            bool changed = false;
            std::vector<std::shared_ptr<Expression>> elements;
            elements.reserve(arr->elements.size());
            for (const auto& el : arr->elements) {
                auto rewrittenEl = rewriteNegationsImpl(el, false, unsupported);
                if (unsupported || !rewrittenEl) return nullptr;
                changed = changed || (rewrittenEl != el);
                elements.push_back(rewrittenEl);
            }
            if (!changed) return expr;
            return std::make_shared<ArrayLiteralExpr>(std::move(elements));
        }
        case ASTNodeType::ObjectConstruct: {
            if (negate) {
                unsupported = true;
                return nullptr;
            }
            auto obj = std::static_pointer_cast<ObjectConstructExpr>(expr);
            bool changed = false;
            std::vector<std::pair<std::string, std::shared_ptr<Expression>>> fields;
            fields.reserve(obj->fields.size());
            for (const auto& field : obj->fields) {
                auto rewrittenValue = rewriteNegationsImpl(field.second, false, unsupported);
                if (unsupported || !rewrittenValue) return nullptr;
                changed = changed || (rewrittenValue != field.second);
                fields.emplace_back(field.first, rewrittenValue);
            }
            if (!changed) return expr;
            return std::make_shared<ObjectConstructExpr>(std::move(fields));
        }
        default:
            unsupported = true;
            return nullptr;
    }
}

} // namespace

AQLTranslator::RewriteResult AQLTranslator::rewriteNegationsForPlanner(const std::shared_ptr<Expression>& expr) {
    bool unsupported = false;
    auto rewritten = rewriteNegationsImpl(expr, false, unsupported);
    if (unsupported || !rewritten) {
        return RewriteResult{expr, false};
    }
    return RewriteResult{rewritten, true};
}

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
    
    // Multi-FOR Join: Wenn mehr als eine FOR-Klausel vorhanden ist, als Join behandeln
    if (ast->for_nodes.size() > 1 || !ast->let_nodes.empty() || ast->collect) {
        TranslationResult::JoinQuery jq;
        jq.for_nodes = ast->for_nodes;
        jq.filters = ast->filters;
        jq.let_nodes = ast->let_nodes;
        jq.return_node = ast->return_node;
        jq.sort = ast->sort;
        jq.limit = ast->limit;
        jq.collect = ast->collect;
        return TranslationResult::SuccessJoin(std::move(jq));
    }

    // Single-FOR Query: Standard ConjunctiveQuery für einfache Queries
    ConjunctiveQuery query;
    query.table = ast->for_node.collection;
    
    // Process FILTER clauses
    std::string error;

    std::vector<std::shared_ptr<Expression>> plannerFilters(ast->filters.size());
    std::vector<bool> filterPushdown(ast->filters.size(), false);

    for (size_t i = 0; i < ast->filters.size(); ++i) {
        const auto& filter = ast->filters[i];
        if (!filter || !filter->condition) {
            return TranslationResult::Error("Invalid filter node");
        }
        auto rewrite = rewriteNegationsForPlanner(filter->condition);
        plannerFilters[i] = rewrite.expression ? rewrite.expression : filter->condition;
        filterPushdown[i] = rewrite.supported;
    }

    // Check if any filter contains OR - if so, use DisjunctiveQuery
    bool hasOr = false;
    for (size_t i = 0; i < ast->filters.size(); ++i) {
        const auto& exprForCheck = filterPushdown[i] ? plannerFilters[i] : ast->filters[i]->condition;
        if (exprForCheck && containsOr(exprForCheck)) {
            hasOr = true;
            break;
        }
    }
    
    if (hasOr) {
        // Build DisjunctiveQuery using DNF conversion across ALL filters (AND-merge between filters)
        DisjunctiveQuery disjQuery;
        disjQuery.table = ast->for_node.collection;

        auto mergeConj = [&](const ConjunctiveQuery& a, const ConjunctiveQuery& b) -> std::optional<ConjunctiveQuery> {
            ConjunctiveQuery merged; merged.table = ast->for_node.collection;
            // Merge equality predicates
            merged.predicates = a.predicates;
            merged.predicates.insert(merged.predicates.end(), b.predicates.begin(), b.predicates.end());
            // Merge ranges
            merged.rangePredicates = a.rangePredicates;
            merged.rangePredicates.insert(merged.rangePredicates.end(), b.rangePredicates.begin(), b.rangePredicates.end());
            // Merge FULLTEXT: allow at most one
            if (a.fulltextPredicate.has_value() && b.fulltextPredicate.has_value()) {
                return std::nullopt; // invalid: two FULLTEXT in same conjunct
            }
            if (a.fulltextPredicate.has_value()) merged.fulltextPredicate = a.fulltextPredicate;
            if (b.fulltextPredicate.has_value()) merged.fulltextPredicate = b.fulltextPredicate;
            return merged;
        };

        bool first = true;
        std::vector<ConjunctiveQuery> acc;
        for (size_t i = 0; i < ast->filters.size(); ++i) {
            std::vector<ConjunctiveQuery> currentParts;
            if (!filterPushdown[i]) {
                ConjunctiveQuery neutral; neutral.table = disjQuery.table;
                currentParts.push_back(std::move(neutral));
            } else {
                currentParts = convertToDNF(plannerFilters[i], disjQuery.table, error);
                if (!error.empty()) {
                    return TranslationResult::Error("OR filter translation failed: " + error);
                }
            }

            if (first) {
                acc = std::move(currentParts);
                first = false;
            } else {
                // AND-merge: cartesian product between acc and currentParts
                std::vector<ConjunctiveQuery> mergedList;
                for (const auto& l : acc) {
                    for (const auto& r : currentParts) {
                        auto m = mergeConj(l, r);
                        if (!m.has_value()) {
                            return TranslationResult::Error("Cannot combine multiple FULLTEXT() predicates in AND - only one per clause allowed");
                        }
                        mergedList.push_back(std::move(*m));
                    }
                }
                acc.swap(mergedList);
            }
        }
        disjQuery.disjuncts = std::move(acc);

        // Process SORT + LIMIT
        if (ast->sort) {
            disjQuery.orderBy = extractOrderBy(ast->sort, ast->limit);
        }
        return TranslationResult::SuccessDisjunctive(std::move(disjQuery));
    }
    
    // No OR: Standard conjunctive query
    for (size_t idx = 0; idx < ast->filters.size(); ++idx) {
        if (!filterPushdown[idx]) {
            continue;
        }

        const auto& condition = plannerFilters[idx];
        if (!condition) {
            continue;
        }

        // Skip NOT filters entirely (runtime evaluation via post-filter)
        if (condition->getType() == ASTNodeType::UnaryOp) {
            auto* u = static_cast<UnaryOpExpr*>(condition.get());
            if (u->op == UnaryOperator::Not) {
                continue; // do not attempt push-down translation
            }
        }
        
        // Check if filter is a FULLTEXT function call
        if (condition->getType() == ASTNodeType::FunctionCall) {
            auto funcCall = std::static_pointer_cast<FunctionCallExpr>(condition);
            std::string funcName = funcCall->name;
            std::transform(funcName.begin(), funcName.end(), funcName.begin(), ::tolower);
            
            if (funcName == "fulltext") {
                // Parse FULLTEXT(column, query [, limit])
                if (funcCall->arguments.size() < 2 || funcCall->arguments.size() > 3) {
                    return TranslationResult::Error("FULLTEXT() requires 2-3 arguments: FULLTEXT(column, query [, limit])");
                }
                
                // Extract column (must be field access: doc.column)
                if (funcCall->arguments[0]->getType() != ASTNodeType::FieldAccess) {
                    return TranslationResult::Error("FULLTEXT() first argument must be field access (e.g., doc.content)");
                }
                std::string column = extractColumnName(funcCall->arguments[0]);
                
                // Extract query string (must be literal)
                if (funcCall->arguments[1]->getType() != ASTNodeType::Literal) {
                    return TranslationResult::Error("FULLTEXT() second argument must be string literal");
                }
                auto queryLiteral = std::static_pointer_cast<LiteralExpr>(funcCall->arguments[1]);
                if (!std::holds_alternative<std::string>(queryLiteral->value)) {
                    return TranslationResult::Error("FULLTEXT() query must be a string");
                }
                std::string queryStr = std::get<std::string>(queryLiteral->value);
                
                // Extract optional limit
                size_t limit = 1000; // default
                if (funcCall->arguments.size() == 3) {
                    if (funcCall->arguments[2]->getType() != ASTNodeType::Literal) {
                        return TranslationResult::Error("FULLTEXT() third argument (limit) must be integer literal");
                    }
                    auto limitLiteral = std::static_pointer_cast<LiteralExpr>(funcCall->arguments[2]);
                    if (std::holds_alternative<int64_t>(limitLiteral->value)) {
                        limit = static_cast<size_t>(std::get<int64_t>(limitLiteral->value));
                    } else {
                        return TranslationResult::Error("FULLTEXT() limit must be an integer");
                    }
                }
                
                // Set fulltext predicate
                query.fulltextPredicate = PredicateFulltext{column, queryStr, limit};
                continue; // Skip normal predicate extraction for this filter
            }
        }
        
        // Check if filter contains FULLTEXT combined with AND
        // Helper to recursively find FULLTEXT in AND tree
        std::function<std::shared_ptr<FunctionCallExpr>(const std::shared_ptr<Expression>&)> findFulltext;
        findFulltext = [&](const std::shared_ptr<Expression>& e) -> std::shared_ptr<FunctionCallExpr> {
            if (!e) return nullptr;
            
            if (e->getType() == ASTNodeType::FunctionCall) {
                auto fc = std::static_pointer_cast<FunctionCallExpr>(e);
                std::string name = fc->name;
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                if (name == "fulltext") return fc;
            }
            
            if (e->getType() == ASTNodeType::BinaryOp) {
                auto bo = std::static_pointer_cast<BinaryOpExpr>(e);
                if (bo->op == BinaryOperator::And) {
                    auto left = findFulltext(bo->left);
                    if (left) return left;
                    return findFulltext(bo->right);
                }
            }
            
            return nullptr;
        };
        
        // Helper to collect all non-FULLTEXT predicates from AND tree
        std::function<void(const std::shared_ptr<Expression>&, std::vector<std::shared_ptr<Expression>>&)> collectNonFulltext;
        collectNonFulltext = [&](const std::shared_ptr<Expression>& e, std::vector<std::shared_ptr<Expression>>& preds) {
            if (!e) return;

            // Ignore NOT subtrees completely (operand evaluated at runtime)
            if (e->getType() == ASTNodeType::UnaryOp) {
                auto* u = static_cast<UnaryOpExpr*>(e.get());
                if (u->op == UnaryOperator::Not) return; // skip
            }
            
            if (e->getType() == ASTNodeType::FunctionCall) {
                auto fc = std::static_pointer_cast<FunctionCallExpr>(e);
                std::string name = fc->name;
                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                if (name != "fulltext") {
                    preds.push_back(e); // Non-FULLTEXT function
                }
                // Skip FULLTEXT itself
                return;
            }
            
            if (e->getType() == ASTNodeType::BinaryOp) {
                auto bo = std::static_pointer_cast<BinaryOpExpr>(e);
                if (bo->op == BinaryOperator::And) {
                    collectNonFulltext(bo->left, preds);
                    collectNonFulltext(bo->right, preds);
                    return;
                }
            }
            
            // Leaf predicate (equality, range, etc.)
            preds.push_back(e);
        };
        
        if (condition->getType() == ASTNodeType::BinaryOp) {
            auto binOp = std::static_pointer_cast<BinaryOpExpr>(condition);
            
            if (binOp->op == BinaryOperator::And) {
                auto fulltextFunc = findFulltext(condition);
                
                if (fulltextFunc) {
                    // Parse FULLTEXT part
                    if (fulltextFunc->arguments.size() < 2 || fulltextFunc->arguments.size() > 3) {
                        return TranslationResult::Error("FULLTEXT() requires 2-3 arguments");
                    }
                    
                    if (fulltextFunc->arguments[0]->getType() != ASTNodeType::FieldAccess) {
                        return TranslationResult::Error("FULLTEXT() first argument must be field access");
                    }
                    std::string column = extractColumnName(fulltextFunc->arguments[0]);
                    
                    if (fulltextFunc->arguments[1]->getType() != ASTNodeType::Literal) {
                        return TranslationResult::Error("FULLTEXT() second argument must be string literal");
                    }
                    auto queryLiteral = std::static_pointer_cast<LiteralExpr>(fulltextFunc->arguments[1]);
                    if (!std::holds_alternative<std::string>(queryLiteral->value)) {
                        return TranslationResult::Error("FULLTEXT() query must be a string");
                    }
                    std::string queryStr = std::get<std::string>(queryLiteral->value);
                    
                    size_t limit = 1000;
                    if (fulltextFunc->arguments.size() == 3) {
                        if (fulltextFunc->arguments[2]->getType() != ASTNodeType::Literal) {
                            return TranslationResult::Error("FULLTEXT() limit must be integer literal");
                        }
                        auto limitLiteral = std::static_pointer_cast<LiteralExpr>(fulltextFunc->arguments[2]);
                        if (std::holds_alternative<int64_t>(limitLiteral->value)) {
                            limit = static_cast<size_t>(std::get<int64_t>(limitLiteral->value));
                        } else {
                            return TranslationResult::Error("FULLTEXT() limit must be an integer");
                        }
                    }
                    
                    query.fulltextPredicate = PredicateFulltext{column, queryStr, limit};
                    
                    // Collect all non-FULLTEXT predicates
                    std::vector<std::shared_ptr<Expression>> predicateExprs;
                    collectNonFulltext(condition, predicateExprs);
                    
                    // Extract each predicate
                    for (const auto& predExpr : predicateExprs) {
                        if (!extractPredicates(predExpr, query.predicates, query.rangePredicates, error)) {
                            return TranslationResult::Error("Filter translation failed: " + error);
                        }
                    }
                    
                    continue; // Successfully handled FULLTEXT AND <predicates>
                }
            }
        }
        
        if (!extractPredicates(condition, query.predicates, query.rangePredicates, error)) {
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

    // Skip NOT operand for push-down (evaluated at runtime)
    if (expr->getType() == ASTNodeType::UnaryOp) {
        auto u = std::static_pointer_cast<UnaryOpExpr>(expr);
        if (u->op == UnaryOperator::Not) {
            return true; // treat as success without adding predicates
        }
    }
    
    // Check for FULLTEXT function call in FILTER
    if (expr->getType() == ASTNodeType::FunctionCall) {
        auto funcCall = std::static_pointer_cast<FunctionCallExpr>(expr);
        
        // Check if it's FULLTEXT(...) - case insensitive
        std::string funcName = funcCall->name;
        std::transform(funcName.begin(), funcName.end(), funcName.begin(), ::tolower);
        
        if (funcName == "fulltext") {
            // FULLTEXT is now allowed in both AND and OR combinations
            // In OR: each disjunct can have its own FULLTEXT
            // In AND: handled at translate level
            // When called from extractPredicates in OR context, we shouldn't reach here
            // (DNF conversion handles FULLTEXT directly)
            error = "FULLTEXT() should be handled by DNF converter in OR context";
            return false;
        }
    }
    
    // Check expression type
    if (expr->getType() == ASTNodeType::BinaryOp) {
        auto binOp = std::static_pointer_cast<BinaryOpExpr>(expr);
        
        // Handle AND: Recursively extract from both sides
        if (binOp->op == BinaryOperator::And) {
            return extractPredicates(binOp->left, eqPredicates, rangePredicates, error) &&
                   extractPredicates(binOp->right, eqPredicates, rangePredicates, error);
        }
        
        // Handle OR: Should be handled at higher level via convertToDNF
        // If we reach here, it means OR is nested in a way that requires DNF conversion
        if (binOp->op == BinaryOperator::Or) {
            error = "OR operator detected - should be handled via DisjunctiveQuery (internal error)";
            return false;
        }
        
        if (binOp->op == BinaryOperator::Xor) {
            error = "XOR operator not supported";
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
        // For offset support, request offset+count from index scan and slice later in handler
        auto off = static_cast<size_t>(std::max<int64_t>(0, limit->offset));
        auto cnt = static_cast<size_t>(std::max<int64_t>(0, limit->count));
        orderBy.limit = off + cnt;
    } else {
        orderBy.limit = 1000; // default limit
    }
    
    return orderBy;
}

bool AQLTranslator::containsOr(const std::shared_ptr<Expression>& expr) {
    if (!expr) return false;
    
    if (expr->getType() == ASTNodeType::BinaryOp) {
        auto binOp = std::static_pointer_cast<BinaryOpExpr>(expr);
        if (binOp->op == BinaryOperator::Or) {
            return true;
        }
        // Recursively check both sides
        return containsOr(binOp->left) || containsOr(binOp->right);
    }
    
    return false;
}

bool AQLTranslator::containsFulltext(const std::shared_ptr<Expression>& expr) {
    if (!expr) return false;
    
    if (expr->getType() == ASTNodeType::FunctionCall) {
        auto funcCall = std::static_pointer_cast<FunctionCallExpr>(expr);
        std::string funcName = funcCall->name;
        std::transform(funcName.begin(), funcName.end(), funcName.begin(), ::tolower);
        if (funcName == "fulltext") {
            return true;
        }
    }
    
    if (expr->getType() == ASTNodeType::BinaryOp) {
        auto binOp = std::static_pointer_cast<BinaryOpExpr>(expr);
        return containsFulltext(binOp->left) || containsFulltext(binOp->right);
    }
    
    return false;
}

std::vector<ConjunctiveQuery> AQLTranslator::convertToDNF(
    const std::shared_ptr<Expression>& expr,
    const std::string& table,
    std::string& error
) {
    if (!expr) {
        error = "Null expression in DNF conversion";
        return {};
    }
    
    // Base case: Single predicate / logical op
    if (expr->getType() == ASTNodeType::BinaryOp) {
        auto binOp = std::static_pointer_cast<BinaryOpExpr>(expr);
        
        // OR: Split into multiple disjuncts
        if (binOp->op == BinaryOperator::Or) {
            // FULLTEXT is now supported in OR expressions (each disjunct can have FULLTEXT)
            auto leftDNF = convertToDNF(binOp->left, table, error);
            if (!error.empty()) return {};
            
            auto rightDNF = convertToDNF(binOp->right, table, error);
            if (!error.empty()) return {};
            
            // Merge disjuncts (union)
            leftDNF.insert(leftDNF.end(), rightDNF.begin(), rightDNF.end());
            return leftDNF;
        }
        
        // AND: Distribute over existing disjuncts
        if (binOp->op == BinaryOperator::And) {
            auto leftDNF = convertToDNF(binOp->left, table, error);
            if (!error.empty()) return {};
            
            auto rightDNF = convertToDNF(binOp->right, table, error);
            if (!error.empty()) return {};
            
            // Cartesian product: (A OR B) AND (C OR D) = (A AND C) OR (A AND D) OR (B AND C) OR (B AND D)
            std::vector<ConjunctiveQuery> result;
            for (const auto& leftConj : leftDNF) {
                for (const auto& rightConj : rightDNF) {
                    ConjunctiveQuery merged;
                    merged.table = table;
                    
                    // Merge predicates
                    merged.predicates = leftConj.predicates;
                    merged.predicates.insert(merged.predicates.end(), 
                                            rightConj.predicates.begin(), 
                                            rightConj.predicates.end());
                    
                    // Merge range predicates
                    merged.rangePredicates = leftConj.rangePredicates;
                    merged.rangePredicates.insert(merged.rangePredicates.end(),
                                                 rightConj.rangePredicates.begin(),
                                                 rightConj.rangePredicates.end());
                    
                    // Merge fulltext predicates
                    // Only one FULLTEXT per disjunct allowed (can't merge multiple FULLTEXT into single AND clause)
                    if (leftConj.fulltextPredicate.has_value() && rightConj.fulltextPredicate.has_value()) {
                        error = "Cannot combine multiple FULLTEXT() predicates in AND - only one FULLTEXT per clause allowed";
                        return {};
                    }
                    if (leftConj.fulltextPredicate.has_value()) {
                        merged.fulltextPredicate = leftConj.fulltextPredicate;
                    } else if (rightConj.fulltextPredicate.has_value()) {
                        merged.fulltextPredicate = rightConj.fulltextPredicate;
                    }
                    
                    result.push_back(std::move(merged));
                }
            }
            return result;
        }
        
    // Leaf comparison (==, <, >, etc.)
        // Create single-predicate conjunctive query
        ConjunctiveQuery conj;
        conj.table = table;
        
        std::vector<PredicateEq> eqPreds;
        std::vector<PredicateRange> rangePreds;
        
        if (!extractPredicates(expr, eqPreds, rangePreds, error)) {
            return {};
        }
        
        conj.predicates = std::move(eqPreds);
        conj.rangePredicates = std::move(rangePreds);
        
        return {conj};
    }

    // Unary NOT: no push-down; return a neutral conjunct and rely on post-filter evaluation in executor
    if (expr->getType() == ASTNodeType::UnaryOp) {
        ConjunctiveQuery conj; conj.table = table; // no predicates
        return {conj};
    }
    
    // FULLTEXT function call - create single-predicate query with FULLTEXT
    if (expr->getType() == ASTNodeType::FunctionCall) {
        auto funcCall = std::static_pointer_cast<FunctionCallExpr>(expr);
        std::string funcName = funcCall->name;
        std::transform(funcName.begin(), funcName.end(), funcName.begin(), ::tolower);
        
        if (funcName == "fulltext") {
            // Parse FULLTEXT(column, query [, limit])
            if (funcCall->arguments.size() < 2 || funcCall->arguments.size() > 3) {
                error = "FULLTEXT() requires 2-3 arguments: FULLTEXT(column, query [, limit])";
                return {};
            }
            
            if (funcCall->arguments[0]->getType() != ASTNodeType::FieldAccess) {
                error = "FULLTEXT() first argument must be field access (e.g., doc.content)";
                return {};
            }
            std::string column = extractColumnName(funcCall->arguments[0]);
            
            if (funcCall->arguments[1]->getType() != ASTNodeType::Literal) {
                error = "FULLTEXT() second argument must be string literal";
                return {};
            }
            auto queryLiteral = std::static_pointer_cast<LiteralExpr>(funcCall->arguments[1]);
            if (!std::holds_alternative<std::string>(queryLiteral->value)) {
                error = "FULLTEXT() query must be a string";
                return {};
            }
            std::string queryStr = std::get<std::string>(queryLiteral->value);
            
            size_t limit = 1000; // default
            if (funcCall->arguments.size() == 3) {
                if (funcCall->arguments[2]->getType() != ASTNodeType::Literal) {
                    error = "FULLTEXT() third argument (limit) must be integer literal";
                    return {};
                }
                auto limitLiteral = std::static_pointer_cast<LiteralExpr>(funcCall->arguments[2]);
                if (std::holds_alternative<int64_t>(limitLiteral->value)) {
                    limit = static_cast<size_t>(std::get<int64_t>(limitLiteral->value));
                } else {
                    error = "FULLTEXT() limit must be an integer";
                    return {};
                }
            }
            
            // Create a ConjunctiveQuery with only the fulltext predicate
            ConjunctiveQuery conj;
            conj.table = table;
            conj.fulltextPredicate = PredicateFulltext{column, queryStr, limit};
            return {conj};
        }
    }
    
    error = "Unsupported expression type in DNF conversion";
    return {};
}

} // namespace themis
