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

## Erweiterungen

Zukünftige Benchmarks:
- Graph+Geo Shortest Path (SHORTEST_PATH TO Syntax vs dijkstra() API)
- Verschiedene Datensatzgrößen (100, 1K, 10K, 100K)
- Composite Index Selektivitätsvarianten
- Spatial BBox Größenvariationen (bbox_ratio 0.01, 0.1, 0.5, 1.0)
- Overfetch-Parameter Sweeps (1x, 2x, 5x, 10x)
