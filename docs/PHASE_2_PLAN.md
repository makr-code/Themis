# Phase 2: AQL Syntax Sugar für Hybrid Queries - Implementation Plan

**Datum:** 17. November 2025  
**Branch:** `feature/aql-st-functions`  
**Status:** ✅ Phase 2 + 2.5 abgeschlossen (SIMILARITY, PROXIMITY, SHORTEST_PATH, spezialisierte AST-Knoten, Composite Index Prefilter, erweiterte Kostenmodelle, Graph-Optimierung, Benchmark Suite)

---

## Übersicht

Phase 2 erweitert AQL mit Syntax-Zucker für Hybrid Queries, sodass diese elegant und intuitiv in AQL geschrieben werden können.

---

## Geplante Features

### 1. SIMILARITY() Funktion für Vector+Geo Queries

**Syntax:**
```aql
FOR doc IN entities
  FILTER ST_Within(doc.location, @region)
  SORT SIMILARITY(doc.embedding, @queryVector) DESC
  LIMIT 10
  RETURN doc
```

**Implementation:**
- Neue FunctionCall: `SIMILARITY(vectorField, queryVector)`
- Parser: Erkennt SIMILARITY in SORT-Klausel
- Translator: Generiert `executeVectorGeoQuery()` statt separater FOR/FILTER/SORT
- Query Optimizer: Kombiniert ST_* Filter + SIMILARITY automatisch

**Vorteile:**
- ✅ Natürliche AQL-Syntax
- ✅ Automatische Optimierung (HNSW + Spatial Index)
- ✅ Backwards compatible (funktioniert auch ohne Indexes)

---

### 2. Graph Traversal mit Spatial Constraints

**Syntax:**
```aql
FOR v, e, p IN 1..10 OUTBOUND "city:berlin" edges
  FILTER ST_Within(v.location, @germanyPolygon)
  SHORTEST_PATH TO "city:dresden"
  RETURN p
```

**Implementation:**
- Neue Keyword: `SHORTEST_PATH TO <target>`
- Parser: Erkennt Graph-Traversal + Spatial FILTER auf Vertex
- Translator: Generiert `executeRecursivePathQuery()` mit spatialConstraint
- Automatisches Batch Loading für Vertices

**Vorteile:**
- ✅ Intuitive Graph+Geo Syntax
- ✅ Automatische Batch-Optimierung
- ✅ Konsistent mit bestehender Graph-Syntax

---

### 3. PROXIMITY() Funktion für Content+Geo

**Syntax:**
```aql
FOR doc IN places
  FILTER FULLTEXT(doc.description, "coffee shop")
  SORT PROXIMITY(doc.location, @myPosition) ASC
  LIMIT 20
  RETURN doc
```

**Implementation:**
- Neue FunctionCall: `PROXIMITY(geoField, point)`
- Parser: Erkennt FULLTEXT + PROXIMITY Kombination
- Translator: Generiert `executeContentGeoQuery()` mit distance boosting
- Query Optimizer: Verwendet Spatial Index wenn verfügbar

**Vorteile:**
- ✅ Klare Semantik (Nähe statt Distance)
- ✅ Automatische Distance-Berechnung
- ✅ Optional: Distance in Metern in RETURN

---

### 4. Kombinierte Hybrid Queries (Advanced)

**Syntax:**
```aql
// Vector + Graph + Geo (Triple Hybrid)
FOR v, e, p IN 1..5 OUTBOUND @startNode edges
  FILTER ST_DWithin(v.location, @center, 5000)
  LET similarity = SIMILARITY(v.features, @queryVector)
  FILTER similarity > 0.7
  SORT similarity DESC
  LIMIT 10
  RETURN {path: p, vertex: v, similarity: similarity}
```

**Implementation:**
- Parser: Erkennt mehrere Hybrid-Features in einer Query
- Translator: Generiert optimierten Multi-Hybrid Query Plan
- Query Optimizer: Cost-based Entscheidung für Filter-Reihenfolge

---

## Parser-Erweiterungen

### Neue Keywords

```cpp
enum class TokenType {
    // Existing...
    FOR, IN, FILTER, SORT, LIMIT, RETURN, LET,
    
    // Phase 2: Hybrid Query Keywords
    SIMILARITY,        // SIMILARITY(vector, query)
    PROXIMITY,         // PROXIMITY(geo, point)
    SHORTEST_PATH,     // SHORTEST_PATH TO target
    FULLTEXT,          // FULLTEXT(field, query)
    
    // Existing...
};
```

### Neue Expression Types

```cpp
// Extend FunctionCallExpr für spezielle Hybrid Functions
struct SimilarityExpr : Expression {
    std::shared_ptr<Expression> vectorField;
    std::shared_ptr<Expression> queryVector;
    
    ASTNodeType getType() const override { return ASTNodeType::SimilarityCall; }
};

struct ProximityExpr : Expression {
    std::shared_ptr<Expression> geoField;
    std::shared_ptr<Expression> point;
    
    ASTNodeType getType() const override { return ASTNodeType::ProximityCall; }
};
```

---

## Query Optimizer Enhancements

### Automatic Hybrid Query Detection

```cpp
class HybridQueryOptimizer {
public:
    // Detect pattern: FILTER ST_* + SORT SIMILARITY
    static bool isVectorGeoQuery(const ASTNode& ast);
    
    // Detect pattern: Graph Traversal + FILTER ST_* on vertex
    static bool isGraphGeoQuery(const ASTNode& ast);
    
    // Detect pattern: FULLTEXT + SORT PROXIMITY
    static bool isContentGeoQuery(const ASTNode& ast);
    
    // Transform AST to optimized execution plan
    static ExecutionPlan optimize(ASTNode& ast);
};
```

### Cost-Based Optimization

```cpp
struct QueryCost {
    double estimatedRows;
    double estimatedTimeMs;
    bool usesHNSW;
    bool usesSpatialIndex;
    bool usesBatchLoading;
};

class CostEstimator {
public:
    // Estimate cost for different execution strategies
    QueryCost estimateVectorGeo(const Query& q, bool hasIndexes);
    QueryCost estimateGraphGeo(const Query& q, int maxDepth);
    QueryCost estimateContentGeo(const Query& q, bool hasFulltext);
    
    // Choose optimal execution order
    ExecutionPlan chooseBestPlan(const std::vector<ExecutionPlan>& candidates);
};
```

---

## Implementation Roadmap

### Phase 2.1: SIMILARITY() Function ⭐ Abgeschlossen

**Tasks:**
1. ✅ Keyword SIMILARITY im Tokenizer
2. ✅ Parser erkennt SIMILARITY als FunctionCall in SORT
3. ✅ SimilarityCallExpr spezialisierter AST Node (Parser ersetzt FunctionCall)
4. ✅ Translator: Erkennung + Erzeugung VectorGeoQuery
5. ✅ Dispatcher: executeAql() ruft executeVectorGeoQuery()
6. ✅ Tests: Parsing / Übersetzung / Dispatch
7. ✅ Zusätzliche Gleichheits-/Range-Prädikate neben Spatial Filter (extra_filters)
8. ✅ Gleichheits-Prädikate extrahiert & Index-Prefilter (Whitelist für ANN / Plan-Kostenmodell)

**Estimated:** 4-6 hours

**Example (mit zusätzlichem Predicate):**
```aql
FOR doc IN hotels
  FILTER ST_Within(doc.location, POLYGON(...))
  FILTER doc.city == "Berlin"
  SORT SIMILARITY(doc.description_embedding, @queryVec) DESC
  LIMIT 10
  RETURN doc
```

---

### Phase 2.2: Graph Spatial Constraints ✅ Abgeschlossen

**Tasks:**
1. ✅ Add SHORTEST_PATH keyword
2. ✅ Extend parser for Graph + FILTER pattern
3. ✅ Implement spatial constraint extraction
4. ✅ Generate executeRecursivePathQuery() with constraints
5. ✅ Add integration tests

**Estimated:** 3-4 hours

**Example:**
```aql
FOR v IN 1..10 OUTBOUND @start edges
  FILTER ST_Within(v.location, @boundary)
  SHORTEST_PATH TO @target
  RETURN v
```

---

### Phase 2.3: PROXIMITY() Function ✅ Abgeschlossen

**Tasks:**
1. ✅ Add PROXIMITY keyword
2. ✅ Implement ProximityExpr AST node
3. ✅ Detect FULLTEXT + PROXIMITY pattern
4. ✅ Generate executeContentGeoQuery()
5. ✅ Add distance calculation
6. ✅ Add integration tests

**Estimated:** 3-4 hours

**Example:**
```aql
FOR doc IN restaurants
  FILTER FULLTEXT(doc.menu, "vegan")
  SORT PROXIMITY(doc.location, ST_Point(13.4, 52.5)) ASC
  LIMIT 20
  RETURN doc
```

---

### Phase 2.4: Query Optimizer ✅ Erstes Kostenmodell integriert

**Tasks:**
1. ✅ Erweiterung bestehender QueryOptimizer (Predicate Reihenfolge + VectorGeo Kostenmodell)
2. ✅ Kostenabschätzung Vector+Geo (Spatial-first vs Vector-first) + Prefilter Rabatt
3. ✅ Integration in `executeVectorGeoQuery` (Span-Attribute für Plan & Kosten)
4. ✅ Tests: `test_query_optimizer_vector_geo.cpp`
5. ✅ Stub-Kostenmodelle für Content+Geo & Graph-Pfade (Future Erweiterung)

**Estimated:** 6-8 hours

**Priority:** Low (system already performant without optimizer)

---

## Testing Strategy

### Unit Tests

```cpp
// tests/test_aql_hybrid_syntax.cpp

TEST(AQLHybridSyntax, ParseSimilarityFunction) {
    std::string aql = R"(
        FOR doc IN entities
        SORT SIMILARITY(doc.vec, @query) DESC
        LIMIT 10
        RETURN doc
    )";
    
    auto ast = AQLParser::parse(aql);
    
    // Verify SIMILARITY node exists
    EXPECT_TRUE(hasSimilarityCall(ast));
}

TEST(AQLHybridSyntax, TranslateVectorGeoQuery) {
    std::string aql = R"(
        FOR doc IN entities
        FILTER ST_Within(doc.location, @region)
        SORT SIMILARITY(doc.embedding, @query) DESC
        LIMIT 10
        RETURN doc
    )";
    
    auto plan = AQLTranslator::translate(aql);
    
    // Verify it generates executeVectorGeoQuery
    EXPECT_EQ(plan.type, ExecutionPlanType::VECTOR_GEO_HYBRID);
}
```

### Integration Tests

```cpp
// tests/test_aql_hybrid_integration.cpp

TEST(AQLHybridIntegration, VectorGeoQueryEndToEnd) {
    // Setup test data + indexes
    setupHotelsWithVectorsAndGeometry();
    
    std::string aql = R"(
        FOR hotel IN hotels
        FILTER ST_Within(hotel.location, @berlinPolygon)
        SORT SIMILARITY(hotel.features, @luxuryQuery) DESC
        LIMIT 5
        RETURN hotel
    )";
    
    auto results = queryEngine.executeAQL(aql, params);
    
    EXPECT_EQ(results.size(), 5);
    // Verify results are sorted by similarity
    // Verify all results are within Berlin
}
```

---

## Performance Targets (Phase 2)

| Feature | Target | Complexity |
|---------|--------|------------|
| **SIMILARITY() parsing** | <1ms | Low |
| **Vector+Geo translation** | <5ms end-to-end | Medium |
| **Graph+Geo parsing** | <1ms | Medium |
| **PROXIMITY() parsing** | <1ms | Low |
| **Query optimization** | <10ms (optional) | High |

---

## Backwards Compatibility

**CRITICAL:** Alle Phase 2 Features sind **100% backwards compatible**:

1. ✅ Alte Queries funktionieren weiterhin
2. ✅ Neue Syntax ist **optional** (C++ API bleibt verfügbar)
3. ✅ Fallback zu unoptimierter Ausführung wenn Syntax nicht erkannt
4. ✅ Keine Breaking Changes in Parser/Translator

---

## Migration Path

### Für Benutzer

**Option 1: Weiter C++ API verwenden**
```cpp
// Funktioniert weiterhin
auto results = qe.executeVectorGeoQuery(table, vecField, query, k, filter);
```

**Option 2: Neue AQL Syntax verwenden**
```aql
-- Eleganter, gleiche Performance
FOR doc IN table
  FILTER ST_Within(doc.geo, @region)
  SORT SIMILARITY(doc.vec, @query) DESC
  LIMIT 10
  RETURN doc
```

**Beide Optionen generieren identischen Execution Plan!**

---

## Documentation Plan

### User-Facing Docs

1. **AQL Hybrid Queries Guide** (`docs/aql-hybrid-queries.md`)
   - SIMILARITY() examples
   - Graph+Geo examples
   - PROXIMITY() examples
   - Performance tips

2. **AQL Reference** (update existing)
   - Add SIMILARITY to function list
   - Add PROXIMITY to function list
   - Add SHORTEST_PATH examples

### Developer Docs

3. **Parser Extension Guide** (`docs/dev/parser-extensions.md`)
   - How to add new functions
   - AST node creation
   - Translation patterns

---

## Open Questions

1. **SIMILARITY() return value:**
   - Option A: Only for SORT (implicit)
   - Option B: Also in LET (explicit): `LET sim = SIMILARITY(doc.vec, @q)`
   - **Decision:** Start with A, add B in Phase 2.5

2. **PROXIMITY() units:**
   - Meters? Kilometers? Configurable?
   - **Decision:** Meters (consistent with ST_DWithin)

3. **Optimizer complexity:**
   - Full cost-based optimizer or simple pattern matching?
   - **Decision:** Start with pattern matching (Phase 2.1-2.3), add costs later (Phase 2.4)

---

## Dependencies

**Required:**
- Phase 1.5 (Hybrid Query C++ API) ✅ COMPLETED

**Optional:**
- Statistics collector for cost estimation (Phase 2.4)
- Query plan visualizer (debugging tool)

---

## Success Criteria

Phase 2 is successful when:

1. ✅ SIMILARITY() function works in AQL
2. ✅ Graph+Geo syntax works (FILTER on vertex + SHORTEST_PATH)
3. ✅ PROXIMITY() function works in AQL
4. ✅ Generated execution plans match C++ API performance
5. ✅ 100% backwards compatible
6. ✅ Comprehensive tests (unit + integration)
7. ✅ Documentation complete

---

## Timeline Estimate

| Phase | Tasks | Duration |
|-------|-------|----------|
| **2.1** | SIMILARITY() | 4-6 hours |
| **2.2** | Graph+Geo | 3-4 hours |
| **2.3** | PROXIMITY() | 3-4 hours |
| **2.4** | Optimizer (opt) | 6-8 hours |
| **Docs** | All docs | 2-3 hours |
| **Testing** | Full coverage | 3-4 hours |
| **TOTAL** | | **21-29 hours** |

**Realistic:** 3-4 working days

---

## Phase 2.5 Follow-Up Tasks ✅ ABGESCHLOSSEN

### 1. Erweiterte Predicate-Normalisierung ✅
- **Status:** Implementiert
- Equality + Range + Composite Index Prefiltering
- `scanKeysEqualComposite()` Integration in `executeVectorGeoQuery`
- Automatische Erkennung von AND-Ketten für Composite Indizes
- Span-Attribut: `composite_prefilter_applied`

### 2. Content+Geo Erweitertes Kostenmodell ✅
- **Status:** Implementiert
- Planwahl zwischen Fulltext-first und Spatial-first
- Heuristisches Modell mit `bboxRatio` und geschätzten FT-Hits
- Naive Token-AND Evaluation im Spatial-first Pfad
- Span-Attribute: `optimizer.cg.plan`, `optimizer.cg.cost_fulltext_first`, `optimizer.cg.cost_spatial_first`

### 3. Graph-Pfad Optimierung ✅
- **Status:** Implementiert
- Dynamische Branching-Faktor-Schätzung (Sampling über erste 2 Tiefen)
- Frühabbruch bei geschätzter Expansion >1M Vertices
- Räumliche Selektivität in Kostenmodell integriert
- Span-Attribute: `optimizer.graph.branching_estimate`, `optimizer.graph.expanded_estimate`, `optimizer.graph.aborted`

### 4. Benchmark Suite Hybrid Sugar ✅
- **Status:** Implementiert
- `benchmarks/bench_hybrid_aql_sugar.cpp` erstellt
- Vergleich: AQL Sugar vs C++ API (Vector+Geo, Content+Geo)
- Parse+Translate Overhead isoliert gemessen
- 1000 Hotels Testdaten mit Indizes
- CMakeLists.txt Target hinzugefügt

### 5. Dokumentation Kostenmodelle ✅
- **Status:** Erweitert
- `docs/dev/cost-models.md` mit allen drei Modellen (Vector+Geo, Content+Geo, Graph)
- Detaillierte Formeln, Tuning-Parameter, Grenzen
- Tracer-Attribute dokumentiert

### 6. Hybrid Queries Doku ✅
- **Status:** Aktualisiert
- `docs/aql-hybrid-queries.md` mit Composite Index Beispielen
- Kostenmodell-Planwahl Details für alle Hybrid-Typen
- Tracer-Attribute für Observability
- Performance Hinweise erweitert

---

## Next Steps (Phase 3 / Future Work)

### Empfohlene nächste Features (priorisiert):

#### Option A: Phase 3 - Advanced AQL Features (Höchste User Value)
1. **Subqueries & Common Table Expressions (CTEs)**
   - `WITH temp AS (...) FOR doc IN temp ...`
   - Erhebliche Verbesserung der Query-Ausdruckskraft
   - Wiederverwendung von Zwischenergebnissen
   - **Aufwand:** 12-16 Stunden
   
2. **JOIN Operations**
   - `FOR doc1 IN table1 FOR doc2 IN table2 FILTER doc1.ref == doc2._id`
   - Nested Loop + Optional Hash Join Optimizer
   - **Aufwand:** 16-20 Stunden
   
3. **Window Functions**
   - `ROW_NUMBER() OVER (PARTITION BY ... ORDER BY ...)`
   - Rank, Dense Rank, Lag, Lead
   - **Aufwand:** 10-14 Stunden

#### Option B: Production Readiness (Höchste Stabilität)
1. **Query Plan Cache**
   - Parsed AST caching (LRU Cache)
   - Reduziert Parse-Overhead bei wiederholten Queries
   - **Aufwand:** 6-8 Stunden
   
2. **Query Timeout & Resource Limits**
   - Max execution time, max memory per query
   - Graceful abort bei Überschreitung
   - **Aufwand:** 8-10 Stunden
   
3. **Enhanced Error Messages**
   - Detaillierte Parse-Fehler mit Zeilennummer/Spalte
   - Query-Explain für Debugging
   - **Aufwand:** 6-8 Stunden

#### Option C: Performance & Scale (Höchste Performance)
1. **Parallel Query Execution**
   - Parallel FOR-Loop Processing (TBB Thread Pool)
   - Chunk-basierte Verteilung
   - **Aufwand:** 12-16 Stunden
   
2. **Adaptive Query Optimizer**
   - Runtime Statistics Collection
   - Plan-Cache mit Statistics-basierter Invalidierung
   - **Aufwand:** 16-20 Stunden
   
3. **Batch Processing API**
   - Multi-Query Batch Execution
   - Amortisierte Parse-Kosten
   - **Aufwand:** 8-10 Stunden

#### Option D: Multi-Model Enhancements (Breite Features)
1. **Graph Pattern Matching (OpenCypher-Style)**
   - `MATCH (a:City)-[:ROAD*1..5]->(b:City)`
   - Deklarative Graph Queries
   - **Aufwand:** 20-24 Stunden
   
2. **Vector Index Improvements**
   - Product Quantization (PQ) für Memory-Effizienz
   - IVF-HNSW Hybrid für sehr große Datensätze
   - **Aufwand:** 16-20 Stunden
   
3. **Fulltext Ranking Improvements**
   - TF-IDF neben BM25
   - Phrase Matching
   - **Aufwand:** 10-12 Stunden

---

**Empfehlung:** Start mit **Option A (Subqueries)** – größter User Value bei moderatem Aufwand.

---

**Status:** Phase 2 + 2.5 Complete ✅  
**Next Priority:** Subqueries / CTEs (Option A.1)
