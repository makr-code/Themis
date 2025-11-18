# Hybrid Multi-Model Queries - Quick Start

**Status:** ✅ Production-Ready (Phase 1.5 Optimized)  
**Performance:** <5ms für Vector+Geo, 20-50ms für Graph+Geo

---

## Was sind Hybrid Queries?

Hybrid Queries kombinieren **mehrere Datenmodelle** in einer einzigen Query:

- **Vector + Geo:** Semantische Ähnlichkeit + räumliche Nähe
- **Graph + Geo:** Graphtraversierung mit räumlichen Constraints
- **Content + Geo:** Volltextsuche + Geo-Proximity-Ranking

---

## Quick Start

### 1. Vector+Geo Query

Finde ähnliche Vektoren in einer bestimmten Region:

```cpp
// Setup
VectorIndexManager vectorIdx(db, "entities", "embedding", 384);
SpatialIndexManager spatialIdx(db);
QueryEngine qe(db, &secIdx, &graphIdx, &vectorIdx, &spatialIdx);

// Query: "Finde 10 ähnlichste Hotels in Berlin"
std::vector<float> queryVec = getQueryEmbedding("luxury hotel");

Condition spatialFilter;
spatialFilter.function_name = "ST_Within";
spatialFilter.args = {"location", "POLYGON((13.3 52.5, ...))"};

auto results = qe.executeVectorGeoQuery(
    "entities",
    "embedding",
    queryVec,
    10,  // top-k
    spatialFilter
);

// Performance: <5ms @ 1000 candidates (mit HNSW+Spatial Index)
```

### 2. Graph+Geo Query

Traversiere Graph mit räumlichen Constraints:

```cpp
// Query: "Finde Weg von Berlin nach Dresden, nur durch Deutschland"
RecursivePathQuery query;
query.startVertex = "city:berlin";
query.targetVertex = "city:dresden";
query.mode = RecursivePathQuery::Mode::SHORTEST_PATH;
query.maxDepth = 10;

// Spatial constraint: Nur Vertices in Deutschland
query.spatialConstraint = Condition{
    .function_name = "ST_Within",
    .args = {"location", "POLYGON((...))"}  // Deutschland-Polygon
};

auto result = qe.executeRecursivePathQuery(query);

// Performance: 20-50ms @ BFS depth 5 (mit Batch Loading)
```

### 3. Content+Geo Query

Volltextsuche mit Geo-Proximity-Ranking:

```cpp
// Query: "Finde 'coffee shop' in meiner Nähe"
auto results = qe.executeContentGeoQuery(
    "places",
    "description",
    "coffee shop",
    geo::Point{13.4, 52.5},  // Meine Position
    100,  // max results
    5000  // max distance (meters)
);

// Results sind nach (BM25-Score × Distance-Boost) sortiert
// Performance: ~20-80ms @ 100 fulltext results
```

---

## Performance-Optimierungen (Phase 1.5)

### Automatische Optimierungen

Wenn Index Manager verfügbar sind, aktivieren sich automatisch:

| Optimierung | Speedup | Aktivierung |
|-------------|---------|-------------|
| **HNSW** | 10× | `VectorIndexManager` vorhanden |
| **Spatial Index** | 100× | `SpatialIndexManager` vorhanden |
| **Batch Loading** | 5× | Immer aktiv |

### Setup mit Optimierungen

```cpp
// Create index managers
VectorIndexManager vectorIdx(db, "entities", "embedding", 384);
SpatialIndexManager spatialIdx(db);

// Populate indexes
vectorIdx.addVector("pk1", embedding1);
spatialIdx.insertGeometry("entities", "pk1", geometry1);

// Create optimized QueryEngine
QueryEngine qe(
    db,
    &secIdx,
    &graphIdx,
    &vectorIdx,    // Aktiviert HNSW
    &spatialIdx    // Aktiviert Spatial Index
);

// Queries verwenden automatisch optimierte Pfade!
```

### Fallback ohne Indexes

```cpp
// QueryEngine ohne Index Manager
QueryEngine qe(db, &secIdx, &graphIdx);

// Queries funktionieren weiterhin (mit Fallback-Algorithmen):
// - Brute-force L2 distance statt HNSW
// - Full table scan statt Spatial Index
// - Immer noch korrekte Ergebnisse, nur langsamer
```

---

## Testing

```bash
# Run all hybrid query tests
./build/themis_tests --gtest_filter="HybridQueriesTest.*"

# Run specific test
./build/themis_tests --gtest_filter="HybridQueriesTest.VectorGeo_WithVectorIndexManager_UsesHNSW"
```

**7 Integration Tests:**
- Vector+Geo (mit/ohne HNSW)
- Graph+Geo (BFS + Dijkstra)
- Content+Geo (Fulltext + Proximity)
- Edge Cases

---

## AQL Syntax (Geplant für Phase 2)

```aql
-- Vector+Geo (Syntax-Zucker, noch nicht implementiert)
FOR doc IN entities
  FILTER ST_Within(doc.location, @region)
  SORT SIMILARITY(doc.embedding, @queryVec) DESC
  LIMIT 10
  RETURN doc

-- Graph+Geo (Syntax-Zucker, noch nicht implementiert)
FOR v, e, p IN 1..10 OUTBOUND "city:berlin" edges
  FILTER ST_Within(v.location, @germany)
  SHORTEST_PATH TO "city:dresden"
  RETURN p
```

---

## Dokumentation

- **[Phase 1.5 Optimization Guide](./hybrid-queries-phase1.5.md)** - Detaillierte Optimierungs-Dokumentation
- **[Database Capabilities Roadmap](./DATABASE_CAPABILITIES_ROADMAP.md)** - Feature Overview
- **[Completion Report](./PHASE_1.5_COMPLETION_REPORT.md)** - Abschlussbericht

---

## Performance-Referenz

**Vector+Geo:**
- HNSW + Spatial Index: <5ms @ 1000 candidates ✅
- Spatial Index only: <20ms @ 1000 candidates
- Fallback (no indexes): 50-100ms @ 1000 candidates

**Graph+Geo:**
- Mit Batch Loading: 20-50ms @ BFS depth 5 ✅
- Ohne Batch Loading: 100-200ms @ BFS depth 5

**Content+Geo:**
- Bereits optimiert: 20-80ms @ 100 fulltext results ✅

---

## FAQ

**Q: Muss ich Index Manager verwenden?**  
A: Nein! Queries funktionieren auch ohne Indexes (mit Fallback-Algorithmen).

**Q: Wie aktiviere ich Optimierungen?**  
A: Einfach Index Manager an QueryEngine übergeben - Optimierungen aktivieren sich automatisch.

**Q: Sind Hybrid Queries production-ready?**  
A: Ja! Alle Performance-Ziele erreicht, 7 Integration Tests, vollständig dokumentiert.

**Q: Gibt es Breaking Changes?**  
A: Nein! 100% backwards compatible durch Optional Dependencies Pattern.

---

**Branch:** `feature/aql-st-functions`  
**Commit:** `687b399`  
**Status:** ✅ Production-Ready
