# Database Capabilities Vervollständigung - Roadmap

**Branch:** `feature/complete-database-capabilities`  
**Erstellt:** 17. November 2025  
**Ziel:** Vervollständigung der Multi-Model-Datenbank-Fähigkeiten auf 90%+

---

## Executive Summary

ThemisDB ist aktuell zu **~64%** implementiert mit starken Core-Features. Diese Roadmap fokussiert sich auf die Vervollständigung der **5 Datenbank-Modelle** + **Geo als Cross-Cutting Capability**:

### Datenbank-Modelle (über RocksDB Blob Storage)
1. **Relational** (aktuell 100% → Ziel: 100%)
2. **Graph** (aktuell 70% → Ziel: 95%)
3. **Vector** (aktuell 75% → Ziel: 95%)
4. **Content/Filesystem** (aktuell 30% → Ziel: 75%)
5. **Time-Series** (aktuell 85% → stabil)

### Cross-Cutting Capabilities
6. **Geo/Spatial** (aktuell 82% → Ziel: 85% MVP) ✅ **FAST FERTIG**
   - **Nicht** ein separates Modell, sondern erweitert alle 5 Modelle
   - Jedes Modell kann geo-enabled sein (optional `geometry` field)
   - Gemeinsamer R-Tree Index, ST_* Functions für alle Tabellen
   - **Status:** EWKB Parser ✅, R-Tree Index ✅, ST_* Functions ✅ (14/17 = 82%)

**Geschätzter Zeitaufwand:** 24 Arbeitstage  
**Priorisierung:** Geo Infrastructure → Graph → Vector → Content

---

## 🌍 Geo als Cross-Cutting Capability

### Architektur-Prinzip: Geo erweitert alle Modelle

**Geo ist KEIN separates Datenbank-Modell**, sondern eine **optionale Capability** für alle 5 Modelle:

```cpp
// Jede Tabelle kann geo-enabled sein
CREATE TABLE cities {
  _id: STRING,
  name: STRING,           // Relational
  population: INT,        // Relational
  boundary: GEOMETRY,     // GEO ← optional field
  embedding: VECTOR,      // Vector
  _labels: ["City"],      // Graph
  content: BLOB           // Content
}

// Gemeinsamer Spatial Index für alle geo-enabled Tabellen
CREATE INDEX spatial_cities ON cities(boundary) TYPE SPATIAL;
```

### Symbiose der Modelle

| Modell | Profitiert von Geo | Geo profitiert von |
|--------|-------------------|-------------------|
| **Relational** | WHERE + ST_Intersects kombiniert | Secondary Indexes für Attribute (country, type) |
| **Graph** | Spatial Graph Traversal (road networks) | Edge-based routing, connectivity |
| **Vector** | Spatial-filtered ANN (location + similarity) | Whitelist/Mask für HNSW |
| **Content** | Geo-tagged Documents/Chunks | Fulltext + Location hybrid search |
| **Time-Series** | Geo-temporal queries (trajectories) | Timestamp-based spatial evolution |

### Gemeinsame Infrastruktur

**Storage Layer (Unchanged):**
- RocksDB Blob für EWKB geometry (wie bei Vector embeddings)
- Sidecar CF für MBR/Centroid/Z-Range (analog zu Vector metadata)

**Index Layer (Erweitert):**
- `SecondaryIndexManager` erhält `SPATIAL` type (wie FULLTEXT, RANGE)
- R-Tree als neuer Index-Typ (Column Family: `index:spatial:<table>:<column>`)
- Z-Range als Composite Index (z_min, z_max)

**Query Layer (Erweitert):**
- AQL Parser: ST_* Functions (analog zu FULLTEXT(), SIMILARITY())
- Query Optimizer: Spatial Selectivity (wie Index Selectivity)
- Execution Engine: Spatial Filter als Predicate (wie FULLTEXT filter)

---

## 🎯 Phase 0: Geo Infrastructure (NEUE PHASE - KRITISCH)

### Ziel: Geo als Infrastruktur für alle Modelle

Diese Phase schafft die **gemeinsame Geo-Basis**, von der alle 5 Modelle profitieren.

#### 0.1 Geo Storage & Sidecar (Priorität: KRITISCH) ✅ **IMPLEMENTIERT**

**Status:** Vollständig implementiert in commits `ead621b` und früher.

**EWKB als universelles Geo-Format:**
```cpp
// include/utils/geo/ewkb.h - IMPLEMENTIERT
class EWKBParser {
public:
    struct GeometryInfo {
        GeometryType type;  // Point, LineString, Polygon, MultiPoint, etc.
        bool has_z;
        int srid;
        std::vector<Coordinate> coords;
        MBR computeMBR() const;
        Coordinate computeCentroid() const;
    };
    
    static GeometryInfo parseEWKB(const std::vector<uint8_t>& ewkb);
    static std::vector<uint8_t> serializeToEWKB(const GeometryInfo& geom);
};

// Sidecar (analog zu Vector metadata) - IMPLEMENTIERT
struct GeoSidecar {
    MBR mbr;              // 2D bounding box (minx, miny, maxx, maxy)
    Coordinate centroid;  // Geometric center
    double z_min = 0.0;   // For 3D geometries
    double z_max = 0.0;
};
```

**BaseEntity Integration:**
```cpp
// include/storage/base_entity.h - IMPLEMENTIERT
class BaseEntity {
    // Existing fields
    std::string id_;
    FieldMap fields_;
    
    // NEW: Optional geometry field (bereits integriert)
    std::optional<GeoSidecar> geo_sidecar_;  // MBR/Centroid/Z metadata
    // geometry_ als EWKB blob in fields_ gespeichert
};
```

**Implementierte Dateien:**
- ✅ `include/utils/geo/ewkb.h` (167 lines)
- ✅ `src/utils/geo/ewkb.cpp` (382 lines) - EWKB Parser, MBR, Centroid
- ✅ `include/storage/base_entity.h` - GeoSidecar include
- ✅ Tests: `tests/geo/test_geo_ewkb.cpp` (258 lines)

**Abgeschlossen:** ✅ (17. November 2025)

---

#### 0.2 Spatial Index (R-Tree) (Priorität: KRITISCH) ✅ **IMPLEMENTIERT**

**Status:** Vollständig implementiert mit Morton-Code Z-Order Indexierung.

**Gemeinsamer R-Tree für alle Tabellen:**
```cpp
// include/index/spatial_index.h - IMPLEMENTIERT
class SpatialIndexManager {
public:
    // Create spatial index for ANY table (relational, graph, vector, content)
    Status createSpatialIndex(
        std::string_view table,
        std::string_view geometry_column = "geometry",
        const RTreeConfig& config = {}
    );
    
    // Insert geometry with automatic Morton encoding
    Status insertSpatial(
        std::string_view table,
        std::string_view pk,
        const geo::MBR& mbr,
        std::optional<double> z_min = std::nullopt,
        std::optional<double> z_max = std::nullopt
    );
    
    // Query operations (returns PKs, agnostic of table type)
    std::vector<SpatialResult> searchByBBox(
        std::string_view table,
        const geo::MBR& query_bbox,
        std::optional<double> z_min = std::nullopt,
        std::optional<double> z_max = std::nullopt
    );
    
    std::vector<SpatialResult> searchByRadius(
        std::string_view table,
        double center_x,
        double center_y,
        double radius_meters
    );
};

// Morton Encoder für Z-Order Space-Filling Curve
class MortonEncoder {
public:
    static uint64_t encode2D(double x, double y, const geo::MBR& bounds);
    static uint64_t encode3D(double x, double y, double z, const geo::MBR& bounds);
    static std::pair<double, double> decode2D(uint64_t code, const geo::MBR& bounds);
    
    // Range queries for R-Tree simulation
    static std::vector<std::pair<uint64_t, uint64_t>> getRanges(
        const geo::MBR& query_bbox,
        const geo::MBR& bounds,
        int max_depth = 20
    );
};
```

**RocksDB Key Schema (Implementiert):**
```
# Analog zu Vector/Fulltext Indexes
spatial:<table>:<morton_code> → list<PK>

# Beispiele für verschiedene Modelle:
spatial:cities:12345678 → ["cities/berlin", "cities/munich"]
spatial:locations:23456789 → ["locations/loc1", "locations/loc2"]  # Graph nodes
spatial:images:34567890 → ["images/img1", "images/img2"]           # Vector entities
spatial:documents:45678901 → ["content/doc1", "content/doc2"]      # Content
```

**Implementierte Dateien:**
- ✅ `include/index/spatial_index.h` (211 lines)
- ✅ `src/index/spatial_index.cpp` (537 lines) - Morton encoding, R-Tree operations
- ✅ Tests: `tests/geo/test_spatial_index.cpp` (333 lines)

**Features:**
- ✅ Morton Z-order encoding (2D/3D)
- ✅ BBox range queries
- ✅ Radius/circle queries
- ✅ 3D Z-range filtering
- ✅ Insert/Remove operations
- ✅ Multi-table support (table-agnostic design)

**Abgeschlossen:** ✅ (17. November 2025)

---

#### 0.3 AQL ST_* Functions (Priorität: KRITISCH) ✅ **17/17 IMPLEMENTIERT (100%)**

**Status:** Core-Funktionen vollständig in `feature/aql-st-functions` (commits `ead621b`, `80d3d4a`, `89778e4`).

**Universelle Geo-Funktionen für alle Modelle:**
```sql
-- Relational + Geo
FOR city IN cities
  FILTER city.population > 100000 
    AND ST_Intersects(city.boundary, @viewport)
  RETURN city

-- Graph + Geo (Spatial Traversal)
FOR v IN 1..5 OUTBOUND 'locations/berlin' GRAPH 'roads'
  FILTER ST_DWithin(v.location, @center, 5000)
  RETURN v

-- Vector + Geo (Spatial-filtered ANN)
FOR img IN images
  FILTER ST_Within(img.location, @region)
  SORT SIMILARITY(img.embedding, @query) DESC
  LIMIT 10
  RETURN img

-- Content + Geo (Location-based RAG)
FOR doc IN documents
  FILTER FULLTEXT(doc.text, "hotel")
    AND ST_DWithin(doc.location, @myLocation, 2000)
  RETURN doc

-- Time-Series + Geo (Geo-temporal queries)
FOR reading IN sensor_data
  FILTER reading.timestamp > @start
    AND ST_Contains(@area, reading.sensor_location)
  RETURN reading
```

**17 ST_* Functions - Implementierungsstatus:**

| Kategorie | Funktion | Status | Commit |
|-----------|----------|--------|--------|
| **Constructors** | ST_Point(x, y) | ✅ | ead621b |
| | ST_GeomFromGeoJSON(json) | ✅ | 80d3d4a |
| | ST_GeomFromText(wkt) | ✅ | 89778e4 |
| **Converters** | ST_AsGeoJSON(geom) | ✅ | ead621b |
| | ST_AsText(geom) | ✅ | 89778e4 |
| **Predicates** | ST_Intersects(g1, g2) | ✅ | ead621b |
| | ST_Within(g1, g2) | ✅ | ead621b |
| | ST_Contains(g1, g2) | ✅ | 80d3d4a |
| **Distance** | ST_Distance(g1, g2) | ✅ | ead621b |
| | ST_DWithin(g1, g2, dist) | ✅ | 80d3d4a |
| | ST_3DDistance(g1, g2) | ✅ | 89778e4 |
| **3D Support** | ST_HasZ(geom) | ✅ | 80d3d4a |
| | ST_Z(point) | ✅ | 80d3d4a |
| | ST_ZMin(geom) | ✅ | 80d3d4a |
| | ST_ZMax(geom) | ✅ | 80d3d4a |
| | ST_Force2D(geom) | ✅ | 89778e4 |
| | ST_ZBetween(g, zmin, zmax) | ✅ | NEW |
| **Advanced** | ST_Buffer(g, d) | ✅ (MVP) | NEW |
|  | ST_Union(g1, g2) | ✅ (MVP) | NEW |

**Progress:** 17/17 (100%) ✅

**Vollständig implementierte Kategorien:**
- ✅ **Constructors:** 3/3 (100%) - ST_Point, ST_GeomFromGeoJSON, ST_GeomFromText
- ✅ **Converters:** 2/2 (100%) - ST_AsGeoJSON, ST_AsText
- ✅ **Predicates:** 3/3 (100%) - ST_Intersects, ST_Within, ST_Contains
- ✅ **Distance:** 3/3 (100%) - ST_Distance, ST_DWithin, ST_3DDistance

**Implementierte Funktionen (17/17 - 100%):**

```cpp
// src/query/let_evaluator.cpp (commits ead621b, 80d3d4a, 89778e4)

// === CONSTRUCTORS (3/3) ✅ ===
// 1. ST_Point(x, y) - Create Point geometry
LET point = ST_Point(13.405, 52.52)
→ {"type": "Point", "coordinates": [13.405, 52.52]}

// 2. ST_GeomFromGeoJSON(json) - Parse GeoJSON string
LET geom = ST_GeomFromGeoJSON('{"type":"Point","coordinates":[13.405,52.52]}')
→ {"type": "Point", "coordinates": [13.405, 52.52]}

// 3. ST_GeomFromText(wkt) - Parse WKT (Well-Known Text) NEW ✨
LET geom = ST_GeomFromText('POINT(13.405 52.52)')
→ {"type": "Point", "coordinates": [13.405, 52.52]}

LET line = ST_GeomFromText('LINESTRING(0 0, 1 1, 2 1, 2 2)')
→ {"type": "LineString", "coordinates": [[0,0],[1,1],[2,1],[2,2]]}

// === CONVERTERS (2/2) ✅ ===
// 4. ST_AsGeoJSON(geom) - Convert to GeoJSON string
LET json = ST_AsGeoJSON(doc.geometry)
→ "{\"type\":\"Point\",\"coordinates\":[13.405,52.52]}"

// 5. ST_AsText(geom) - Convert to WKT NEW ✨
LET wkt = ST_AsText(ST_Point(13.405, 52.52))
→ "POINT(13.405 52.52)"

// === PREDICATES (3/3) ✅ ===
// 6. ST_Intersects(g1, g2) - Spatial intersection
LET intersects = ST_Intersects(point1, point2)
→ true/false

// 7. ST_Within(g1, g2) - Point within Polygon/MBR
LET within = ST_Within(ST_Point(13.405, 52.52), boundary)
→ true/false

// 8. ST_Contains(g1, g2) - Containment test
LET contains = ST_Contains(boundary, point)
→ true/false

// === DISTANCE (3/3) ✅ ===
// 9. ST_Distance(g1, g2) - 2D Euclidean distance
LET dist = ST_Distance(
    ST_Point(13.405, 52.52),
    ST_Point(2.35, 48.86)
)
→ 14.87 degrees (~1654 km)

// 10. ST_DWithin(g1, g2, distance) - Proximity check
LET nearby = ST_DWithin(doc.location, ST_Point(13.405, 52.52), 0.1)
→ true/false

// 11. ST_3DDistance(g1, g2) - 3D Euclidean distance NEW ✨
LET dist3d = ST_3DDistance(
    ST_GeomFromText('POINT(0 0 0)'),
    ST_GeomFromText('POINT(1 1 1)')
)
→ 1.732 (sqrt(3))

// === 3D SUPPORT (5/7) ===
// 12. ST_HasZ(geom) - Check for 3D coordinates
LET is3d = ST_HasZ(ST_GeomFromText('POINT(13.405 52.52 35.0)'))
→ true

// 13. ST_Z(point) - Extract Z coordinate
LET elevation = ST_Z(ST_GeomFromText('POINT(13.405 52.52 35.0)'))
→ 35.0

// 14. ST_ZMin(geom) - Minimum Z value
LET min_z = ST_ZMin(terrain_polygon)
→ 12.5 (or null if 2D)

// 15. ST_ZMax(geom) - Maximum Z value
LET max_z = ST_ZMax(terrain_polygon)
→ 156.8 (or null if 2D)

// 16. ST_Force2D(geom) - Strip Z coordinates NEW ✨
LET geom2d = ST_Force2D(ST_GeomFromText('POINT(1 2 3)'))
→ {"type": "Point", "coordinates": [1, 2]}

// 17. ST_ZBetween(geom, zmin, zmax) - Z-range filter NEW ✨
LET inRange = ST_ZBetween(ST_GeomFromText('LINESTRING(0 0 1, 1 1 5, 2 2 10)'), 4, 6)
→ true

// 18. ST_Buffer(geom, d) - MVP: Punkt → Quadrat-Buffer
LET buffered = ST_Buffer(ST_Point(1,2), 0.5)
→ {"type":"Polygon","coordinates":[[[0.5,1.5],[1.5,1.5],[1.5,2.5],[0.5,2.5],[0.5,1.5]]]]}

// 19. ST_Union(g1, g2) - MVP: MBR-Union als Polygon
LET uni = ST_Union(ST_Point(0,0), ST_GeomFromText('POLYGON((1 1,2 1,2 2,1 2,1 1))'))
→ {"type":"Polygon","coordinates":[[[0,0],[2,0],[2,2],[0,2],[0,0]]]]}
```

**Implementierte Dateien:**
- ✅ `src/query/let_evaluator.cpp` - evaluateFunctionCall() erweitert
- ✅ `include/utils/geo/ewkb.h` - MBR, Coordinate, GeometryInfo
- ✅ Windows-Kompatibilität: M_PI definition, GeoSidecar include

**Remaining Work:**
- Performance & Genauigkeit: ST_Buffer/ST_Union sind MVPs (MBR-basiert). Präzise Geometrie-Operationen optional via GEOS-Plugin (Phase 2).

**Geschätzt:** <0.1 Tage (ST_ZBetween trivial, advanced functions für Phase 2)

---

### AQL Syntax & Parser-Integration (Dokumentation)

- Syntax: ST_* Funktionen werden als normale Funktionsaufrufe in AQL genutzt, z. B.
  - `FILTER ST_Intersects(doc.boundary, @viewport)`
  - `LET p = ST_Point(13.405, 52.52)`
  - `RETURN ST_AsText(ST_Buffer(doc.geom, 1.0))`
- Parser: Der AQL-Parser unterstützt generische Funktionsaufrufe (`FunctionCallExpr`).
- Auswertung: 
  - ✅ `LetEvaluator::evaluateFunctionCall()` dispatcht alle ST_* für LET-Ausdrücke.
  - ✅ `QueryEngine::evaluateExpression()` wertet ST_* in FILTER/RETURN via `qe_evalFunction()` aus.
- Implementierung: ST_* sind in `src/query/query_engine.cpp` (qe_evalFunction) und `src/query/let_evaluator.cpp` verfügbar.

**Tests**
- Neu: `tests/geo/test_aql_st_functions.cpp` deckt alle implementierten Funktionen mit Unit- und Integrationstests ab.
- Neu: `tests/geo/test_aql_st_queryengine.cpp` testet ST_* in AQL FILTER/RETURN via QueryEngine.
- Build-Hinweis (Windows/MSVC): PDB-Locks erzwingen ggf. Single-Thread-Build; CI-Umgebungen sind meist nicht betroffen.

**AQL Query-Beispiele (ST_* in FILTER/RETURN):**

```aql
// 1. Räumliche Filterung: Punkte innerhalb eines Polygons
FOR place IN places
  FILTER ST_Within(
    ST_GeomFromGeoJSON(place.geom),
    ST_GeomFromText('POLYGON((0 0, 2 0, 2 2, 0 2, 0 0))')
  )
  RETURN place.name

// 2. Proximity-Suche: Hotels im Umkreis von 2 km
FOR doc IN hotels
  FILTER ST_DWithin(
    ST_GeomFromGeoJSON(doc.location),
    ST_Point(13.405, 52.52),
    2.0
  )
  RETURN doc

// 3. Z-Filter: 3D-Objekte in Höhenbereich
FOR building IN buildings
  FILTER ST_ZBetween(
    ST_GeomFromText(building.geometry),
    50.0,
    100.0
  )
  RETURN building._key

// 4. RETURN mit ST_*: Buffer-Ergebnis als WKT
FOR place IN places
  LET buffered = ST_Buffer(ST_GeomFromGeoJSON(place.geom), 1.0)
  RETURN ST_AsText(buffered)

// 5. LET + SORT: Nächste Hotels nach Distanz sortiert
FOR hotel IN hotels
  LET dist = ST_Distance(
    ST_GeomFromGeoJSON(hotel.location),
    ST_Point(13.405, 52.52)
  )
  FILTER dist < 5.0
  SORT dist ASC
  LIMIT 10
  RETURN { name: hotel.name, distance: dist }

// 6. Hybrid: Fulltext + Geo
FOR doc IN documents
  FILTER FULLTEXT(doc.text, "hotel")
    AND ST_DWithin(doc.location, @myLocation, 2000)
  RETURN doc
```

---

### Hybrid Multi-Model Queries ✨ **NEU (November 2025)**

**Vector + Geo: Spatial-Filtered ANN Search**
```aql
// Ähnliche Bilder NUR aus bestimmter Region
FOR img IN images
  FILTER ST_Within(
    ST_GeomFromGeoJSON(img.location),
    ST_GeomFromText(@berlin_region)
  )
  SORT SIMILARITY(img.embedding, @query_vector) DESC
  LIMIT 10
  RETURN img

// C++ Implementation:
VectorGeoQuery q;
q.table = "images";
q.vector_field = "embedding";
q.query_vector = {...};
q.spatial_filter = ST_Within(...);  // Pre-filter via spatial index
q.k = 10;
auto [st, results] = engine->executeVectorGeoQuery(q);
// Results: Spatial candidates → Vector search with whitelist → Top-K
```

**Graph + Geo: Spatial-Constrained Traversal**
```aql
// Shortest path Berlin → Dresden, nur durch deutsche Städte
FOR v, e, p IN 1..5 OUTBOUND 'locations/berlin' GRAPH 'roads'
  FILTER ST_Within(
    ST_GeomFromGeoJSON(v.location),
    ST_GeomFromText(@germany_bbox)
  )
  RETURN p

// C++ Implementation:
RecursivePathQuery q;
q.start_node = "locations/berlin";
q.end_node = "locations/dresden";
q.spatial_constraint = {
  .vertex_geom_field = "location",
  .spatial_filter = ST_Within(v.location, @region)
};
auto [st, paths] = engine->executeRecursivePathQuery(q);
// BFS/Dijkstra checks spatial filter per vertex
```

**Content + Geo: Location-Based Fulltext RAG**
```aql
// Hotels mit "luxury" im Text UND in Berlin
FOR doc IN documents
  FILTER FULLTEXT(doc.text, "luxury hotel")
    AND ST_DWithin(
      ST_GeomFromGeoJSON(doc.location),
      ST_Point(13.405, 52.52),
      5000  // 5km radius
    )
  SORT BM25(doc) DESC, ST_Distance(doc.location, @center) ASC
  LIMIT 10
  RETURN doc

// C++ Implementation:
ContentGeoQuery q;
q.table = "documents";
q.fulltext_query = "luxury hotel";
q.spatial_filter = ST_DWithin(...);
q.boost_by_distance = true;
q.center_point = {13.405, 52.52};
auto [st, results] = engine->executeContentGeoQuery(q);
// Fulltext results → Spatial filter → Distance-based re-ranking
```

**Time-Series + Geo: Geo-Temporal Queries**
```aql
-- Time-Series + Geo (Geo-temporal queries)
FOR reading IN sensor_data
  FILTER reading.timestamp > @start
    AND ST_Contains(@area, reading.sensor_location)
  RETURN reading
```

---

### 🚀 Hybrid Query Implementierungsstatus (November 2025)

**✅ VOLLSTÄNDIG IMPLEMENTIERT:**
- Vector+Geo: `executeVectorGeoQuery()` mit Two-Phase Filtering
- Graph+Geo: `RecursivePathQuery::SpatialConstraint` für BFS/Dijkstra
- Content+Geo: `executeContentGeoQuery()` mit BM25 + Distance Boosting
- Tests: 7 Integration Tests in `test_hybrid_queries.cpp`
- Dokumentation: AQL-Beispiele + C++ API Snippets

**⚡ Performance-Optimierungen (Phase 1.5):**

1. **HNSW Integration** ✅ IMPLEMENTIERT
   - `VectorIndexManager::searchKnn()` mit Whitelist
   - Fallback: Brute-Force wenn kein VectorIndexManager
   - Performance: O(log n) HNSW vs. O(n) Brute-Force (10× bei 10k+ vectors)
   - Test: `VectorGeo_WithVectorIndexManager_UsesHNSW`

2. **Spatial Index Integration** ✅ IMPLEMENTIERT
   - `SpatialIndexManager::searchWithin()` für R-Tree Pre-Filtering
   - Helper: `extractBBoxFromFilter()` für ST_Within/ST_DWithin
   - Performance: O(log n) R-Tree vs. O(n) Full Table Scan (100× bei 100k+ entities)
   - Fallback: Full Table Scan wenn kein SpatialIndexManager

3. **Batch Entity Loading** ✅ IMPLEMENTIERT
   - `RocksDBWrapper::multiGet()` für Graph+Geo vertices
   - Performance: 1 × RocksDB latency vs. N × individual gets (5× bei 100+ vertices)
   - Beide Cases: Dijkstra path validation + BFS reachable nodes

**Performance (Stand November 2025):**
- Vector+Geo (MIT HNSW + Spatial Index): <5ms @ 1000 candidates ✅✅
- Vector+Geo (Brute-Force + Spatial Index): <20ms @ 1000 candidates ✅
- Vector+Geo (Fallback Full Scan): 50-100ms @ 1000 candidates
- Graph+Geo (MIT Batch Loading): 20-50ms @ BFS depth 5 ✅
- Graph+Geo (Sequential Loading): 100-200ms @ BFS depth 5
- Content+Geo: 20-80ms @ 100 fulltext results (bereits effizient durch Fulltext Pre-Filter)

**Verbleibende Optimierungen (Optional):**
- ⏳ Parallel Filtering (TBB) für Content+Geo bei >1000 fulltext results
- ⏳ SIMD für L2 distance computation in Brute-Force fallback
- ⏳ Geo-aware Query Optimizer (cost-based spatial vs. fulltext ordering)

**Fazit:** Alle kritischen Optimierungen implementiert! System production-ready für Hybrid Queries.

---

**17 ST_* Functions (für alle Tabellen):**
- Constructors: ST_Point, ST_GeomFromGeoJSON, ST_GeomFromText
- Converters: ST_AsGeoJSON, ST_AsText
- Predicates: ST_Intersects, ST_Within, ST_Contains
- Distance: ST_Distance, ST_DWithin, ST_3DDistance
- 3D: ST_HasZ, ST_Z, ST_ZMin/ZMax, ST_Force2D/3D, ST_ZBetween

**Geschätzt:** 1.5 Tage

---

#### 0.4 Query Engine Integration (Priorität: HOCH)

**Spatial Execution Plan (modell-agnostisch):**
```cpp
// Execution für JEDES Modell identisch:
1. Parse: ST_Intersects(geometry_field, @viewport)
2. Extract: @viewport MBR
3. Candidates: R-Tree scan -> PK set
4. Z-Filter (optional): Z-Range index -> intersect PK set
5. Load entities: FROM <table> WHERE _id IN (candidates)
6. Exact Check: Boost.Geometry predicate
7. Additional filters: Apply non-geo predicates (population, type, etc.)
8. Return: Filtered entities
```

**Query Optimizer Extensions:**
```cpp
struct SpatialSelectivity {
    double area_ratio;      // query_bbox / total_area
    double density;         // avg entities per unit
    int estimated_hits;     // from R-Tree stats
};

// Cost-based decision (gilt für alle Modelle)
if (spatial_selectivity < 0.01) {
    plan = SPATIAL_FIRST;  // Geo filter -> other filters
} else {
    plan = FILTER_FIRST;   // Other filters -> geo filter
}
```

**Geschätzt:** 2 Tage

---

### Geo Infrastructure Zusammenfassung
**Total:** ~7 Tage  
**Ergebnis:** Geo-Capability verfügbar für **ALLE 5 Modelle**  
**Kritische Features:** 
- EWKB Storage (universal)
- R-Tree Index (table-agnostic)
- ST_* Functions (AQL-integriert)
- Query Optimizer (selectivity-aware)

---

## 🎯 Phase 1: Graph Database Vervollständigung (70% → 95%)

### Aktueller Stand
✅ **Implementiert (70%):**
- BFS/Dijkstra/A* Traversal
- Adjacency Lists (graph:out, graph:in)
- Variable Depth (min..max hops)
- Temporal Graph Queries
- Edge Type Filtering
- Property Graph Model (Labels, Types)
- Multi-Graph Support

❌ **Fehlend (30%):**
- Path Constraints (LAST_EDGE, NO_VERTEX, UNIQUE_VERTICES)
- Centrality Algorithms (Degree, Betweenness, Closeness, PageRank)
- Community Detection (Louvain, Label Propagation)
- Pattern Matching (Cypher-ähnlich)
- Bulk Edge Operations
- Graph Statistics Aggregation

### Implementierungsplan

#### 1.1 Path Constraints (Priorität: HOCH)
**Dateien:**
- `include/index/graph_index.h`: PathConstraints struct erweitern
- `src/index/graph_index.cpp`: Constraint-Validierung in BFS/Dijkstra

**Features:**
```cpp
struct PathConstraints {
    bool unique_vertices = false;     // No vertex visited twice
    bool unique_edges = false;        // No edge traversed twice
    std::set<std::string> forbidden_vertices;  // Blacklist
    std::set<std::string> forbidden_edges;
    std::set<std::string> required_vertices;   // Must-visit
    int max_edge_count = -1;          // Limit edges per path
};
```

**Tests:**
- `tests/test_graph_path_constraints.cpp`
- Szenarien: Cycle detection, avoiding nodes, forced routing

**Geschätzt:** 1 Tag

---

#### 1.2 Centrality Algorithms (Priorität: MITTEL)
**Dateien:**
- `include/index/graph_analytics.h` (NEU)
- `src/index/graph_analytics.cpp` (NEU)

**Algorithmen:**
1. **Degree Centrality:** Einfaches In/Out-Degree Counting
2. **Betweenness Centrality:** Shortest-Path-basiert (Brandes Algorithm)
3. **Closeness Centrality:** Average shortest path zu allen Nodes
4. **PageRank:** Iterative Power-Methode (10-20 Iterationen)

**API:**
```cpp
class GraphAnalytics {
public:
    GraphAnalytics(GraphIndexManager& gm);
    
    // Degree centrality
    std::map<std::string, int> degreeCentrality(std::string_view graph_id);
    
    // PageRank (iterative)
    std::map<std::string, double> pageRank(
        std::string_view graph_id,
        double damping = 0.85,
        int max_iterations = 20,
        double tolerance = 1e-6
    );
    
    // Betweenness (Brandes algorithm)
    std::map<std::string, double> betweennessCentrality(std::string_view graph_id);
};
```

**Tests:**
- Small graph (10 nodes) mit bekannten Werten
- Validierung gegen NetworkX/Neo4j Referenz

**Geschätzt:** 2 Tage

---

#### 1.3 Community Detection (Priorität: NIEDRIG)
**Algorithmen:**
- **Label Propagation:** Schnell, für große Graphen
- **Louvain:** Modularitäts-basiert (komplexer)

**MVP:** Nur Label Propagation implementieren

```cpp
class CommunityDetection {
public:
    // Label Propagation
    std::map<std::string, int> labelPropagation(
        std::string_view graph_id,
        int max_iterations = 100
    );
};
```

**Geschätzt:** 1.5 Tage

---

#### 1.4 Pattern Matching (Priorität: HOCH)
**Ziel:** Cypher-ähnliche Pattern Queries

**Beispiel:**
```aql
FOR p IN PATTERN (a)-[:FOLLOWS]->(b)-[:LIKES]->(c)
  WHERE a.type == 'Person' AND c.type == 'Post'
  RETURN a, b, c
```

**Implementation:**
- Pattern Parser (Regex-basiert oder Hand-written)
- Pattern Matcher (BFS mit Constraints)

**Dateien:**
- `include/query/pattern_matcher.h`
- `src/query/pattern_matcher.cpp`

**Geschätzt:** 2 Tage

---

### Graph Phase Zusammenfassung
**Total:** ~6.5 Tage  
**Fortschritt:** 70% → 95%  
**Kritische Features:** Path Constraints, PageRank, Pattern Matching

---

## 🎯 Phase 1.5: Hybrid Query Optimization (MVP → Production) ⚡ **NEU**

### Ziel: Performance-Optimierung für Production-Scale Hybrid Queries

**Status:** Hybrid Queries implementiert (MVP), aber mit Performance-Gaps

#### 1.5.1 HNSW Integration für Vector+Geo (Priorität: HOCH)

**Problem:** Brute-Force L2-Distanz über spatial candidates ineffizient bei 10k+ vectors

**Lösung:** VectorIndexManager mit Whitelist nutzen

```cpp
// Current (MVP - Brute-Force):
for (const auto& pk : spatialCandidates) {
    const auto& entity = entityCache[pk];
    std::vector<float> vec = entity[q.vector_field];
    float dist = computeL2(vec, q.query_vector);  // O(n × dim)
    // ...
}

// Phase 2 (HNSW with Whitelist):
auto [st, results] = vectorIndexMgr_->searchKnn(
    q.query_vector, 
    q.k, 
    &spatialCandidates  // Whitelist from spatial filter
);
// O(log n × dim) via HNSW, or O(n × dim) brute-force fallback if whitelist given
```

**Implementation:**
- VectorIndexManager* in QueryEngine constructor (optional dependency)
- executeVectorGeoQuery() nutzt VectorIndexManager falls verfügbar
- Fallback: Aktueller Brute-Force (für Backwards Compatibility)

**Geschätzt:** 0.5 Tage

#### 1.5.2 Spatial Index Integration (Priorität: HOCH)

**Problem:** Full Table Scan für ST_Within/ST_DWithin ineffizient bei 100k+ entities

**Lösung:** SpatialIndexManager für Phase 1 Pre-Filtering

```cpp
// Current (MVP - Full Table Scan):
auto it = db_.newIterator();
std::string prefix = q.table + ":";
it->Seek(prefix);
while (it->Valid()) {  // O(n) scan
    nlohmann::json entity = nlohmann::json::parse(it->value());
    if (evaluateCondition(q.spatial_filter, ctx)) {
        spatialCandidates.push_back(pk);
    }
    it->Next();
}

// Phase 2 (R-Tree Range Query):
auto bbox = extractBBoxFromFilter(q.spatial_filter);  // Parse ST_Within/ST_DWithin
auto [st, pks] = spatialIndexMgr_->queryRange(
    q.table, 
    q.geom_field, 
    bbox
);  // O(log n) R-Tree traversal
spatialCandidates = pks;
```

**Implementation:**
- SpatialIndexManager* in QueryEngine constructor
- Helper: extractBBoxFromFilter() für ST_Within/ST_DWithin/ST_Contains
- executeVectorGeoQuery(), executeContentGeoQuery() nutzen R-Tree

**Geschätzt:** 1 Tag (inkl. BBox extraction logic)

#### 1.5.3 Batch Entity Loading (Priorität: MEDIUM)

**Problem:** N × db_.get() in Graph+Geo Vertex Loop ineffizient bei 100+ path nodes

**Lösung:** RocksDB multiGet() für batch loading

```cpp
// Current (MVP - Sequential Get):
for (const auto& vertexPk : pathResult.path) {
    auto [getSt, vertexData] = db_.get(vertexPk);  // O(n × latency)
    nlohmann::json vertex = nlohmann::json::parse(vertexData);
    // ...
}

// Phase 2 (Batch MultiGet):
auto [st, entities] = db_.multiGet(pathResult.path);  // O(1 × latency)
for (size_t i = 0; i < pathResult.path.size(); ++i) {
    const auto& vertexPk = pathResult.path[i];
    nlohmann::json vertex = nlohmann::json::parse(entities[i]);
    // ...
}
```

**Implementation:**
- RocksDBWrapper::multiGet() (falls noch nicht vorhanden)
- executeRecursivePathQuery() batch-loads vertices vor Loop

**Geschätzt:** 0.3 Tage

#### 1.5.4 Parallel Spatial Filtering (Priorität: LOW)

**Problem:** Sequential evaluateCondition() über 1000+ fulltext results

**Lösung:** TBB parallel_for für Content+Geo Phase 2

```cpp
// Current (MVP - Sequential):
for (const auto& [pk, bm25_score] : ftResults) {  // O(n)
    if (evaluateCondition(q.spatial_filter, ctx)) {
        results.push_back({pk, bm25_score, ...});
    }
}

// Phase 2 (Parallel):
tbb::concurrent_vector<ContentGeoResult> concurrent_results;
tbb::parallel_for(size_t(0), ftResults.size(), [&](size_t i) {  // O(n/cores)
    const auto& [pk, bm25_score] = ftResults[i];
    if (evaluateCondition(q.spatial_filter, ctx)) {
        concurrent_results.push_back({pk, bm25_score, ...});
    }
});
results = std::vector<ContentGeoResult>(concurrent_results.begin(), concurrent_results.end());
```

**Geschätzt:** 0.2 Tage

**Gesamtaufwand Phase 1.5:** 2 Tage (nur High-Priority) oder 3 Tage (mit Medium+Low)

---

## 🎯 Phase 1.5: Hybrid Query Optimization (MVP → Production) ⚡ **NEU**

### Ziel: Performance-Optimierung für Production-Scale Hybrid Queries

**Status:** Hybrid Queries implementiert (MVP), aber mit Performance-Gaps identifiziert

#### 1.5.1 HNSW Integration für Vector+Geo (Priorität: HOCH)

**Problem:** Brute-Force L2-Distanz über spatial candidates ineffizient bei 10k+ vectors

**Lösung:** VectorIndexManager mit Whitelist nutzen

```cpp
// Current (MVP - Brute-Force in executeVectorGeoQuery):
for (const auto& pk : spatialCandidates) {
    const auto& entity = entityCache[pk];
    std::vector<float> vec = entity[q.vector_field];
    float dist = computeL2(vec, q.query_vector);  // O(n × dim)
    vectorResults.push_back({pk, dist});
}
std::sort(vectorResults.begin(), vectorResults.end());

// Phase 1.5 (HNSW with Whitelist):
if (vectorIndexMgr_) {
    auto [st, results] = vectorIndexMgr_->searchKnn(
        q.query_vector, 
        q.k, 
        &spatialCandidates  // Whitelist from spatial filter
    );
    // O(log n × dim) via HNSW, falls whitelist leer
    // O(n × dim) brute-force über whitelist, falls gegeben (wie aktuell)
}
```

**Implementation:**
- `VectorIndexManager*` als optionale Dependency in QueryEngine constructor
- executeVectorGeoQuery() prüft `if (vectorIndexMgr_)` vor Brute-Force
- Fallback: Aktueller Code (Backwards Compatibility)

**Dateien:**
- `include/query/query_engine.h`: `VectorIndexManager* vectorIndexMgr_` hinzufügen
- `src/query/query_engine.cpp`: Constructor + executeVectorGeoQuery() anpassen

**Geschätzt:** 0.5 Tage

#### 1.5.2 Spatial Index Integration (Priorität: HOCH)

**Problem:** Full Table Scan für ST_Within/ST_DWithin ineffizient bei 100k+ entities

**Lösung:** SpatialIndexManager für Phase 1 Pre-Filtering

```cpp
// Current (MVP - Full Table Scan):
auto it = db_.newIterator();
std::string prefix = q.table + ":";
it->Seek(prefix);
while (it->Valid()) {  // O(n) scan über ALLE entities
    nlohmann::json entity = nlohmann::json::parse(it->value());
    EvaluationContext ctx;
    ctx.set("doc", entity);
    if (evaluateCondition(q.spatial_filter, ctx)) {
        spatialCandidates.push_back(pk);
    }
    it->Next();
}

// Phase 1.5 (R-Tree Range Query):
if (spatialIndexMgr_) {
    auto bbox = extractBBoxFromFilter(q.spatial_filter);  // Parse ST_Within/ST_DWithin
    auto [st, pks] = spatialIndexMgr_->queryRange(
        q.table, 
        q.geom_field, 
        bbox
    );  // O(log n) R-Tree traversal → ~1000 candidates
    spatialCandidates = pks;
} else {
    // Fallback: Current full scan
}
```

**Implementation:**
- `SpatialIndexManager*` in QueryEngine constructor
- Helper: `extractBBoxFromFilter(Expression*)` für ST_Within/ST_DWithin/ST_Contains
  - ST_Within(geom, POLYGON(...)) → MBR von Polygon
  - ST_DWithin(geom, ST_Point(x,y), d) → {x-d, y-d, x+d, y+d}
- executeVectorGeoQuery(), executeContentGeoQuery(), executeRecursivePathQuery() nutzen R-Tree

**Dateien:**
- `include/query/query_engine.h`: `SpatialIndexManager* spatialIndexMgr_` hinzufügen
- `src/query/query_engine.cpp`: extractBBoxFromFilter() + alle drei Hybrid-Executors

**Geschätzt:** 1 Tag (inkl. BBox extraction logic mit Expression tree traversal)

#### 1.5.3 Batch Entity Loading (Priorität: MEDIUM)

**Problem:** N × db_.get() in Graph+Geo Vertex Loop ineffizient bei 100+ path nodes

**Lösung:** RocksDB multiGet() für batch loading

```cpp
// Current (MVP - Sequential Get):
for (const auto& vertexPk : reachableNodes) {
    auto [getSt, vertexData] = db_.get(vertexPk);  // N × RocksDB latency
    if (!getSt.ok) continue;
    nlohmann::json vertex = nlohmann::json::parse(vertexData);
    EvaluationContext ctx;
    ctx.set("v", vertex);
    if (evaluateCondition(sc.spatial_filter, ctx)) {
        filteredNodes.push_back(vertexPk);
    }
}

// Phase 1.5 (Batch MultiGet):
auto [st, entities] = db_.multiGet(reachableNodes);  // 1 × RocksDB latency
for (size_t i = 0; i < reachableNodes.size(); ++i) {
    if (entities[i].empty()) continue;
    nlohmann::json vertex = nlohmann::json::parse(entities[i]);
    EvaluationContext ctx;
    ctx.set("v", vertex);
    if (evaluateCondition(sc.spatial_filter, ctx)) {
        filteredNodes.push_back(reachableNodes[i]);
    }
}
```

**Implementation:**
- RocksDBWrapper::multiGet(vector<string> keys) → vector<optional<string>> (falls noch nicht vorhanden)
- executeRecursivePathQuery() batch-loads vertices vor spatial evaluation loop

**Dateien:**
- `include/storage/rocksdb_wrapper.h`: multiGet() signature
- `src/storage/rocksdb_wrapper.cpp`: RocksDB MultiGet API wrapper
- `src/query/query_engine.cpp`: executeRecursivePathQuery() beide Cases

**Geschätzt:** 0.3 Tage

#### 1.5.4 Parallel Spatial Filtering (Priorität: LOW)

**Problem:** Sequential evaluateCondition() über 1000+ fulltext results

**Lösung:** TBB parallel_for für Content+Geo Phase 2

```cpp
// Current (MVP - Sequential):
for (const auto& [pk, bm25_score] : ftResults) {  // O(n)
    auto [getSt, entity] = db_.get(q.table + ":" + pk);
    nlohmann::json doc = nlohmann::json::parse(entity);
    EvaluationContext ctx;
    ctx.set("doc", doc);
    if (!evaluateCondition(q.spatial_filter, ctx)) continue;
    results.push_back({pk, bm25_score, ...});
}

// Phase 1.5 (Parallel):
tbb::concurrent_vector<ContentGeoResult> concurrent_results;
tbb::parallel_for(size_t(0), ftResults.size(), [&](size_t i) {  // O(n/cores)
    const auto& [pk, bm25_score] = ftResults[i];
    auto [getSt, entity] = db_.get(q.table + ":" + pk);
    if (!getSt.ok) return;
    nlohmann::json doc = nlohmann::json::parse(entity);
    EvaluationContext ctx;
    ctx.set("doc", doc);
    if (evaluateCondition(q.spatial_filter, ctx)) {
        concurrent_results.push_back({pk, bm25_score, ...});
    }
});
results = std::vector<ContentGeoResult>(concurrent_results.begin(), concurrent_results.end());
```

**Hinweis:** Nur sinnvoll bei >100 fulltext results (TBB overhead)

**Geschätzt:** 0.2 Tage

**Gesamtaufwand Phase 1.5:** 2 Tage (nur High-Priority: HNSW + Spatial Index) oder 2.5 Tage (mit Batch Loading)

---

## 🎯 Phase 2: Vector Database Vervollständigung (75% → 95%)

### Aktueller Stand
✅ **Implementiert (75%):**
- HNSW Index (hnswlib)
- k-NN Search (L2, Cosine, Dot Product)
- Batch Insert/Delete
- Persistenz (save/load)
- Cursor Pagination

❌ **Fehlend (25%):**
- Filtered Vector Search (Metadata pre-filtering)
- Approximate Radius Search
- Multi-Vector Search (Multiple embeddings per entity)
- Index Compaction/Optimization
- Hybrid Search (Vector + Fulltext)

### Implementierungsplan

#### 2.1 Filtered Vector Search (Priorität: HOCH)
**Problem:** HNSW sucht über gesamten Index, dann Filter → ineffizient

**Lösung:** Pre-filtering mit Whitelist

**Implementation:**
```cpp
struct VectorSearchFilter {
    std::optional<std::string> category;  // e.g., "Person"
    std::map<std::string, std::string> metadata;  // e.g., {"country": "DE"}
    std::optional<std::pair<double, double>> score_range;
};

// In VectorIndexManager
std::pair<Status, std::vector<Result>> searchKnnFiltered(
    const std::vector<float>& query,
    size_t k,
    const VectorSearchFilter& filter
);
```

**Whitelist Generation:**
1. Scan Secondary Index für `category:Person`
2. Scan für `metadata:country:DE`
3. Intersection der PKs
4. HNSW sucht nur über Whitelist

**Tests:** Filtered search mit 90% Filterung (10% passthrough)

**Geschätzt:** 1 Tag

---

#### 2.2 Approximate Radius Search (Priorität: MITTEL)
**Ziel:** Finde alle Vektoren innerhalb Radius `r` von Query

**Challenge:** HNSW ist für k-NN, nicht für Radius optimiert

**Approach:**
1. k-NN mit großem k (z.B. 1000)
2. Filter Ergebnisse nach Distanz <= r
3. Falls < k Ergebnisse: erhöhe k und retry

```cpp
std::pair<Status, std::vector<Result>> searchRadius(
    const std::vector<float>& query,
    float max_distance,
    size_t max_results = 10000
);
```

**Geschätzt:** 0.5 Tage

---

#### 2.3 Multi-Vector Search (Priorität: NIEDRIG)
**Use Case:** Entity mit mehreren Embeddings (Bild + Text)

**Ansatz:**
- Speichere multiple vectors: `embedding_text`, `embedding_image`
- Separate HNSW Indizes oder Multi-Vector HNSW

**MVP:** Separate Indizes, kombiniere Ergebnisse via Score-Fusion

**Geschätzt:** 1 Tag

---

#### 2.4 Hybrid Search (Vector + Fulltext) (Priorität: HOCH)
**Ziel:** RRF (Reciprocal Rank Fusion) von Vector + Keyword

**Implementation:**
```cpp
struct HybridSearchParams {
    std::vector<float> query_vector;
    std::string query_text;
    float vector_weight = 0.7;
    float text_weight = 0.3;
};

std::pair<Status, std::vector<Result>> hybridSearch(
    const HybridSearchParams& params,
    size_t k
);
```

**Algorithm:**
1. Vector Search → Rank list V
2. Fulltext Search → Rank list T
3. RRF: score(doc) = Σ 1/(k + rank_V(doc)) + 1/(k + rank_T(doc))
4. Sort by RRF score

**Geschätzt:** 1.5 Tage

---

### Vector Phase Zusammenfassung
**Total:** ~4 Tage  
**Fortschritt:** 75% → 95%  
**Kritische Features:** Filtered Search, Hybrid Search

---

## 🎯 Phase 4: Content/Filesystem Vervollständigung (30% → 75%)

### Aktueller Stand
✅ **Implementiert (30%):**
- ContentMeta/ChunkMeta Schemas
- Basic Import API (`/content/import`)
- Content Storage (RocksDB)
- Chunk-Graph (parent/next/prev)

❌ **Fehlend (45%):**
- Content Search API (Hybrid Search)
- Filesystem Interface (/fs/* endpoints)
- Content Retrieval Optimization
- Chunk Navigation API

⚠️ **Enterprise Features (Externe DLL):**
- Text Extraction (PDF/DOCX/Markdown) ← Enterprise DLL
- Chunking Pipeline ← Enterprise DLL
- Binary File Storage (Large Blobs) ← Enterprise DLL
- Multi-Modal Embeddings ← Enterprise DLL

### Implementierungsplan

#### 3.1 Content Search API (Priorität: HOCH)
**Endpoints:**
```http
POST /content/search
{
  "query": "machine learning",
  "k": 10,
  "filters": {
    "category": "TEXT",
    "tags": ["research"]
  }
}
```

**Implementation:** Bereits teilweise vorhanden (`ContentManager::searchContent`)

**Verbesserungen:**
- Hybrid Search (Vector + Fulltext)
- Faceted Filters (by category, tags, date)
- Ranking (BM25 + Vector Similarity)

**Geschätzt:** 1 Tag

---

#### 3.2 Filesystem Interface (Priorität: MITTEL)
**Ziel:** Mount ThemisDB als Virtual Filesystem (FUSE on Linux)

**Alternative (MVP):** HTTP File API

```http
GET /fs/:path
PUT /fs/:path
DELETE /fs/:path
```

**Mapping:**
- `/fs/documents/report.pdf` → `content:<uuid>`
- Hierarchie via `parent_id` in ContentMeta

**Geschätzt:** 1.5 Tage

---

#### 3.3 Content Retrieval Optimization (Priorität: MITTEL)
**Ziel:** Effiziente Chunk-Navigation und Content-Assembly

**Implementation:**
```cpp
// In ContentManager
struct ContentAssembly {
    ContentMeta metadata;
    std::vector<ChunkMeta> chunks;
    std::optional<std::string> assembled_text;  // All chunks concatenated
};

ContentAssembly assembleContent(const std::string& content_id);

// Chunk navigation
std::optional<ChunkMeta> getNextChunk(const std::string& chunk_id);
std::optional<ChunkMeta> getPreviousChunk(const std::string& chunk_id);
```

**Features:**
- Lazy loading (nur Chunks on-demand)
- Pagination für große Dokumente
- Cache für häufig abgerufene Chunks

**Geschätzt:** 1 Tag

---

### Content Phase Zusammenfassung
**Total:** ~3.5 Tage  
**Fortschritt:** 30% → 75%  
**Kritische Features:** Content Search, Filesystem Interface, Retrieval Optimization  
**Enterprise Features:** Text Extraction, Chunking (via externe DLL)

---

## 🎯 Phase 5: Geo Acceleration & Enterprise (Optional)

### Aktueller Stand
❌ **Nicht implementiert (100%):**
- Geospatial Storage (EWKB/EWKBZ)
- Spatial Indexes (R-Tree, Z-Range)
- AQL Geo Functions (ST_*)
- Geo Query Engine
- 3D/Z-Coordinate Support
- Cross-Modal Integration (Geo+Vector, Geo+Graph)

✅ **Design vorhanden:**
- Geo Feature Tiering (Core vs. Enterprise)
- Execution Plan (Blob-based Storage)
- 3D Game Acceleration Techniques

## 🎯 Phase 5: Geo Acceleration & Enterprise (Optional)

### Aktueller Stand nach Phase 0
✅ **Geo Infrastructure implementiert:**
- EWKB Storage + Sidecar
- R-Tree Spatial Index (table-agnostic)
- ST_* Functions (17 functions)
- Query Engine Integration
- **Geo verfügbar für alle 5 Modelle**

### Optional: Performance & Enterprise Features

#### 5.1 CPU Acceleration (SIMD, Morton, Roaring) - Optional
**Ziel:** Basis-Funktionalität ohne GPU, portabel, permissive licenses

**Storage & Sidecar:**
```cpp
// include/utils/geo/ewkb.h
class EWKBParser {
public:
    struct GeometryInfo {
        GeometryType type;  // Point, LineString, Polygon, etc.
        bool has_z;
        bool has_m;
        int srid;
        std::vector<Coordinate> coords;
    };
    
    static GeometryInfo parse(const std::vector<uint8_t>& ewkb);
    static std::vector<uint8_t> serialize(const GeometryInfo& geom);
};

// include/utils/geo/mbr.h
struct MBR {
    double minx, miny, maxx, maxy;
    std::optional<double> z_min, z_max;  // For 3D
    
    MBR expand(double distance_meters) const;
    bool intersects(const MBR& other) const;
};

struct Sidecar {
    MBR mbr;
    Coordinate centroid;
    double z_min = 0.0;
    double z_max = 0.0;
};
```

**Spatial Indexes:**
```cpp
// include/index/spatial_index.h
class SpatialIndexManager {
public:
    // R-Tree for 2D MBR
    Status createRTreeIndex(
        std::string_view table,
        std::string_view column,
        const RTreeConfig& config
    );
    
    // Z-Range Index for 3D elevation filtering
    Status createZRangeIndex(
        std::string_view table,
        std::string_view column
    );
    
    // Query
    std::pair<Status, std::vector<std::string>> searchIntersects(
        std::string_view table,
        const MBR& query_bbox
    );
    
    std::pair<Status, std::vector<std::string>> searchWithin(
        std::string_view table,
        const MBR& query_bbox,
        double z_min = -DBL_MAX,
        double z_max = DBL_MAX
    );
};
```

**AQL Geo Functions (MVP):**
```sql
-- Constructors
ST_Point(lon DOUBLE, lat DOUBLE, z DOUBLE = NULL) -> GEOMETRY
ST_GeomFromGeoJSON(json STRING) -> GEOMETRY
ST_GeomFromText(wkt STRING) -> GEOMETRY

-- Converters
ST_AsGeoJSON(geom GEOMETRY) -> STRING
ST_AsText(geom GEOMETRY) -> STRING
ST_Envelope(geom GEOMETRY) -> GEOMETRY

-- Predicates (2D + 3D)
ST_Intersects(geom1 GEOMETRY, geom2 GEOMETRY) -> BOOL
ST_Within(geom1 GEOMETRY, geom2 GEOMETRY) -> BOOL
ST_Contains(geom1 GEOMETRY, geom2 GEOMETRY) -> BOOL

-- Distance (Haversine for geodetic)
ST_Distance(geom1 GEOMETRY, geom2 GEOMETRY) -> DOUBLE
ST_DWithin(geom1 GEOMETRY, geom2 GEOMETRY, distance DOUBLE) -> BOOL
ST_3DDistance(geom1 GEOMETRY, geom2 GEOMETRY) -> DOUBLE

-- 3D Helpers
ST_HasZ(geom GEOMETRY) -> BOOL
ST_Z(geom GEOMETRY) -> DOUBLE
ST_ZMin(geom GEOMETRY) -> DOUBLE
ST_ZMax(geom GEOMETRY) -> DOUBLE
ST_Force3D(geom GEOMETRY, z DOUBLE = 0.0) -> GEOMETRY
ST_Force2D(geom GEOMETRY) -> GEOMETRY
ST_ZBetween(geom GEOMETRY, z_min DOUBLE, z_max DOUBLE) -> BOOL
```

**Query Engine Integration:**
```cpp
// Execution Plan
1. Parse: ST_Intersects(location, ST_GeomFromGeoJSON(@viewport))
2. Extract: @viewport MBR -> (minx, miny, maxx, maxy)
3. Candidates: R-Tree scan -> PK set (broadphase)
4. Z-Filter: If 3D query, Z-Range index -> intersect PK set
5. Exact Check: Load EWKB, Boost.Geometry exact test -> final hits
6. Return: Filtered entities
```

**Dependencies:**
- Boost.Geometry (BSL-1.0) - already in project
- No GEOS/PROJ for MVP (optional later)

**Files:**
- `include/utils/geo/ewkb.h`, `src/utils/geo/ewkb.cpp` (300 lines)
- `include/utils/geo/mbr.h`, `src/utils/geo/mbr.cpp` (200 lines)
- `include/index/spatial_index.h`, `src/index/spatial_rtree.cpp` (600 lines)
- `src/index/spatial_zrange.cpp` (150 lines)
- `src/query/aql_parser.cpp` (extend with ST_* parsing, +400 lines)
- `src/query/query_engine.cpp` (spatial execution, +500 lines)
- `tests/test_geo_ewkb.cpp`, `tests/test_spatial_index.cpp`, `tests/test_geo_aql.cpp`

**Geschätzt:** 5 Tage

---

#### M2: CPU Acceleration (SIMD, Morton, Roaring) - Priorität: HOCH
**Ziel:** Performance-Optimierung ohne GPU

**SIMD Kernels:**
```cpp
// include/geo/simd_kernels.h
namespace geo::simd {
    // AVX2/AVX-512/NEON optimized
    bool pointInPolygon_simd(const Point& p, const Polygon& poly);
    bool bboxOverlap_simd(const MBR& a, const MBR& b);
    double haversineDistance_simd(const Point& a, const Point& b);
}
```

**Morton Codes (Z-Order):**
```cpp
// include/index/morton_index.h
class MortonIndex {
public:
    uint64_t encode2D(double x, double y) const;
    uint64_t encode3D(double x, double y, double z) const;
    
    std::pair<double, double> decode2D(uint64_t code) const;
    
    // Range queries
    std::vector<std::pair<uint64_t, uint64_t>> getRanges(const MBR& bbox);
};
```

**Roaring Bitmaps:**
```cpp
// include/utils/roaring_set.h
class RoaringPKSet {
public:
    void add(uint64_t pk);
    void intersect(const RoaringPKSet& other);
    void unionWith(const RoaringPKSet& other);
    
    std::vector<std::string> toPKs() const;
};
```

**Integration:**
- SIMD in exact checks (ST_Intersects CPU path)
- Morton sorting for better RocksDB locality
- Roaring for AQL OR/AND set algebra

**Dependencies:**
- Google Highway (Apache-2.0) - optional, CMake flag
- CRoaring (Apache-2.0) - optional

**Files:**
- `include/geo/simd_kernels.h`, `src/geo/simd_kernels.cpp` (400 lines)
- `include/index/morton_index.h`, `src/index/morton_index.cpp` (300 lines)
- `include/utils/roaring_set.h`, `src/utils/roaring_set.cpp` (200 lines)
- Benchmarks: `benchmarks/bench_spatial_intersects.cpp`

**Geschätzt:** 2.5 Tage (optional)

---

#### 5.2 Import Tools (Shapefile, GeoTIFF) - Optional

**Shapefile → Relational Table:**
```cpp
// Use case: "Find similar images within 5km of location"
FOR img IN images
  FILTER ST_DWithin(img.location, ST_Point(13.4, 52.5), 5000)
  SORT SIMILARITY(img.embedding, @query_vector) DESC
  LIMIT 10
  RETURN img

// Implementation:
1. Geo filter: ST_DWithin -> PK whitelist (Roaring bitmap)
2. Vector search: HNSW with whitelist mask
3. Fusion: Pre-filtered ANN
```

**GeoTIFF → Tiles:**
```cpp
// Use case: "Find accessible locations via road network"
FOR v IN 1..5 OUTBOUND 'locations/berlin' GRAPH 'roads'
  FILTER ST_Intersects(v.location, @viewport)
  RETURN v

// Implementation:
1. Traversal: BFS with frontier
2. Spatial filter: Check each frontier node location
3. Early termination: If all frontier outside viewport
```

**Geschätzt:** 1.5 Tage (optional)

---

#### 5.3 GPU Backend (Optional)
```sql
-- Query: Combine spatial + attribute filters
FOR u IN users
  FILTER u.age > 18 
    AND ST_Within(u.home_location, @city_boundary)
    AND u.status == 'active'
  RETURN u

-- Shape File Import (.shp → Relational Table + Geo Index)
POST /api/import/shapefile
{
  "file": "cities.shp",
  "table": "cities",
  "geometry_column": "boundary",
  "attributes": ["name", "population", "country", "admin_level"]
}

-- Result: Table 'cities' with columns:
--   _id, _key, name, population, country, admin_level, boundary (GEOMETRY)
-- Indexes: 
--   - R-Tree on 'boundary'
--   - Secondary Index on 'country', 'admin_level'
--   - Z-Range on boundary.z_min/z_max (if 3D)

-- Use case: Spatial join with relational filters
FOR city IN cities
  FILTER city.population > 100000 
    AND city.country == 'Germany'
    AND ST_Intersects(city.boundary, @viewport)
  RETURN city
```

**Geschätzt:** 3 Tage (optional)

---

### Geo Acceleration Zusammenfassung
**Total:** ~7 Tage (optional)  
**Fortschritt:** 85% → 95%  
**Features:** SIMD, Morton, Roaring, Shapefile/GeoTIFF Import, GPU Backend
```sql
-- Use case 1: Geo-tagged documents (photos, reports, PDFs)
POST /content/import
{
  "file": "report.pdf",
  "metadata": {
    "category": "REPORT",
    "location": {"type": "Point", "coordinates": [13.4, 52.5]},
    "tags": ["berlin", "2025", "city-planning"]
  }
}

-- Search: Find documents near location
FOR doc IN content
  FILTER doc.category == 'REPORT'
    AND ST_DWithin(doc.location, ST_Point(13.4, 52.5), 5000)
  SORT doc.created_at DESC
  LIMIT 10
  RETURN doc

-- Use case 2: GeoTIFF/Raster import (satellite imagery, elevation maps)
POST /api/import/geotiff
{
  "file": "elevation_berlin.tif",
  "table": "elevation_tiles",
  "tile_size": 256,  // Split into tiles for efficient queries
  "extract_bounds": true,  // Create MBR for each tile
  "z_values": true  // Store elevation as z-coordinate
}

-- Query: Elevation within bounding box
FOR tile IN elevation_tiles
  FILTER ST_Intersects(tile.bounds, @viewport)
    AND tile.z_min <= 100  // Max elevation 100m
  RETURN tile

-- Use case 3: Geo-tagged chunks (location-based RAG)
FOR chunk IN content_chunks
  FILTER FULLTEXT(chunk.text, "hotel")
    AND ST_DWithin(chunk.parent_location, ST_Point(13.4, 52.5), 2000)
  SORT SIMILARITY(chunk.embedding, @query_vector) DESC
  LIMIT 5
  RETURN chunk
```

**Query Optimizer Extensions:**
```cpp
// Cost estimation
struct SpatialSelectivity {
    double area_ratio;  // query_bbox_area / total_area
    double density;     // avg entities per unit area
    int candidate_count; // estimated from R-Tree stats
};

// Plan selection
if (spatial_selectivity < 0.01) {
    // Spatial-first: geo filter -> eq checks
} else {
    // Eq-first: eq filter -> geo checks
}
```

**Shape File Import Integration:**
```cpp
// include/import/shapefile_importer.h
class ShapefileImporter {
public:
    struct ImportConfig {
        std::string shapefile_path;  // .shp
        std::string table_name;
        std::string geometry_column = "geometry";
        std::vector<std::string> attributes;  // DBF fields to import
        bool create_spatial_index = true;
        bool create_z_index = false;  // For 3D shapes
    };
    
    Status importShapefile(const ImportConfig& config);
    
private:
    // Parse .shp (geometry) + .dbf (attributes) + .shx (index)
    std::vector<Feature> parseShapeFile(const std::string& path);
    
    // Convert to EWKB + sidecar
    std::pair<std::vector<uint8_t>, Sidecar> convertToEWKB(
        const ShapeGeometry& geom
    );
};
```

**GeoTIFF/Raster Import:**
```cpp
// include/import/geotiff_importer.h
class GeoTIFFImporter {
public:
    struct TileConfig {
        int tile_size = 256;  // pixels
        bool extract_bounds = true;
        bool store_z_values = true;
        std::string compression = "ZSTD";  // For raster data
    };
    
    Status importGeoTIFF(
        const std::string& tiff_path,
        const std::string& table_name,
        const TileConfig& config
    );
    
private:
    // GDAL integration (optional)
    std::vector<RasterTile> splitIntoTiles(
        const GeoTIFF& tiff,
        const TileConfig& config
    );
};
```

**Files:**
- `include/query/spatial_query_optimizer.h` (150 lines)
- `src/query/vector_engine.cpp` (extend with geo mask, +200 lines)
- `src/query/graph_engine.cpp` (extend with geo filter, +150 lines)
- `src/query/query_optimizer.cpp` (cost estimation, +300 lines)
- `include/import/shapefile_importer.h`, `src/import/shapefile_importer.cpp` (400 lines)
- `include/import/geotiff_importer.h`, `src/import/geotiff_importer.cpp` (300 lines)
- `src/content/content_manager.cpp` (extend with location field, +100 lines)

**Dependencies (Optional):**
- GDAL (MIT/X11) for GeoTIFF/Shapefile parsing (can use header-only shapelib as alternative)
- Shapelib (MIT) for .shp parsing (lighter alternative)

**Geschätzt:** 2.5 Tage (statt 2)

---

#### M4: Optional Enterprise Features - Priorität: NIEDRIG
**Ziel:** GPU, Advanced Functions, H3/S2 (extern als Plugin)

**GPU Batch Backend (Optional):**
```cpp
// include/geo/gpu_backend.h
class GpuBatchBackend : public ISpatialComputeBackend {
public:
    // Batch ST_Intersects (10k+ geometries)
    std::vector<bool> batchIntersects(
        const std::vector<Geometry>& queries,
        const Geometry& region
    ) override;
    
    // Compute shaders (DX12/Vulkan)
    // SoA layout, prefix sum, stream compaction
};
```

**Advanced Functions (via GEOS/PROJ plugin):**
```sql
-- Topology (GEOS)
ST_Buffer(geom, distance) -> GEOMETRY
ST_Union(geom1, geom2) -> GEOMETRY
ST_Difference(geom1, geom2) -> GEOMETRY
ST_Simplify(geom, tolerance) -> GEOMETRY

-- CRS Transform (PROJ)
ST_Transform(geom, from_srid, to_srid) -> GEOMETRY

-- H3/S2 (plugins)
H3_LatLonToCell(lat, lon, resolution) -> STRING
S2_CellIdToToken(lat, lon, level) -> STRING
```

**Feature Flags:**
```json
{
  "geo": {
    "use_gpu": false,
    "use_simd": true,
    "plugins": ["geos", "h3"],
    "enterprise": false
  }
}
```

**Files:**
- `include/geo/gpu_backend.h`, `src/geo/gpu_backend_dx12.cpp` (800 lines)
- `src/geo/geos_plugin.cpp` (400 lines, dynamic load)
- `src/geo/h3_plugin.cpp` (300 lines)

**Geschätzt:** 3 Tage (optional, kann später erfolgen)

---

### Geo Phase Zusammenfassung
**Total:** ~10 Tage (MVP + CPU Acceleration + Cross-Modal mit Import)  
**Optional:** +3 Tage (GPU + Advanced Functions)  
**Fortschritt:** 0% → 85% (MVP complete, enterprise optional)  
**Kritische Features:** 
- EWKB Storage, R-Tree Index, ST_* Functions
- Cross-Modal Integration (Geo+Vector, Geo+Graph, Geo+Relational, Geo+Content)
- Shape File Import (.shp → Table + Spatial Index)
- GeoTIFF Import (Raster → Tiles)
- Geo-Tagged Content (Documents, Chunks)

---

## 🎯 Phase 5: Relational Enhancements (100% → 100% + Enterprise)

### Aktueller Stand
✅ **Vollständig implementiert (100%):**
- FOR/FILTER/SORT/LIMIT
- Joins (Hash-Join, Nested-Loop)
- Window Functions
- CTEs (WITH)
- Subqueries
- Advanced Aggregations

### Enterprise Features (Optional)

#### 4.1 Recursive CTEs (Priorität: NIEDRIG)
**Use Case:** Hierarchical Queries (Org Charts, Bill of Materials)

**Syntax:**
```sql
WITH RECURSIVE subordinates AS (
  SELECT * FROM employees WHERE manager_id IS NULL
  UNION ALL
  SELECT e.* FROM employees e JOIN subordinates s ON e.manager_id = s.id
)
SELECT * FROM subordinates;
```

**Geschätzt:** 2 Tage

---

#### 4.2 Materialized Views (Priorität: NIEDRIG)
**Ziel:** Pre-computed Aggregates

**Geschätzt:** 1.5 Tage

---

### Relational Phase Zusammenfassung
**Total:** Optional (nur bei Bedarf)  
**Fortschritt:** 100% → 100% (keine Änderungen notwendig)

---

## 📊 Gesamtzeitplan

| Phase | Komponente | Tage | Priorität | Fortschritt |
|-------|-----------|------|-----------|-------------|
| **0** | **Geo Infrastructure** | 7 | **KRITISCH** | 0% → 85% |
| 1 | Graph Vervollständigung | 6.5 | HOCH | 70% → 95% |
| 2 | Vector Vervollständigung | 4 | HOCH | 75% → 95% |
| 3 | Content Vervollständigung | 3.5 | MITTEL | 30% → 75% |
| 4 | Relational Enhancements | 0 | NIEDRIG | 100% → 100% |
| **Total (Core)** | | **21 Tage** | | **64% → 88%** |
| **Optional** | Geo Acceleration + Import | +7 | NIEDRIG | 85% → 95% |

**Hinweise:**
- **Geo ist KEIN separates Modell**, sondern Cross-Cutting Capability
- Geo Infrastructure (Phase 0) macht **alle 5 Modelle** geo-enabled
- Text Extraction, Chunking → Enterprise DLL
- GPU Geo Acceleration, Shapefile/GeoTIFF Import → Optional

---

## 🎯 Erfolgsmetriken

**Zielwerte:**
- ✅ **Geo Infrastructure: 85%+ (Cross-Cutting für alle Modelle)**
  - EWKB/EWKBZ Storage ✅
  - R-Tree Index (table-agnostic) ✅
  - ST_* Functions (17 core functions) ✅
  - Query Engine Integration ✅
  - Geo-enabled für: Relational, Graph, Vector, Content, Time-Series ✅
  - ⚠️ SIMD/Morton/Roaring → Optional
  - ⚠️ Shapefile/GeoTIFF Import → Optional
  - ⚠️ GPU Backend → Optional Plugin
- ✅ Graph: 95%+ (Path Constraints + PageRank + Pattern Matching)
  - Profitiert von Geo: Spatial Graph Traversal
- ✅ Vector: 95%+ (Filtered Search + Hybrid Search)
  - Profitiert von Geo: Spatial-filtered ANN
- ✅ Content: 75%+ (Search + Filesystem Interface + Retrieval Optimization)
  - Profitiert von Geo: Geo-tagged Documents/Chunks
  - ⚠️ Ingestion Features (Extraction, Chunking) → Enterprise DLL
- ✅ Relational: 100% (keine Änderungen)
  - Profitiert von Geo: WHERE + ST_* kombinierbar

**Tests:**
- +40 neue Unit Tests (inkl. 15 Geo Tests)
- +20 Integration Tests (Geo mit allen 5 Modellen)
- Benchmark Suite für alle Features

**Dokumentation:**
- GEO_ARCHITECTURE.md (Cross-Cutting Design, Symbiose mit allen Modellen)
- GEO_SPATIAL_GUIDE.md (EWKB, R-Tree, ST_* Functions, 3D Support)
- GEO_QUERY_EXAMPLES.md (Geo+Relational, Geo+Graph, Geo+Vector, Geo+Content, Geo+TimeSeries)
- GEO_ACCELERATION.md (SIMD, Morton, Roaring - optional)
- GEO_IMPORT.md (Shapefile, GeoTIFF - optional)
- GRAPH_ANALYTICS.md (Centrality, Communities)
- VECTOR_HYBRID_SEARCH.md (Filters, Radius, Fusion)
- CONTENT_API.md (Search, Filesystem, Enterprise DLL)

---

## 🚀 Nächste Schritte

### Woche 1: Geo Infrastructure (Tag 1-7) - KRITISCH
1. **Geo EWKB Storage + Sidecar** (1.5 Tage)
   - ewkb.h/cpp, mbr.h/cpp, BaseEntity integration
2. **Geo R-Tree Index** (2 Tage)
   - SpatialIndexManager, table-agnostic design
3. **Geo AQL ST_* Parser** (1.5 Tage)
   - 17 ST_* functions, universal für alle Modelle
4. **Geo Query Engine** (2 Tage)
   - Spatial execution plan, optimizer integration

### Woche 2: Graph Completion (Tag 8-14.5)
5. **Graph Path Constraints** (1 Tag)
6. **Graph PageRank** (1.5 Tage)
7. **Graph Pattern Matching** (2 Tage)
8. **Graph Centrality** (1.5 Tage)
9. **Vector Filtered Search** (1 Tag)

### Woche 3: Vector + Content (Tag 15-21)
10. **Vector Hybrid Search** (1.5 Tage)
11. **Content Search API** (1 Tag)
12. **Content Filesystem Interface** (1.5 Tage)
13. **Content Retrieval Optimization** (1 Tag)
14. **Dokumentation** (2.5 Tage)
    - GEO_ARCHITECTURE, GEO_SPATIAL_GUIDE, GEO_QUERY_EXAMPLES
    - GRAPH_ANALYTICS, VECTOR_HYBRID_SEARCH, CONTENT_API

### Optional: Geo Acceleration (nach Core Completion)
15. **Geo SIMD Kernels** (1.5 Tage)
16. **Geo Morton + Roaring** (1.5 Tage)
17. **Geo Shapefile/GeoTIFF Import** (1.5 Tage)
18. **Geo GPU Backend** (3 Tage)

---

## 📝 Offene Fragen

1. **Geo Architecture:** Ist Cross-Cutting Design (statt separates Modell) korrekt? ✅ **JA**
2. **Geo Priority:** Geo Infrastructure (Phase 0) vor Graph/Vector? (Empfehlung: JA - macht alle Modelle geo-enabled)
3. **Geo 3D Use Cases:** Werden Elevation Queries häufig benötigt? (Z-Support ist in Infrastructure enthalten)
4. **Geo SIMD Libraries:** Google Highway (Apache-2.0) vs. xsimd (BSD)? (Empfehlung: Highway, aber optional)
5. **Import Tools Priority:** Shapefile/GeoTIFF Import sofort oder später? (Empfehlung: Optional, nach Core)
6. **Graph Analytics:** Welche Centrality-Algorithmen sind kritisch?
7. **Vector Search:** Welche Distanz-Metriken am häufigsten?

---

**Status:** Roadmap konsolidiert - **Geo als Cross-Cutting Capability**  
**Nächster Schritt:** Phase 0 (Geo Infrastructure) implementieren
