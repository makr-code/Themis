# aql_translator.cpp

Path: `src/query/aql_translator.cpp`

Purpose: Translate AQL AST fragments into internal query engine operations; handles FULLTEXT parsing.

Public functions / symbols:
- `if (!ast) {`
- `if (ast->traversal) {`
- `switch (ast->traversal->direction) {`
- `for (const auto& filter : ast->filters) {`
- ``
- `if (!filter || !filter->condition) {`
- `if (ast->sort) {`
- `if (bo->op == BinaryOperator::And) {`
- `if (name != "fulltext") {`
- `for (const auto& predExpr : predicateExprs) {`
- `if (!expr) {`
- `if (binOp->op == BinaryOperator::And) {`
- `if (binOp->op == BinaryOperator::Or) {`
- `switch (binOp->op) {`
- `if (limit) {`
- `for (const auto& leftConj : leftDNF) {`
- `for (const auto& rightConj : rightDNF) {`
- `return findFulltext(bo->right);`
- `collectNonFulltext(bo->left, preds);`
- `collectNonFulltext(bo->right, preds);`
- `collectNonFulltext(filter->condition, predicateExprs);`
- `extractPredicates(binOp->right, eqPredicates, rangePredicates, error);`

Notes / TODOs:
- Document FULLTEXT translation steps and tokenization.
