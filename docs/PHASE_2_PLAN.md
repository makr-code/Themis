# Phase 2: AQL Syntax Sugar für Hybrid Queries - Implementation Plan

**Datum:** 17. November 2025  
**Branch:** `feature/aql-st-functions`  
**Status:** 🚧 In Planning

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

### Phase 2.1: SIMILARITY() Function ⭐ HIGH PRIORITY

**Tasks:**
1. ✅ Add SIMILARITY keyword to tokenizer
2. ✅ Extend parser to recognize SIMILARITY(field, vector)
3. ✅ Implement SimilarityExpr AST node
4. ✅ Extend translator to detect Vector+Geo pattern
5. ✅ Generate executeVectorGeoQuery() call
6. ✅ Add integration tests

**Estimated:** 4-6 hours

**Example:**
```aql
FOR doc IN hotels
  FILTER ST_Within(doc.location, POLYGON(...))
  SORT SIMILARITY(doc.description_embedding, @queryVec) DESC
  LIMIT 10
  RETURN doc
```

---

### Phase 2.2: Graph Spatial Constraints

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

### Phase 2.3: PROXIMITY() Function

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

### Phase 2.4: Query Optimizer (Optional)

**Tasks:**
1. ⏳ Implement HybridQueryOptimizer
2. ⏳ Add cost estimation
3. ⏳ Implement plan selection
4. ⏳ Add optimizer tests

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

## Next Steps

1. Start with Phase 2.1 (SIMILARITY function)
2. Extend tokenizer + parser
3. Implement translator pattern detection
4. Add integration tests
5. Document + commit
6. Repeat for 2.2, 2.3

---

**Status:** Ready to implement Phase 2.1  
**Priority:** SIMILARITY() function (highest user value)
