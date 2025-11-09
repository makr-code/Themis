# AQL LET Implementation Guide

**Status:** Parser vollständig, Executor teilweise implementiert  
**Datum:** 09. November 2025

## Aktuelle Implementierung

### ✅ Was funktioniert

1. **Parser** (`src/query/aql_parser.cpp`, Zeile 502-514)
   - `parseLetClause()` vollständig implementiert
   - AST-Node: `LetNode` mit `variable` und `expression`
   - Unterstützt: `LET var = expression`

2. **RETURN mit LET** (`src/server/http_server.cpp`, Zeile 6182-6197)
   - LET-Environment wird pro Row aufgebaut
   - `evalExpr()` unterstützt Variable Resolution aus `env` Map
   - RETURN-Expression wird mit LET-Context ausgewertet

3. **Optimierte FILTER-LET-Extraktion** (`src/server/http_server.cpp`, Zeile 4115-4186)
   - Spezialfall: Einfache Gleichheitsprädikate mit LET-Variablen
   - Wird zu ConjunctiveQuery umgewandelt (Index-Pushdown)
   - **Limitation:** Nur `loopVar.field == literal` via LET

### ❌ Was fehlt

1. **Runtime FILTER mit LET-Variablen**
   - FILTER-Conditions die LET-Variablen verwenden (z.B. `LET x = u.age * 2 FILTER x > 50`)
   - Derzeit: Nur Index-optimierte Filter, keine Post-Filter-Evaluation
   - **Benötigt:** Post-Filter nach Query-Execution

2. **SORT mit LET-Variablen**
   - `LET x = u.salary * 0.1 SORT x DESC`
   - Derzeit: Nur Index-basiertes ORDER BY
   - **Benötigt:** In-Memory Sort mit LET-Evaluation

3. **COLLECT mit LET**
   - `LET bonus = u.salary * 0.1 COLLECT g = bonus ...`
   - Derzeit funktioniert COLLECT MVP, aber nicht mit LET-Variablen
   - **Benötigt:** LET-Evaluation vor Gruppierung

## Implementierungsplan

### 1. Post-Filter für LET-Variablen

**Datei:** `src/server/http_server.cpp`  
**Position:** Nach `forSpan.setStatus(true)` (ca. Zeile 5670)

```cpp
// Post-filter: Apply FILTER expressions that reference LET variables
if (parse_result.query && !parse_result.query->filters.empty() && !parse_result.query->let_nodes.empty()) {
    auto filterSpan = Tracer::startSpan("aql.post_filter_let");
    
    // Reuse evalExpr lambda (defined later for RETURN)
    // Move evalExpr definition here or create shared helper
    
    std::vector<themis::BaseEntity> filtered;
    filtered.reserve(sliced.size());
    
    size_t filtered_out = 0;
    for (const auto& ent : sliced) {
        // Build LET environment for this row
        std::unordered_map<std::string, nlohmann::json> env;
        for (const auto& ln : parse_result.query->let_nodes) {
            auto val = evalExpr(ln.expression, ent, env);
            env[ln.variable] = std::move(val);
        }
        
        // Evaluate all FILTER conditions (AND-combined)
        bool pass = true;
        for (const auto& filter : parse_result.query->filters) {
            auto result = evalExpr(filter->condition, ent, env);
            bool cond = result.is_boolean() ? result.get<bool>() : (!result.is_null() && result != false);
            if (!cond) {
                pass = false;
                break;
            }
        }
        
        if (pass) {
            filtered.push_back(ent);
        } else {
            filtered_out++;
        }
    }
    
    filterSpan.setAttribute("post_filter.input_count", static_cast<int64_t>(sliced.size()));
    filterSpan.setAttribute("post_filter.output_count", static_cast<int64_t>(filtered.size()));
    filterSpan.setAttribute("post_filter.filtered_out", static_cast<int64_t>(filtered_out));
    filterSpan.setStatus(true);
    
    sliced = std::move(filtered);
}
```

**Problem:** `evalExpr` Lambda ist aktuell weiter unten definiert (Zeile 5990+)

**Lösung:** 
- Option A: Extrahiere `evalExpr` in separate Funktion (empfohlen)
- Option B: Definiere `evalExpr` früher im Code und capture by reference

### 2. SORT mit LET-Evaluation

**Datei:** `src/server/http_server.cpp`  
**Position:** Vor existierendem SORT (ca. Zeile 5700-5715)

```cpp
// SORT with LET-variable evaluation (if SORT expression references LET variables)
if (parse_result.query && parse_result.query->sort && !parse_result.query->let_nodes.empty()) {
    auto sortSpan = Tracer::startSpan("aql.sort_let");
    
    // Check if SORT expression contains LET variables
    std::function<bool(const std::shared_ptr<Expression>&)> referencesLetVar;
    referencesLetVar = [&](const std::shared_ptr<Expression>& expr)->bool {
        if (!expr) return false;
        using namespace themis::query;
        if (auto* v = dynamic_cast<VariableExpr*>(expr.get())) {
            // Check if variable is a LET variable
            for (const auto& ln : parse_result.query->let_nodes) {
                if (ln.variable == v->name) return true;
            }
            return false;
        }
        // Recurse into compound expressions
        if (auto* bo = dynamic_cast<BinaryOpExpr*>(expr.get())) {
            return referencesLetVar(bo->left) || referencesLetVar(bo->right);
        }
        if (auto* u = dynamic_cast<UnaryOpExpr*>(expr.get())) {
            return referencesLetVar(u->operand);
        }
        if (auto* fa = dynamic_cast<FieldAccessExpr*>(expr.get())) {
            return referencesLetVar(fa->object);
        }
        if (auto* fc = dynamic_cast<FunctionCallExpr*>(expr.get())) {
            for (const auto& arg : fc->arguments) {
                if (referencesLetVar(arg)) return true;
            }
        }
        return false;
    };
    
    const auto& spec = parse_result.query->sort->specifications[0];
    if (referencesLetVar(spec.expression)) {
        // In-memory sort with LET evaluation
        std::unordered_map<std::string, nlohmann::json> sortKeys;
        
        for (const auto& ent : sliced) {
            std::unordered_map<std::string, nlohmann::json> env;
            for (const auto& ln : parse_result.query->let_nodes) {
                auto val = evalExpr(ln.expression, ent, env);
                env[ln.variable] = std::move(val);
            }
            auto sortKey = evalExpr(spec.expression, ent, env);
            sortKeys[ent.getPrimaryKey()] = std::move(sortKey);
        }
        
        std::sort(sliced.begin(), sliced.end(), [&](const themis::BaseEntity& a, const themis::BaseEntity& b){
            const auto& ka = sortKeys[a.getPrimaryKey()];
            const auto& kb = sortKeys[b.getPrimaryKey()];
            if (spec.ascending) return ka < kb;
            else return ka > kb;
        });
        
        sortSpan.setAttribute("sort_let.count", static_cast<int64_t>(sliced.size()));
        sortSpan.setStatus(true);
    }
}
```

### 3. Refactoring: Extrahiere evalExpr in Helper-Funktion

**Neuer File:** `include/query/aql_evaluator.h`

```cpp
#pragma once

#include "aql_parser.h"
#include "storage/base_entity.h"
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <functional>

namespace themis {
namespace query {

class AQLExpressionEvaluator {
public:
    using Environment = std::unordered_map<std::string, nlohmann::json>;
    
    AQLExpressionEvaluator(const std::string& loop_variable)
        : loop_variable_(loop_variable) {}
    
    nlohmann::json evaluate(
        const std::shared_ptr<Expression>& expr,
        const BaseEntity& entity,
        const Environment& env
    );
    
    // Optional: Provide custom function handlers
    using FunctionHandler = std::function<nlohmann::json(const std::vector<nlohmann::json>&)>;
    void registerFunction(const std::string& name, FunctionHandler handler);

private:
    std::string loop_variable_;
    std::unordered_map<std::string, FunctionHandler> custom_functions_;
    
    std::optional<std::string> extractColumnFromFieldAccess(
        const std::shared_ptr<Expression>& expr,
        bool& rooted
    );
};

} // namespace query
} // namespace themis
```

**Implementierung:** `src/query/aql_evaluator.cpp`

```cpp
#include "query/aql_evaluator.h"
#include <algorithm>

namespace themis {
namespace query {

nlohmann::json AQLExpressionEvaluator::evaluate(
    const std::shared_ptr<Expression>& expr,
    const BaseEntity& entity,
    const Environment& env
) {
    if (!expr) return nlohmann::json();
    
    switch (expr->getType()) {
        case ASTNodeType::Literal:
            return static_cast<LiteralExpr*>(expr.get())->toJSON()["value"];
            
        case ASTNodeType::Variable: {
            auto* v = static_cast<VariableExpr*>(expr.get());
            if (v->name == loop_variable_) return entity.toJson();
            auto it = env.find(v->name);
            if (it != env.end()) return it->second;
            return nullptr;
        }
        
        case ASTNodeType::FieldAccess: {
            bool rooted = false;
            auto colOpt = extractColumnFromFieldAccess(expr, rooted);
            if (colOpt.has_value() && rooted) {
                // Direct entity field access
                auto asDouble = entity.getFieldAsDouble(*colOpt);
                if (asDouble.has_value()) return *asDouble;
                auto asStr = entity.getFieldAsString(*colOpt);
                if (asStr.has_value()) return *asStr;
                return nullptr;
            } else {
                // Nested access via JSON
                auto* fa = static_cast<FieldAccessExpr*>(expr.get());
                auto base = evaluate(fa->object, entity, env);
                if (base.is_object()) {
                    auto it = base.find(fa->field);
                    if (it != base.end()) return *it;
                }
                return nullptr;
            }
        }
        
        case ASTNodeType::BinaryOp: {
            auto* bo = static_cast<BinaryOpExpr*>(expr.get());
            auto left = evaluate(bo->left, entity, env);
            auto right = evaluate(bo->right, entity, env);
            
            auto toNumber = [](const nlohmann::json& j, double& out)->bool {
                if (j.is_number()) { out = j.get<double>(); return true; }
                if (j.is_boolean()) { out = j.get<bool>() ? 1.0 : 0.0; return true; }
                if (j.is_string()) {
                    char* end = nullptr;
                    std::string s = j.get<std::string>();
                    out = strtod(s.c_str(), &end);
                    return end && *end == '\0';
                }
                return false;
            };
            
            switch (bo->op) {
                case BinaryOperator::Eq:  return left == right;
                case BinaryOperator::Neq: return left != right;
                case BinaryOperator::Lt:  return left < right;
                case BinaryOperator::Lte: return left <= right;
                case BinaryOperator::Gt:  return left > right;
                case BinaryOperator::Gte: return left >= right;
                case BinaryOperator::And: {
                    bool lb = left.is_boolean() ? left.get<bool>() : (!left.is_null());
                    bool rb = right.is_boolean() ? right.get<bool>() : (!right.is_null());
                    return nlohmann::json(lb && rb);
                }
                case BinaryOperator::Or: {
                    bool lb = left.is_boolean() ? left.get<bool>() : (!left.is_null());
                    bool rb = right.is_boolean() ? right.get<bool>() : (!right.is_null());
                    return nlohmann::json(lb || rb);
                }
                case BinaryOperator::Add: {
                    double a, b;
                    if (toNumber(left, a) && toNumber(right, b)) return a + b;
                    return nullptr;
                }
                case BinaryOperator::Sub: {
                    double a, b;
                    if (toNumber(left, a) && toNumber(right, b)) return a - b;
                    return nullptr;
                }
                case BinaryOperator::Mul: {
                    double a, b;
                    if (toNumber(left, a) && toNumber(right, b)) return a * b;
                    return nullptr;
                }
                case BinaryOperator::Div: {
                    double a, b;
                    if (toNumber(left, a) && toNumber(right, b) && b != 0.0) return a / b;
                    return nullptr;
                }
                default: return nullptr;
            }
        }
        
        case ASTNodeType::UnaryOp: {
            auto* u = static_cast<UnaryOpExpr*>(expr.get());
            auto val = evaluate(u->operand, entity, env);
            switch (u->op) {
                case UnaryOperator::Not:
                    return val.is_boolean() ? nlohmann::json(!val.get<bool>()) : nlohmann::json(false);
                case UnaryOperator::Minus:
                    if (val.is_number()) return -val.get<double>();
                    return nullptr;
                case UnaryOperator::Plus:
                    if (val.is_number()) return val.get<double>();
                    return nullptr;
                default: return nullptr;
            }
        }
        
        case ASTNodeType::FunctionCall: {
            auto* fc = static_cast<FunctionCallExpr*>(expr.get());
            std::string name = fc->name;
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            
            // Check custom functions first
            auto it = custom_functions_.find(name);
            if (it != custom_functions_.end()) {
                std::vector<nlohmann::json> args;
                for (const auto& arg : fc->arguments) {
                    args.push_back(evaluate(arg, entity, env));
                }
                return it->second(args);
            }
            
            // Built-in functions (CONCAT, etc.) can be implemented here
            // For now, return null for unsupported functions
            return nullptr;
        }
        
        case ASTNodeType::ObjectConstruct: {
            auto* oc = static_cast<ObjectConstructExpr*>(expr.get());
            nlohmann::json obj = nlohmann::json::object();
            for (const auto& kv : oc->fields) {
                obj[kv.first] = evaluate(kv.second, entity, env);
            }
            return obj;
        }
        
        case ASTNodeType::ArrayLiteral: {
            auto* ar = static_cast<ArrayLiteralExpr*>(expr.get());
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& el : ar->elements) {
                arr.push_back(evaluate(el, entity, env));
            }
            return arr;
        }
        
        default:
            return nullptr;
    }
}

std::optional<std::string> AQLExpressionEvaluator::extractColumnFromFieldAccess(
    const std::shared_ptr<Expression>& expr,
    bool& rooted
) {
    auto* fa = dynamic_cast<FieldAccessExpr*>(expr.get());
    if (!fa) return std::nullopt;
    
    std::vector<std::string> parts;
    parts.push_back(fa->field);
    
    auto* cur = fa->object.get();
    while (auto* fa2 = dynamic_cast<FieldAccessExpr*>(cur)) {
        parts.push_back(fa2->field);
        cur = fa2->object.get();
    }
    
    auto* root = dynamic_cast<VariableExpr*>(cur);
    if (!root || root->name != loop_variable_) {
        rooted = false;
        return std::nullopt;
    }
    
    rooted = true;
    std::string col;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        if (!col.empty()) col += ".";
        col += *it;
    }
    return col;
}

void AQLExpressionEvaluator::registerFunction(const std::string& name, FunctionHandler handler) {
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    custom_functions_[lower_name] = std::move(handler);
}

} // namespace query
} // namespace themis
```

### 4. Integration in http_server.cpp

**Änderungen:**

1. Include neuen Header:
```cpp
#include "query/aql_evaluator.h"
```

2. Erstelle Evaluator-Instanz am Anfang von `handleQueryAql`:
```cpp
themis::query::AQLExpressionEvaluator evaluator(loopVar);

// Register custom functions (BM25, FULLTEXT_SCORE, etc.)
evaluator.registerFunction("bm25", [&](const std::vector<nlohmann::json>& args)->nlohmann::json {
    if (args.empty()) return 0.0;
    auto arg = args[0];
    if (arg.is_object()) {
        std::string pk;
        if (arg.contains("_key") && arg["_key"].is_string()) pk = arg["_key"].get<std::string>();
        else if (arg.contains("_pk") && arg["_pk"].is_string()) pk = arg["_pk"].get<std::string>();
        if (!pk.empty()) {
            auto it = fulltextScoreByPk.find(pk);
            return (it != fulltextScoreByPk.end()) ? it->second : 0.0;
        }
    }
    return 0.0;
});

// Add other custom functions as needed
```

3. Verwende Evaluator für Post-Filter, SORT, RETURN

## Tests

**Datei:** `tests/test_aql_let.cpp` (bereits erstellt)

**Test-Szenarien:**
- ✅ `Let_SimpleArithmetic` - Grundlegende Arithmetik
- ✅ `Let_MultipleLets` - Mehrere LET-Bindings
- ✅ `Let_InFilter` - FILTER mit LET-Variable
- ✅ `Let_WithSort` - SORT mit LET-Variable
- ✅ `Let_StringConcatenation` - String-Operationen
- ✅ `Let_NestedFieldAccess` - Verschachtelte Felder
- ✅ `Let_ReferenceInReturn` - LET-Variable in RETURN

**Build & Run:**
```powershell
# Rebuild with new files
cmake --build build --config Release

# Run LET tests
.\build\Release\themis_tests.exe --gtest_filter="HttpAqlLetTest.*"
```

## Metriken

- **Parser:** 100% (bereits implementiert)
- **RETURN mit LET:** 100% (bereits implementiert)
- **FILTER mit LET:** 30% (nur Index-Optimierung, kein Post-Filter)
- **SORT mit LET:** 0% (nicht implementiert)
- **COLLECT mit LET:** 0% (nicht implementiert)

**Gesamtfortschritt LET:** ~40% → Ziel: 100%

## Nächste Schritte

1. **Refactoring:** Extrahiere `evalExpr` in `AQLExpressionEvaluator` Klasse
2. **Post-Filter:** Implementiere Runtime-Filter für LET-Variablen
3. **SORT:** Implementiere In-Memory Sort mit LET-Evaluation
4. **Tests:** Führe alle test_aql_let.cpp Tests aus und fixe Fehler
5. **Dokumentation:** Update `implementation_status.md` Phase 1 auf 100%

---

**Autor:** GitHub Copilot  
**Datum:** 09. November 2025  
**Status:** Ready for Implementation
