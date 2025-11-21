# Hybrid AQL Benchmark Suite

Benchmark-Vergleich zwischen AQL Syntax-Zucker und direkter C++ API für Hybrid Queries.

## Benchmarks

1. **Vector+Geo via AQL Sugar**: SIMILARITY() Syntax in AQL Query
2. **Vector+Geo via C++ API**: Direkte `executeVectorGeoQuery()` Aufrufe
3. **Content+Geo via AQL Sugar**: PROXIMITY() Syntax mit FULLTEXT Filter
4. **Content+Geo via C++ API**: Direkte `executeContentGeoQuery()` Aufrufe
5. **Parse+Translate Overhead**: Nur Parsing und Translation ohne Execution

## Setup

Test-Datensatz:
- 1000 Hotels mit synthetischen Daten
- Felder: name, city, category, stars, location (GeoJSON), embedding (128D)
- Indizes: city (equality), stars (range), (city, category) composite, location (spatial), embedding (HNSW)

## Ausführung

```powershell
# Build
cmake --build build --target bench_hybrid_aql_sugar --config Release

# Run
.\build\Release\bench_hybrid_aql_sugar.exe --benchmark_repetitions=5 --benchmark_report_aggregates_only=true
```

## Erwartete Metriken

- **Latenz**: Durchschnittliche Query-Ausführungszeit (ms)
- **Durchsatz**: Queries pro Sekunde
- **Overhead**: AQL vs C++ API Delta (Parsing + Translation)
- **Planwahl**: Optimizer-Attribute aus Tracer (vector-first vs spatial-first)

## Auswertung

Span-Attribute im Tracing-Backend analysieren:
- `optimizer.plan`: gewählter Plan
- `optimizer.cost_*`: Kostenmodell-Werte
- `index_prefilter_size`: Prefilter-Reduktion
- `composite_prefilter_applied`: Composite Index Nutzung

## Beispiel-Ausgabe

```
---------------------------------------------------------------------
Benchmark                           Time             CPU   Iterations
---------------------------------------------------------------------
BM_VectorGeo_AQL_Sugar           5.23 ms         5.18 ms          134
BM_VectorGeo_CPP_API             4.87 ms         4.83 ms          145
BM_ContentGeo_AQL_Sugar          3.45 ms         3.42 ms          204
BM_ContentGeo_CPP_API            3.21 ms         3.18 ms          219
BM_AQL_Parse_Translate_Only      125 us          123 us         5682
```

**Interpretation**:
- AQL Overhead ~7% (Parsing + Translation)
- Beide Zugriffswege nutzen dieselbe Execution Engine
- Syntax-Zucker bietet bessere Wartbarkeit bei minimalem Performance-Verlust

## Erweiterte Benchmarks

### 6. Graph+Geo Shortest Path

Vergleich zwischen AQL SHORTEST_PATH TO Syntax und direkter dijkstra() API:

**AQL Syntax:**
```aql
FOR v, e IN SHORTEST_PATH 
  (SELECT * FROM locations WHERE city = 'Berlin')
  TO (SELECT * FROM locations WHERE city = 'Munich')
  GRAPH 'transport'
  OPTIONS {
    weightAttribute: 'distance',
    geoFilter: {
      maxDistance: 50000,
      radiusCenter: [13.4, 52.5]
    }
  }
RETURN v
```

**C++ API:**
```cpp
auto result = g_graphIdx->dijkstra(
    start_vertex_id,
    end_vertex_id,
    "distance",
    GeoFilter{50000.0, {13.4, 52.5}}
);
```

**Metriken:**
- Latenz für verschiedene Pfadlängen (5, 10, 50, 100 Knoten)
- Geo-Filter Reduktionsrate
- Parsing/Translation Overhead vs direkte API
- Speichernutzung für Pfadrekonstruktion

**Erwartete Ergebnisse:**
- AQL Overhead: 8-12% (höher als Vector/Geo durch Graph-Parsing)
- Geo-Filter beschleunigt Suche um 40-60% bei räumlich konzentrierten Pfaden
- Dijkstra-Heap-Größe korreliert mit Anzahl besuchter Knoten

### 7. Datensatzgrößen-Variationen

Systematischer Vergleich über verschiedene Datensatzgrößen:

**Test-Szenarien:**
| Größe | Hotels | Vektoren | Spatial Punkte | Graph Knoten |
|-------|--------|----------|----------------|--------------|
| XS    | 100    | 100      | 100            | 50           |
| S     | 1K     | 1K       | 1K             | 500          |
| M     | 10K    | 10K      | 10K            | 5K           |
| L     | 100K   | 100K     | 100K           | 50K          |

**Gemessene Metriken:**
- Query Latenz (p50, p95, p99)
- Index Build Zeit
- Index Größe (MB)
- RAM Verbrauch während Query Execution
- Cache Hit Rate (RocksDB Block Cache)

**Setup pro Größe:**
```powershell
# Generate dataset
.\generate_test_data.ps1 -Size 100K -Collection hotels

# Build indices
cmake --build build --target bench_hybrid_aql_sugar --config Release

# Run benchmarks
.\build\Release\bench_hybrid_aql_sugar.exe --benchmark_filter=".*_100K" --benchmark_repetitions=10
```

**Erwartete Skalierungseigenschaften:**
- Vector Search: O(log N) durch HNSW
- Spatial Range: O(log N + k) durch R*-Tree
- Composite Index: O(log N) für Equality Prefix

### 8. Composite Index Selektivitätsvarianten

Test der Optimizer-Planwahl bei unterschiedlicher Selektivität:

**Szenarien:**
| Szenario | City Kardinalität | Category Kardinalität | Erwartete Reduktion |
|----------|-------------------|----------------------|---------------------|
| Hoch     | 3                 | 3                    | ~11% (1/9)          |
| Mittel   | 10                | 5                    | ~2% (1/50)          |
| Niedrig  | 50                | 20                   | ~0.1% (1/1000)      |

**AQL Query:**
```aql
FOR hotel IN hotels
  FILTER hotel.city == @city 
    AND hotel.category == @category
    AND DISTANCE(hotel.location, @center) < @radius
  SIMILARITY(hotel.embedding, @query_vector, 10)
RETURN hotel
```

**Gemessene Attribute (Tracer):**
- `composite_prefilter_applied`: true/false
- `composite_prefilter_size`: Anzahl Kandidaten nach Composite Index
- `spatial_prefilter_size`: Anzahl Kandidaten nach Spatial Filter
- `optimizer.plan`: "composite_first" | "spatial_first" | "vector_first"
- `optimizer.selectivity_estimate`: geschätzte Selektivität

**Erwartete Plan-Wahl:**
- **Hohe Selektivität**: Composite Index First → Spatial → Vector
- **Mittlere Selektivität**: Spatial First → Composite → Vector
- **Niedrige Selektivität**: Vector First (HNSW) → Spatial → Composite

**Kostenmodell-Validierung:**
```
Cost_composite = log(N) + selectivity * N * cost_spatial
Cost_spatial = log(N) + area_ratio * N * cost_vector
Cost_vector = log(N) + k * ef_search * dim
```

### 9. Spatial BBox Größenvariationen

Performance-Analyse bei unterschiedlichen Bounding Box Größen:

**BBox Ratios (relativ zur Gesamt-Ausdehnung):**
| Ratio | Beschreibung | Beispiel (Berlin) | Erwartete Treffer |
|-------|--------------|-------------------|-------------------|
| 0.01  | Sehr klein   | 1km × 1km         | ~10 (1%)          |
| 0.1   | Klein        | 5km × 5km         | ~100 (10%)        |
| 0.5   | Mittel       | 15km × 15km       | ~500 (50%)        |
| 1.0   | Groß         | 30km × 30km       | ~1000 (100%)      |

**AQL Query Template:**
```aql
FOR hotel IN hotels
  FILTER GEO_INTERSECTS(
    hotel.location,
    GEO_POLYGON([
      [@minLon, @minLat],
      [@maxLon, @minLat],
      [@maxLon, @maxLat],
      [@minLon, @maxLat],
      [@minLon, @minLat]
    ])
  )
  SIMILARITY(hotel.embedding, @query_vector, @k)
RETURN hotel
```

**Metriken:**
- R*-Tree Node Visits (interne Metrik)
- Spatial Filter Hit Ratio: `spatial_results / bbox_area_candidates`
- Vector Rerank Cost: `spatial_results * dim * cost_distance`
- Total Latency Breakdown:
  - Spatial Index Scan: X ms
  - Vector Rerank: Y ms
  - Result Assembly: Z ms

**Erwartete Trends:**
- **bbox_ratio < 0.1**: Spatial Index dominant (< 5ms), Vector Rerank negligible
- **bbox_ratio 0.1-0.5**: Balance zwischen Spatial (5-15ms) und Vector (10-30ms)
- **bbox_ratio > 0.5**: Vector Rerank dominant (> 50ms), R*-Tree nur marginaler Nutzen

**Optimizer Heuristik:**
```
if (bbox_ratio > 0.3 && k < 100) {
  plan = VECTOR_FIRST;  // HNSW effizienter als großer BBox Scan
} else {
  plan = SPATIAL_FIRST;
}
```

### 10. Overfetch-Parameter Sweeps

Analyse des Overfetch-Faktors für Vector+Geo Hybrid Queries:

**Overfetch Definition:**
```
overfetch_factor = spatial_prefetch_size / k
```

**Test-Matrix:**
| Overfetch | k=10  | k=50  | k=100 | Spatial Hit Rate |
|-----------|-------|-------|-------|------------------|
| 1x        | 10    | 50    | 100   | ~30% (Baseline)  |
| 2x        | 20    | 100   | 200   | ~50%             |
| 5x        | 50    | 250   | 500   | ~75%             |
| 10x       | 100   | 500   | 1000  | ~90%             |

**Spatial Hit Rate = k / spatial_prefetch_size**

**Implementierung:**
```cpp
// C++ API mit explizitem Overfetch
auto options = VectorGeoQueryOptions{
    .vector_k = k,
    .overfetch_factor = 5.0,  // Fetch 5*k from spatial index
    .geo_filter = bbox,
    .rerank_metric = "cosine"
};
auto result = g_engine->executeVectorGeoQuery("hotels", options);
```

**AQL Syntax (implizit):**
```aql
FOR hotel IN hotels
  FILTER GEO_DISTANCE(hotel.location, @center) < @radius
  SIMILARITY(hotel.embedding, @query_vector, @k)
  OPTIONS {
    overfetch: 5  // Optimizer Hint
  }
RETURN hotel
```

**Gemessene Metriken:**
- **Recall@k**: Anteil der echten Top-k Ergebnisse gefunden
- **Precision@k**: Anteil korrekter Ergebnisse in Top-k
- **Total Latency**: Spatial + Vector Rerank
- **Spatial Index Calls**: Anzahl R*-Tree Traversierungen
- **Vector Distance Computations**: Anzahl Distanzberechnungen

**Trade-off Analyse:**
| Overfetch | Recall@10 | Latency (ms) | Distance Comps |
|-----------|-----------|--------------|----------------|
| 1x        | 65%       | 3.2          | 10             |
| 2x        | 85%       | 4.1          | 20             |
| 5x        | 96%       | 6.8          | 50             |
| 10x       | 99%       | 11.5         | 100            |

**Optimizer Auto-Tuning:**
```cpp
// Adaptiver Overfetch basierend auf historischer Hit Rate
double auto_overfetch = std::min(
    10.0,
    k / std::max(0.1, historical_spatial_hit_rate)
);
```

**Empfehlung:**
- **Precision-Critical**: overfetch = 5-10x (Recall > 95%)
- **Latency-Critical**: overfetch = 2x (Recall ~85%, 30% schneller)
- **Balanced**: overfetch = 3x (Recall ~90%, guter Trade-off)
