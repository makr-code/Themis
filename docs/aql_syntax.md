# AQL - THEMIS Query Language

**Version:** 1.0  
**Datum:** 28. Oktober 2025  
**Inspiriert von:** ArangoDB AQL, mit Fokus auf Multi-Modell-Queries

---

## Überblick

**AQL (Advanced Query Language)** ist eine deklarative SQL-ähnliche Sprache für THEMIS, optimiert für hybride Queries über relationale, Graph-, Vektor- und Dokument-Daten.

**Design-Prinzipien:**
- ✅ **Einfach:** SQL-ähnliche Syntax für schnelle Adoption
- ✅ **Mächtig:** Multi-Modell-Support (Relational, Graph, Vector)
- ✅ **Optimierbar:** Automatische Index-Auswahl via Optimizer
- ✅ **Erweiterbar:** Schrittweise Erweiterung (Aggregationen, Joins, Subqueries)

---

## Syntax-Übersicht

### Grundstruktur

```aql
FOR variable IN collection
  [FILTER condition]
  [SORT expression [ASC|DESC] [, ...]]
  [LIMIT offset, count]
  [RETURN expression]
```

**Execution-Reihenfolge:**
1. `FOR` - Iteration über Collection/Index
2. `FILTER` - Prädikat-Evaluation (mit Index-Nutzung)
3. `SORT` - Sortierung (mit Index-Nutzung wenn möglich)
4. `LIMIT` - Pagination/Offset
5. `RETURN` - Projektion (Felder auswählen)

---

## Kern-Klauseln

### 1. FOR - Collection Iteration

```aql
FOR doc IN users
  RETURN doc

FOR u IN users
  FILTER u.age > 18
  RETURN u.name
```

**Syntax:**
- `variable` - Beliebiger Bezeichner (lowercase empfohlen)
- `collection` - Table-Name aus Storage-Layer

**Multi-Collection (später):**
```aql
FOR u IN users
  FOR o IN orders
    FILTER o.user_id == u._key
    RETURN {user: u.name, order: o.id}
```

---

### 2. FILTER - Bedingungen

**Vergleichsoperatoren:**
```aql
FILTER doc.age == 25          // Gleichheit
FILTER doc.age != 25          // Ungleichheit
FILTER doc.age > 18           // Größer
FILTER doc.age >= 18          // Größer-Gleich
FILTER doc.age < 65           // Kleiner
FILTER doc.age <= 65          // Kleiner-Gleich
```

**Logische Operatoren:**
```aql
FILTER doc.age > 18 AND doc.city == "Berlin"
FILTER doc.status == "active" OR doc.status == "pending"
FILTER NOT doc.deleted
```

**IN-Operator:**
```aql
FILTER doc.status IN ["active", "pending", "approved"]
FILTER doc.age IN [18, 21, 25, 30]
```

**String-Operatoren:**
```aql
FILTER LIKE(doc.name, "Max%")           // Prefix-Match
FILTER CONTAINS(doc.description, "AI")  // Substring
FILTER REGEX_TEST(doc.email, ".*@example\.com")
```

**NULL-Checks:**
```aql
FILTER doc.email != null
FILTER doc.phone == null
```

---

### 3. SORT - Sortierung

**Einfache Sortierung:**
```aql
SORT doc.age                  // ASC (default)
SORT doc.age DESC
SORT doc.created_at DESC
```

**Multi-Column-Sort:**
```aql
SORT doc.city ASC, doc.age DESC
SORT doc.priority DESC, doc.created_at ASC
```

**Index-Nutzung:**
- Range-Index auf `age` → effiziente Sortierung
- Composite-Index `(city, age)` → optimale Multi-Column-Sort

---

### 4. LIMIT - Pagination

**Syntax:**
```aql
LIMIT count                   // Erste N Ergebnisse
LIMIT offset, count           // Pagination
```

**Beispiele:**
```aql
LIMIT 10                      // Erste 10
LIMIT 20, 10                  // Zeilen 21-30 (Seite 3)
```

**Best Practices:**
- Immer mit `LIMIT` arbeiten (verhindert Full-Scans)
- Für große Offsets: Cursor-basierte Pagination bevorzugen

---

### 5. RETURN - Projektion

**Ganzes Dokument:**
```aql
RETURN doc
```

**Einzelne Felder:**
```aql
RETURN doc.name
RETURN doc.email
```

**Objekt-Konstruktion:**
```aql
RETURN {
  name: doc.name,
  age: doc.age,
  city: doc.city
}
```

**Berechnete Felder:**
```aql
RETURN {
  name: doc.name,
  age_in_months: doc.age * 12,
  full_address: CONCAT(doc.street, ", ", doc.city)
}
```

**Arrays (später):**
```aql
RETURN [doc.name, doc.age, doc.city]
```

---

## Erweiterte Features (Phase 1.1+)

### LET - Variable Binding

```aql
FOR doc IN users
  LET age_category = (
    doc.age < 18 ? "minor" :
    doc.age < 65 ? "adult" :
    "senior"
  )
  FILTER age_category == "adult"
  RETURN {name: doc.name, category: age_category}
```

### COLLECT - Aggregationen

```aql
FOR doc IN orders
  COLLECT city = doc.city
  AGGREGATE total = SUM(doc.amount), count = COUNT()
  RETURN {city, total, count}
```

**Aggregations-Funktionen:**
- `COUNT()` - Anzahl
- `SUM(expr)` - Summe
- `AVG(expr)` - Durchschnitt
- `MIN(expr)` / `MAX(expr)` - Minimum/Maximum
- `STDDEV(expr)` - Standardabweichung

---

## HTTP-spezifische Parameter für Pagination

Bei Nutzung des HTTP-Endpunkts `POST /query/aql` können optionale Felder zur Pagination mitgegeben werden:

```json
{
  "query": "FOR u IN users SORT u.age ASC LIMIT 10 RETURN u",
  "use_cursor": true,
  "cursor": "<token-aus-previous-response>",
  "allow_full_scan": false
}
```

- `use_cursor` (bool): Aktiviert Cursor-basierte Pagination. Antwortformat enthält `{items, has_more, next_cursor, batch_size}`.
- `cursor` (string): Token aus `next_cursor` der vorherigen Seite. Gültig nur in Kombination mit `use_cursor: true`.
- `allow_full_scan` (bool): Optionaler Fallback für kleine Datenmengen/Tests; für große Daten wird Index-basierte Sortierung empfohlen.

Weitere Details siehe `docs/cursor_pagination.md`.

---

## Spezial-Queries

### Graph-Traversierung

```aql
FOR v, e, p IN 1..3 OUTBOUND "users/alice" edges
  FILTER v.active == true
  RETURN {vertex: v, edge: e, path: p}
```

**Traversal-Richtungen:**
- `OUTBOUND` - Ausgehende Kanten (Alice → Bob)
- `INBOUND` - Eingehende Kanten (Alice ← Bob)
- `ANY` - Beide Richtungen

**Depth-Limits:**
- `1..1` - Nur direkte Nachbarn
- `1..3` - Bis zu 3 Hops
- `2..5` - Min 2, Max 5 Hops

---

### Vektor-Ähnlichkeitssuche

```aql
FOR doc IN users
  NEAR(doc.embedding, @query_vector, 10)
  FILTER doc.age > 18
  RETURN {name: doc.name, similarity: SIMILARITY()}
```

**Funktionen:**
- `NEAR(field, vector, k)` - k-NN-Suche
- `SIMILARITY()` - Aktueller Similarity-Score (0.0 - 1.0)

**Metriken:**
```aql
NEAR(doc.embedding, @query_vector, 10, "cosine")    // Cosine Similarity
NEAR(doc.embedding, @query_vector, 10, "euclidean") // L2-Distance
```

---

### Geo-Queries

```aql
FOR doc IN locations
  GEO_DISTANCE(doc.lat, doc.lon, 52.52, 13.405) < 5000
  RETURN {name: doc.name, distance: GEO_DISTANCE(doc.lat, doc.lon, 52.52, 13.405)}
```

**Funktionen:**
- `GEO_DISTANCE(lat1, lon1, lat2, lon2)` - Haversine-Distanz (Meter)
- `GEO_BOX(lat, lon, minLat, maxLat, minLon, maxLon)` - Bounding-Box-Check

---

### Fulltext-Suche

```aql
FOR doc IN articles
  FULLTEXT(doc.content, "machine learning AI")
  SORT BM25(doc) DESC
  LIMIT 10
  RETURN {title: doc.title, score: BM25(doc)}
```

**Funktionen:**
- `FULLTEXT(field, query)` - Tokenisierte Suche
- `BM25(doc)` - Relevanz-Score (0.0+)

---

## Funktionen & Operatoren

### String-Funktionen

```aql
CONCAT(str1, str2, ...)       // "Hello" + " " + "World"
LOWER(str)                     // "HELLO" → "hello"
UPPER(str)                     // "hello" → "HELLO"
SUBSTRING(str, offset, length) // "Hello"[1:4] → "ell"
LENGTH(str)                    // "Hello" → 5
TRIM(str)                      // "  Hello  " → "Hello"
```

### Numeric-Funktionen

```aql
ABS(num)                       // |-5| → 5
CEIL(num) / FLOOR(num)         // 3.7 → 4 / 3
ROUND(num, decimals)           // 3.14159, 2 → 3.14
SQRT(num)                      // √16 → 4
POW(base, exp)                 // 2^8 → 256
```

### Aggregations (in COLLECT)

```aql
COUNT()                        // Anzahl Zeilen
SUM(expr)                      // Summe
AVG(expr)                      // Durchschnitt
MIN(expr) / MAX(expr)          // Minimum/Maximum
STDDEV(expr)                   // Standardabweichung
VARIANCE(expr)                 // Varianz
```

### Type-Checks

```aql
IS_NULL(value)
IS_NUMBER(value)
IS_STRING(value)
IS_ARRAY(value)
IS_OBJECT(value)
```

---

## Beispiel-Queries

### 1. Einfache Filterung

```aql
FOR user IN users
  FILTER user.age > 18 AND user.city == "Berlin"
  SORT user.created_at DESC
  LIMIT 10
  RETURN {
    name: user.name,
    email: user.email,
    age: user.age
  }
```

**Optimizer:**
- Nutzt Composite-Index `(city, age)` falls vorhanden
- Fallback: Equality-Index `city` + Full-Scan-Filter `age`

---

### 2. Geo-Proximity-Search

```aql
FOR loc IN restaurants
  FILTER GEO_DISTANCE(loc.lat, loc.lon, 52.52, 13.405) < 2000
  FILTER loc.rating >= 4.0
  SORT GEO_DISTANCE(loc.lat, loc.lon, 52.52, 13.405) ASC
  LIMIT 5
  RETURN {
    name: loc.name,
    rating: loc.rating,
    distance: GEO_DISTANCE(loc.lat, loc.lon, 52.52, 13.405)
  }
```

**Optimizer:**
- Nutzt Geo-Index für Bounding-Box-Scan
- Post-Filter für exakte Distanz-Berechnung

---

### 3. Vektor-Suche mit Filter

```aql
FOR product IN products
  NEAR(product.embedding, @query_vector, 20)
  FILTER product.price < 100.0 AND product.in_stock == true
  SORT SIMILARITY() DESC
  LIMIT 10
  RETURN {
    name: product.name,
    price: product.price,
    similarity: SIMILARITY()
  }
```

**Pre-Filtering vs Post-Filtering:**
- Pre-Filter: Bitset für `price < 100 AND in_stock == true` → k-NN
- Post-Filter: k-NN (20) → Filter → Top-10

---

### 4. Aggregationen

```aql
FOR order IN orders
  FILTER order.created_at >= "2025-01-01"
  COLLECT city = order.city
  AGGREGATE 
    total_revenue = SUM(order.amount),
    avg_order = AVG(order.amount),
    order_count = COUNT()
  SORT total_revenue DESC
  LIMIT 10
  RETURN {
    city,
    total_revenue,
    avg_order,
    order_count
  }
```

---

### 5. Graph-Traversierung

```aql
FOR vertex, edge, path IN 1..3 OUTBOUND "users/alice" friendships
  FILTER vertex.active == true
  RETURN {
    friend: vertex.name,
    connection_type: edge.type,
    path_length: LENGTH(path.edges)
  }
```

---

## Query-Execution & Optimizer

### Explain-Plan

```json
POST /query/aql
{
  "query": "FOR u IN users FILTER u.age > 18 SORT u.created_at DESC LIMIT 10",
  "explain": true
}
```

**Response:**
```json
{
  "explain_plan": {
    "steps": [
      {
        "type": "IndexScan",
        "index": "range_users_age",
        "condition": "age > 18",
        "estimated_rows": 1200
      },
      {
        "type": "Sort",
        "field": "created_at",
        "order": "DESC",
        "index_used": "range_users_created_at"
      },
      {
        "type": "Limit",
        "offset": 0,
        "count": 10
      }
    ],
    "estimated_cost": 145.3,
    "estimated_rows": 10
  }
}
```

### Index-Hints (später)

```aql
FOR doc IN users USE INDEX idx_age_city
  FILTER doc.age > 18
  RETURN doc
```

---

## AST-Struktur (Internal)

```cpp
// AST-Node-Typen
enum class ASTNodeType {
    ForNode,          // FOR variable IN collection
    FilterNode,       // FILTER condition
    SortNode,         // SORT expr [ASC|DESC]
    LimitNode,        // LIMIT offset, count
    ReturnNode,       // RETURN expression
    
    // Expressions
    BinaryOp,         // ==, !=, >, <, >=, <=, AND, OR
    UnaryOp,          // NOT, -
    FunctionCall,     // CONCAT, SUM, etc.
    FieldAccess,      // doc.field
    Literal,          // "string", 123, true, null
    Variable          // doc, user, etc.
};

// Beispiel-AST für: FOR u IN users FILTER u.age > 18 RETURN u.name
ForNode {
    variable: "u",
    collection: "users",
    
    filter: FilterNode {
        condition: BinaryOp {
            op: ">",
            left: FieldAccess("u", "age"),
            right: Literal(18)
        }
    },
    
    return_expr: ReturnNode {
        expression: FieldAccess("u", "name")
    }
}
```

---

## Implementierungs-Phasen

### Phase 1 (MVP - Woche 1-2):
- ✅ FOR, FILTER (Equality, Range, IN), SORT, LIMIT, RETURN
- ✅ Parser (PEGTL)
- ✅ AST → QueryEngine-Translation
- ✅ HTTP-Endpoint `/query/aql`
- ✅ Unit-Tests

### Phase 2 (Woche 3-4):
- LET (Variable Binding)
- COLLECT (Aggregationen: COUNT, SUM, AVG)
- String-/Numeric-Funktionen
- Explain-Plan-Integration

### Phase 3 (Woche 5-6):
- Graph-Traversierung (FOR v, e, p IN ... OUTBOUND)
- Vektor-Suche (NEAR, SIMILARITY)
- Geo-Queries (GEO_DISTANCE, GEO_BOX)
- Fulltext (FULLTEXT, BM25)

### Phase 4 (später):
- Joins (Multi-Collection)
- Subqueries
- Transactions (BEGIN, COMMIT, ROLLBACK)
- INSERT, UPDATE, DELETE via AQL

---

## Performance-Überlegungen

**Index-Nutzung:**
- FILTER mit `==` → Equality-Index
- FILTER mit `>`, `<` → Range-Index
- FILTER mit `IN` → Batch-Lookup
- SORT → Range-Index (wenn vorhanden)

**Optimizer-Strategien:**
- **Filter-Pushdown:** FILTER vor SORT (reduziert Sortier-Kosten)
- **Index-Auswahl:** Kleinster geschätzter Index zuerst
- **Short-Circuit:** LIMIT früh anwenden (z.B. Top-K)

**Vermeiden:**
- Full-Table-Scans ohne LIMIT
- Sortierung ohne Index auf großen Datasets
- Aggregationen ohne COLLECT (ineffizient)

---

## Kompatibilität & Erweiterungen

**ArangoDB AQL:**
- Ähnliche Syntax (FOR, FILTER, SORT, LIMIT, RETURN)
- Unterschiede: THEMIS nutzt natives MVCC, kein `_key` zwingend

**SQL-Vergleich:**
```sql
-- SQL
SELECT name, age FROM users WHERE age > 18 ORDER BY created_at DESC LIMIT 10;

-- AQL
FOR user IN users
  FILTER user.age > 18
  SORT user.created_at DESC
  LIMIT 10
  RETURN {name: user.name, age: user.age}
```

**Vorteile AQL:**
- Multi-Modell (Graph, Vector, Geo in einer Query)
- Explizite Execution-Reihenfolge (leichter zu optimieren)
- Schemalos (flexible Felder)

---

## Fehlerbehandlung

**Syntax-Errors:**
```json
{
  "error": "Syntax error at line 2, column 10: Expected 'IN' after variable name",
  "query": "FOR user users FILTER ...",
  "line": 2,
  "column": 10
}
```

**Semantic-Errors:**
```json
{
  "error": "Collection 'userz' does not exist (did you mean 'users'?)",
  "query": "FOR u IN userz RETURN u"
}
```

**Runtime-Errors:**
```json
{
  "error": "Division by zero in expression: amount / quantity",
  "entity_key": "orders:12345"
}
```

---

## Referenz-Links

- **Parser:** PEGTL (https://github.com/taocpp/PEGTL)
- **Inspiration:** ArangoDB AQL (https://www.arangodb.com/docs/stable/aql/)
- **Optimizer:** docs/query_optimizer.md
- **Index-Typen:** docs/indexes.md

---

**Status:** ✅ Syntax-Definition vollständig  
**Nächster Schritt:** Parser-Implementation mit PEGTL
