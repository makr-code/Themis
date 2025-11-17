# Query Engine & AQL – THEMIS

**Version:** 2.0  
**Status:** Implementiert (MVP)  
**Letzte Aktualisierung:** 2. November 2025

---

## Überblick

Das **Query Engine & AQL**-System von THEMIS besteht aus mehreren Komponenten, die zusammen eine effiziente Ausführung von Multi-Modell-Queries ermöglichen:

1. **AQL (Advanced Query Language)** – SQL-ähnliche deklarative Query-Sprache
2. **AQL Parser** – Lexer & Parser für AQL → AST
3. **AQL Translator** – AST → Ausführungspläne (ConjunctiveQuery, JoinQuery, TraversalQuery)
4. **Query Optimizer** – Kardinalitätsschätzung & Index-Auswahl
5. **Query Engine** – Ausführung mit Index-/Full-Scan-Support

---

## 1. AQL – Advanced Query Language

### 1.1 Design-Prinzipien

- ✅ **Einfach:** SQL-ähnliche Syntax (FOR, FILTER, SORT, LIMIT, RETURN)
- ✅ **Mächtig:** Multi-Modell-Support (Relational, Graph, Vector)
- ✅ **Optimierbar:** Automatische Index-Auswahl via Optimizer
- ✅ **Erweiterbar:** Schrittweise Features (LET, COLLECT, Joins)

### 1.2 Grundstruktur

```aql
FOR variable IN collection
  [LET var = expression [, ...]]
  [FILTER condition]
  [SORT expression [ASC|DESC] [, ...]]
  [LIMIT offset, count]
  [RETURN expression]
```

**Execution-Reihenfolge:**
1. `FOR` – Iteration über Collection/Index
2. `FILTER` – Prädikat-Evaluation (mit Index-Nutzung)
3. `SORT` – Sortierung (mit Range-Index wenn möglich)
4. `LIMIT` – Pagination/Offset
5. `RETURN` – Projektion

### 1.3 Query-Typen

| Typ | FOR-Klauseln | Features | Beispiel |
|-----|--------------|----------|----------|
| **Relational** | 1 | FILTER, SORT, LIMIT, RETURN | `FOR u IN users FILTER u.age > 18` |
| **Join** | 2+ | Multi-FOR, JOIN-Bedingungen | `FOR u IN users FOR o IN orders FILTER o.user_id == u._key` |
| **Graph Traversal** | 1 (speziell) | BFS, Depth-Limits, FILTER auf v/e | `FOR v IN 1..3 OUTBOUND 'user1' GRAPH 'social'` |
| **Vector Search** | 1 | NEAR(), k-NN | `FOR doc IN articles NEAR(doc.embedding, @vec, 10)` |
| **Aggregation** | 1 | COLLECT, AGGREGATE (SUM/AVG/etc.) | `FOR sale IN sales COLLECT cat = sale.category AGGREGATE total = SUM(sale.amount)` |

---

## 2. AQL Parser

### 2.1 Komponenten

```cpp
class AQLParser {
public:
    struct ParseResult {
        bool success;
        std::string error_message;
        std::shared_ptr<Query> ast;  // Root AST Node
    };
    
    ParseResult parse(std::string_view query);
};
```

### 2.2 AST-Struktur

**Node-Typen:**

```cpp
enum class ASTNodeType {
    // Query Nodes
    Query,              // Root
    ForNode,            // FOR variable IN collection
    FilterNode,         // FILTER condition
    SortNode,           // SORT expr [ASC|DESC]
    LimitNode,          // LIMIT offset, count
    ReturnNode,         // RETURN expression
    LetNode,            // LET variable = expression
    CollectNode,        // COLLECT ... AGGREGATE ...
    
    // Expressions
    BinaryOp,           // ==, !=, >, <, AND, OR, +, -, *, /
    UnaryOp,            // NOT, -
    FunctionCall,       // CONCAT, SUM, etc.
    FieldAccess,        // doc.field
    Literal,            // "string", 123, true, null
    Variable,           // doc, user
    ArrayLiteral,       // [1, 2, 3]
    ObjectConstruct     // {name: doc.name, age: doc.age}
};
```

**Beispiel-AST:**

```aql
FOR u IN users 
FILTER u.age > 18 AND u.city == "Berlin"
RETURN u.name
```

→ AST:

```json
{
  "type": "Query",
  "children": [
    {
      "type": "ForNode",
      "variable": "u",
      "collection": "users"
    },
    {
      "type": "FilterNode",
      "condition": {
        "type": "BinaryOp",
        "operator": "AND",
        "left": {
          "type": "BinaryOp",
          "operator": ">",
          "left": {"type": "FieldAccess", "variable": "u", "field": "age"},
          "right": {"type": "Literal", "value": 18}
        },
        "right": {
          "type": "BinaryOp",
          "operator": "==",
          "left": {"type": "FieldAccess", "variable": "u", "field": "city"},
          "right": {"type": "Literal", "value": "Berlin"}
        }
      }
    },
    {
      "type": "ReturnNode",
      "expression": {"type": "FieldAccess", "variable": "u", "field": "name"}
    }
  ]
}
```

### 2.3 Operatoren

**Binary Operators:**

```cpp
enum class BinaryOperator {
    // Comparison
    Eq, Neq, Lt, Lte, Gt, Gte,      // ==, !=, <, <=, >, >=
    
    // Logical
    And, Or, Xor,                    // AND, OR, XOR
    
    // Arithmetic
    Add, Sub, Mul, Div, Mod,         // +, -, *, /, %
    
    // Membership
    In                               // IN
};
```

**Unary Operators:**

```cpp
enum class UnaryOperator {
    Not,                // NOT
    Minus,              // - (unary)
    Plus                // + (unary)
};
```

---

## 3. AQL Translator

### 3.1 Übersetzungsstrategien

Der Translator wandelt AST in ausführbare Query-Pläne um:

```cpp
class AQLTranslator {
public:
    struct TranslationResult {
        bool success;
        std::string error_message;
        
        // Single-FOR: Relational Query
        ConjunctiveQuery query;
        
        // Multi-FOR: Join Query
        std::optional<JoinQuery> join;
        
        // Graph: Traversal Query
        std::optional<TraversalQuery> traversal;
    };
    
    TranslationResult translate(std::shared_ptr<Query> ast);
};
```

### 3.2 Relational Query Translation

**Eingabe:**
```aql
FOR u IN users 
FILTER u.age > 18 AND u.city == "Berlin"
SORT u.created_at DESC
LIMIT 10
RETURN u
```

**Ausgabe (ConjunctiveQuery):**
```cpp
ConjunctiveQuery {
    table: "users",
    predicates: [
        {column: "city", op: Eq, value: "Berlin"}
    ],
    rangePredicates: [
        {column: "age", lower: "18", includeLower: false, op: Gt}
    ],
    orderBy: {
        column: "created_at",
        desc: true,
        limit: 10
    }
}
```

### 3.3 Join Query Translation

**Eingabe:**
```aql
FOR u IN users
FOR o IN orders
FILTER o.user_id == u._key
RETURN {user: u.name, order: o.id}
```

**Ausgabe (JoinQuery):**
```cpp
JoinQuery {
    for_nodes: [
        {variable: "u", collection: "users"},
        {variable: "o", collection: "orders"}
    ],
    filters: [
        {op: Eq, left: "o.user_id", right: "u._key"}  // Join-Bedingung
    ],
    return_node: ObjectConstruct{...}
}
```

### 3.4 Graph Traversal Translation

**Eingabe:**
```aql
FOR v, e, p IN 1..3 OUTBOUND 'user1' GRAPH 'social'
FILTER v.age > 18
RETURN v
```

**Ausgabe (TraversalQuery):**
```cpp
TraversalQuery {
    variable: "v",
    minDepth: 1,
    maxDepth: 3,
    direction: Outbound,
    startVertex: "user1",
    graphName: "social",
    filters: [{column: "age", op: Gt, value: "18"}]
}
```

---

## 4. Query Optimizer

### 4.1 Kardinalitätsschätzung

Der Optimizer schätzt Selektivitäten von Prädikaten und ordnet sie optimal:

```cpp
class QueryOptimizer {
public:
    struct Estimation {
        PredicateEq pred;
        size_t estimatedCount;
        bool capped;  // true wenn >= maxProbe
    };
    
    struct Plan {
        std::vector<PredicateEq> orderedPredicates;  // Sortiert nach Selektivität
        std::vector<Estimation> details;
    };
    
    Plan chooseOrderForAndQuery(const ConjunctiveQuery& q, size_t maxProbe = 1000);
};
```

### 4.2 Optimierungs-Strategie

**Beispiel:**

```aql
FOR u IN users 
FILTER u.age > 18 AND u.city == "Berlin"
```

**Schätzung:**
- `city == "Berlin"`: ~100 Treffer (selektiv!)
- `age > 18`: ~5000 Treffer (weniger selektiv)

**Optimaler Plan:**
1. Scan `city == "Berlin"` → 100 Keys
2. Für jeden Key: Check `age > 18` → ~80 finale Treffer

**Vorteil:** Nur 100 Entity-Loads statt 5000!

### 4.3 Index-Auswahl

**Verfügbare Strategien:**

```cpp
enum class QueryMode {
    IndexOptimized,      // Optimizer-gesteuert (Kardinalitätsschätzung)
    IndexParallel,       // Parallele Scans + AND-Merge (für kleine Datasets)
    FullScanFallback,    // Sequential Scan (nur mit allow_full_scan=true)
    IndexRangeAware      // Range-Index + Sortierung direkt
};
```

**Beispiel-Plan (EXPLAIN):**

```json
{
  "plan": {
    "mode": "index_optimized",
    "order": [
      {"column": "city", "value": "Berlin"},
      {"column": "age", "value": "18"}
    ],
    "estimates": [
      {"column": "city", "value": "Berlin", "estimatedCount": 100, "capped": false},
      {"column": "age", "value": "18", "estimatedCount": 5000, "capped": false}
    ]
  }
}
```

---

## 5. Query Engine

### 5.1 Ausführungs-Pipeline

```cpp
class QueryEngine {
public:
    struct Status {
        bool ok;
        std::string message;
    };
    
    // Relational Query (Single-FOR)
    std::pair<Status, std::vector<std::string>> 
    executeConjunctiveKeys(const ConjunctiveQuery& q);
    
    std::pair<Status, std::vector<BaseEntity>> 
    executeConjunctiveEntities(const ConjunctiveQuery& q);
    
    // Join Query (Multi-FOR)
    std::pair<Status, std::vector<nlohmann::json>> 
    executeJoin(const JoinQuery& join);
    
    // Graph Traversal (BFS)
    std::pair<Status, std::vector<BaseEntity>> 
    executeTraversal(const TraversalQuery& trav);
};
```

### 5.2 Relational Execution

**Schritt-für-Schritt:**

1. **Optimizer:** Schätze Selektivitäten → Sortiere Prädikate
2. **Index-Scan:** Starte mit selektivstem Prädikat
3. **Filter-Chain:** Wende weitere Prädikate an
4. **Sort/Limit:** Nutze Range-Index wenn möglich
5. **Return:** Projiziere Felder

**Code-Flow (vereinfacht):**

```cpp
auto [status, keys] = idx.scanKeysEqual("users", "city", "Berlin");  // 100 Keys
std::vector<std::string> filtered;
for (const auto& key : keys) {
    auto entity = loadEntity(key);
    if (entity.getFieldAsInt("age") > 18) {  // Range-Filter
        filtered.push_back(key);
    }
}
// Sort/Limit...
```

### 5.3 Join Execution (Nested-Loop)

**Algorithmus:**

```cpp
std::vector<nlohmann::json> results;
for (const auto& uKey : getUserKeys()) {
    auto user = loadEntity("users", uKey);
    for (const auto& oKey : getOrderKeys()) {
        auto order = loadEntity("orders", oKey);
        if (order.getField("user_id") == user.getField("_key")) {  // Join-Bedingung
            results.push_back({
                {"user", user.getField("name")},
                {"order", order.getField("id")}
            });
        }
    }
}
```

**Performance-Hinweise:**
- ⚠️ O(n×m) Komplexität (teuer bei großen Collections)
- 💡 Nutze Indizes auf Join-Spalten (`user_id`)
- 💡 Geplant: Hash-Join für große Collections

### 5.4 Graph Traversal (BFS)

**BFS-Algorithmus mit Pruning:**

```cpp
std::queue<Node> frontier;
std::unordered_set<std::string> visited;
frontier.push({startVertex, depth: 0});

while (!frontier.empty()) {
    auto node = frontier.front(); frontier.pop();
    if (visited.count(node.pk)) continue;
    visited.insert(node.pk);
    
    if (node.depth >= minDepth) {
        auto entity = loadEntity(node.pk);
        if (evalFilters(entity)) {  // v.age > 18
            results.push_back(entity);
        }
    }
    
    if (node.depth < maxDepth) {
        auto neighbors = getNeighbors(node.pk, direction);
        for (const auto& nb : neighbors) {
            if (node.depth + 1 == maxDepth) {
                // Konservatives Pruning am letzten Level
                auto e = loadEntity(nb.pk);
                if (!evalFilters(e)) {
                    pruned_last_level++;
                    continue;
                }
            }
            frontier.push({nb.pk, node.depth + 1});
        }
    }
}
```

**Metriken (siehe EXPLAIN):**
- `edges_expanded`: Anzahl inspizierter Kanten
- `pruned_last_level`: Durch Filter gedroppt
- `frontier_processed_per_depth`: BFS-Expansion pro Level

---

## 6. EXPLAIN & PROFILE

### 6.1 EXPLAIN Usage

**HTTP Request:**

```http
POST /query/aql
Content-Type: application/json

{
  "query": "FOR u IN users FILTER u.age > 18 AND u.city == 'Berlin' RETURN u",
  "explain": true
}
```

**Response:**

```json
{
  "query": "FOR u IN users FILTER u.age > 18 AND u.city == 'Berlin' RETURN u",
  "ast": {...},
  "plan": {
    "mode": "index_optimized",
    "order": [
      {"column": "city", "value": "Berlin"},
      {"column": "age", "value": "18"}
    ],
    "estimates": [
      {"column": "city", "value": "Berlin", "estimatedCount": 100, "capped": false},
      {"column": "age", "value": "18", "estimatedCount": 5000, "capped": false}
    ]
  }
}
```

### 6.2 Traversal Metrics

**Graph-Query:**

```aql
FOR v IN 1..3 OUTBOUND 'user1' GRAPH 'social' 
FILTER v.age > 30 
RETURN v
```

**Metrics:**

```json
{
  "metrics": {
    "edges_expanded": 156,
    "pruned_last_level": 23,
    "filter_evaluations_total": 89,
    "filter_short_circuits": 12,
    "frontier_processed_per_depth": {
      "0": 1,
      "1": 5,
      "2": 18,
      "3": 65
    }
  }
}
```

**Interpretation:**
- **edges_expanded**: 156 Kanten inspiziert (BFS-Kosten)
- **pruned_last_level**: 23 Vertices am letzten Level gedroppt (Filter wirkt!)
- **filter_short_circuits**: 12 AND-Short-Circuits (Effizienz)

### 6.3 Cursor Pagination Metrics

**Relational Query mit Cursor:**

```json
{
  "query": "FOR u IN users SORT u.created_at DESC LIMIT 10 RETURN u",
  "use_cursor": true
}
```

**Plan-Details:**

```json
{
  "plan": {
    "mode": "index_rangeaware",
    "cursor": {
      "used": true,
      "cursor_present": false,
      "sort_column": "created_at",
      "effective_limit": 11,
      "anchor_set": false,
      "requested_count": 10
    }
  }
}
```

**Prometheus Metrics:**
- `vccdb_cursor_anchor_hits_total`: Cursor-Anker-Verwendungen
- `vccdb_range_scan_steps_total`: Besuchte Index-Einträge
- `vccdb_page_fetch_time_ms_*`: Seitenerzeugung-Dauer

---

## 7. Best Practices

### 7.1 Query-Optimierung

```aql
-- ✅ RICHTIG: Selektive Filter zuerst
FOR u IN users 
FILTER u.city == "SmallTown" AND u.age > 18  -- city sehr selektiv!
RETURN u

-- ❌ FALSCH: Unselektive Filter zuerst
FOR u IN users 
FILTER u.age > 18 AND u.city == "SmallTown"  -- age wenig selektiv
RETURN u
```

### 7.2 Index-Nutzung

```aql
-- ✅ RICHTIG: Index auf age + city
CREATE INDEX idx_users_age ON users(age)
CREATE INDEX idx_users_city ON users(city)

FOR u IN users 
FILTER u.age > 18 AND u.city == "Berlin"  -- Beide Indizes genutzt!
RETURN u

-- ❌ FALSCH: Kein Index → Full Scan
FOR u IN users 
FILTER u.random_field == "value"  -- Kein Index!
RETURN u
```

### 7.3 Joins minimieren

```aql
-- ✅ RICHTIG: Filter vor Join
FOR u IN users 
FILTER u.active == true           -- Reduziert u-Set!
FOR o IN orders 
FILTER o.user_id == u._key
RETURN {user: u.name, order: o.id}

-- ❌ FALSCH: Kein Filter → Großer Cross-Product
FOR u IN users 
FOR o IN orders 
FILTER o.user_id == u._key        -- Erst nach Cross-Product!
RETURN {user: u.name, order: o.id}
```

### 7.4 Graph-Traversals

```aql
-- ✅ RICHTIG: Depth begrenzen
FOR v IN 1..3 OUTBOUND 'user1' GRAPH 'social'  -- Max 3 Hops
FILTER v.age > 30
RETURN v

-- ❌ FALSCH: Unbegrenzte Depth
FOR v IN 1..10 OUTBOUND 'user1' GRAPH 'social'  -- Exponentielles Wachstum!
RETURN v
```

---

## 8. Limitierungen (MVP)

### 8.1 Aktuelle Einschränkungen

- ❌ **OR-Support:** Nur AND im Translator (OR in Arbeit)
- ❌ **Feld-zu-Feld-Vergleiche:** `u.city == o.city` nicht generisch (nur in Join-Bedingungen)
- ❌ **Subqueries:** Noch nicht implementiert
- ❌ **Hash-Join:** Nur Nested-Loop-Joins (O(n×m))
- ❌ **Complex Functions:** CONCAT, SUBSTRING in Entwicklung

### 8.2 Geplante Erweiterungen

- [ ] **OR-Support:** Disjunktive Prädikate
- [ ] **Hash-Join:** Für große Collections
- [ ] **Subqueries:** Nested Queries
- [ ] **Window Functions:** ROW_NUMBER, RANK
- [ ] **CTEs (WITH):** Common Table Expressions
- [ ] **UPSERT:** INSERT ... ON CONFLICT UPDATE

---

## Referenzen

- **AQL Syntax:** [aql_syntax.md](aql_syntax.md)
- **EXPLAIN & PROFILE:** [aql_explain_profile.md](aql_explain_profile.md)
- **Parser:** `include/query/aql_parser.h`
- **Translator:** `include/query/aql_translator.h`
- **Optimizer:** `include/query/query_optimizer.h`
- **Engine:** `include/query/query_engine.h`
- **Cursor Pagination:** [cursor_pagination.md](cursor_pagination.md)
- **Indexes:** [indexes.md](indexes.md)
