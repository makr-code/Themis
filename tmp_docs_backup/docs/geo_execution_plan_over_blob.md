# Geo Execution Plan (oberhalb des Blob-Layers)

Ziel: Geo-Funktionalität aufbauend auf dem bestehenden Themis-Stack (Storage/Blob, Secondary Indices, Query Engine, HTTP) integrieren. Geo-Daten liegen als WKB/EWKB(Z)-Blobs vor; darüber implementieren wir Sidecar-Metadaten, Indizes, AQL-Syntax (ST_*), Engine-Pfade und optionale GPU/CPU-Beschleuniger.

Leitprinzipien
- API-stabil: GeoJSON als API/Interchange, intern EWKB(Z) im Blob-Feld.
- Index-first: Kandidatenfilter via R-Tree (2D) + Z-Range; exakte Prüfung via Boost.Geometry (CPU). Optionale Beschleuniger (SIMD/GPU) hinter Feature-Flags.
- Cross-Modal: Geo-Filter als Baustein für Graph/Vector/Relational/File-Pfade; einheitliche Kosten-/Metrik-Signale.
- Portabel & OSS-konform: Boost.Geometry (BSL-1.0), optional PROJ; GEOS nur optional (dyn).

Schichten & Schnittstellen
1) Storage & Sidecar (neu)
- Feld `geom_blob` (EWKB(Z)) in Entities.
- Sidecar: `mbr(minx,miny,maxx,maxy)`, `centroid(lon,lat)`, `z_min`, `z_max` (berechnet beim Write/Update).
- Module:
  - `include/utils/geo/ewkb.h`, `src/utils/geo/ewkb.cpp` – Parser/Serializer, Validation.
  - `include/utils/geo/mbr.h`, `src/utils/geo/mbr.cpp` – BBox/centroid-Berechnung.

2) Indizes (MVP + optional)
- MVP: R-Tree (R*-ähnlich) über 2D-MBR in `SecondaryIndexManager` (CF "spatial").
  - API: `createSpatialIndex(table, column, options)`; Rebuild/Scan; Stats (node_count, depth, candidate_count).
- 3D: Z-Range-Index auf (`z_min`,`z_max`) als Range-Index, AND-gekoppelt mit MBR.
- Optional: H3-Pre-Filter-Index (1–2 Auflösungen) für Aggregation/Geofencing.
- Module:
  - `include/index/spatial_index.h` (Interface), `src/index/spatial_rtree.cpp` (MVP), `src/index/spatial_zrange.cpp`.

3) AQL Syntax & Parser
- Funktionen (MVP): `ST_Point`, `ST_GeomFromGeoJSON/Text`, `ST_AsGeoJSON/Text`, `ST_Envelope`, `ST_Distance`, `ST_DWithin`, `ST_Intersects`, `ST_Within`, `ST_Contains`.
- Parser erweitert um ST_* Funktionsknoten, Argumentchecks, Fehlertexte.
- Module:
  - `src/query/aql_parser.cpp` (+ Header in `include/query/`), Tests in `tests/test_aql_parser.cpp` erweitern.

4) Translator & Engine-Pfade
- Translator: führt `spatialPredicate` ein; DNF/OR orchestriert wie bei FULLTEXT; max. ein spatialPredicate pro AND-Klausel (MVP) – später kombinierbar.
- Engine: Ausführungsplan
  - Kandidaten: R-Tree (BBox) ∩ Z-Range (optional) → PK-Set
  - Exaktprüfung: Boost.Geometry (CPU) → finaler Treffer
  - DWithin: BBox-expand (geodätisch approx) → exakte Distanz (Haversine)
  - KNN: Distance-ORDER BY mit Early-Out (Heap) – MVP seriell
- Module:
  - `include/query/query_engine.h` (spatial-APIs), `src/query/query_engine.cpp` (spatial-exec), zusätzliche Metriken.

5) Beschleuniger-Backends (optional)
- Abstraktion: `ISpatialComputeBackend` mit Implementierungen:
  - `CpuExactBackend` (Default): Boost.Geometry + optionale SIMD-Kerne (Highway/xsimd) für PiP, BBox-Overlap, Haversine.
  - `GpuBatchBackend` (Optional): Compute-Shader-Pipelines (DX12/Vulkan) für Batch-Intersects/DWithin; zwingender CPU-Fallback.
- Datenlayout: SoA/AoSoA Vektoren (Koordinaten/MBR/Offsets); Morton-Sortierung als optionaler Pre-Pass (Locality).
- Feature-Flags: `geo.use_gpu=false` (Default), `geo.use_simd=true` sofern CPU unterstützt.
- Module:
  - `include/geo/spatial_backend.h`, `src/geo/cpu_backend.cpp`, `src/geo/gpu_backend_stub.cpp` (später echtes Backend), `third_party` optional (Highway/xsimd/keine Pflicht).

Hinweis zum Tiering/Plugins: Details zur Aufteilung Core vs. Enterprise, Build-/Runtime-Flags und Capability-API siehe `docs/geo_feature_tiering.md`.

6) Cross-Modal Integration
- Vector: Engine-Interface erhält optionales Kandidaten-PK-Set (Mask) aus Geo-Filter; Pipeline: Geo-Prefilter → ANN → Score-Fusion (gewichtbar).
- Graph: Traversal-Startmenge und Frontier durch `ST_Intersects(..., region)` geschnitten; Query-Plan bevorzugt räumliche Restriktion früh.
- Relational: Kombinierbar mit EQ/RANGE-Prädikaten; Optimizer bewertet Reihenfolge (spatial-first vs. eq-first).
- File/Ingress: GeoJSON-Import (Content-Ingestion) füllt `geom_blob` + Sidecar; optional Tools zum Vorberechnen (H3/Morton/Quantisierung).
- Module/Änderungen:
  - Vector: `include/query/vector_search.h` – Kandidatenmaske; `src/query/vector_engine.cpp` nutzt Maske.
  - Graph: `src/query/graph_engine.cpp` – region-constraint; Parser ermöglicht `ST_*` im Traversal-FILTER.

7) Observability & Qualität
- Metriken: `spatial.index_hits`, `spatial.candidate_count`, `spatial.exact_checks`, `spatial.z_pruned`, `spatial.simd_hits`, `spatial.gpu_batches`.
- Tests: Korrektheit vs. Boost.Geometry, Engine-End2End, HTTP-API, Benchmarks (Intersects/DWithin, Indexbau).
- Quality Gates: Build/Lint/Tests; Vergleichsläufe für P95.

Phasenplan (inkrementell)
M1 – MVP (CPU, ohne GPU)
- EWKB(Z) + Sidecar; R-Tree + Z-Range; ST_* (Kernset); Engine-Pfad Index→Exact; HTTP/OpenAPI; Basis-Metriken/Tests.

M2 – CPU-Beschleuniger
- SIMD-Kerne (PiP, BBox-Overlap, Haversine) + Morton-Order Pre-Pass; Roaring-Bitmaps für OR/AND-Set-Algebra; Benchmarks.

M3 – Cross-Modal
- Geo+Vector Kandidatenmaske, Geo+Graph Frontier-Filter, Beispiele/Benchmarks; Kostenmodell-Heuristik (spatial-first vs. eq-first).

M4 – Optional GPU/Advanced
- GpuBatchBackend-PoC (Batch Intersects/DWithin) mit robustem Fallback; H3-Pre-Filter (1–2 Res-Level) für Analytics; weitere Funktionen (Buffer/Area/Transform via PROJ).

Akzeptanzkriterien
- Korrektheit: Ergebnisse deckungsgleich mit Boost.Geometry-Referenztests.
- Performance: P95 ≥2× Speedup vs. Full-Scan bei typischen Viewports; Kandidatenfilter ≤5% der Tabelle.
- Portabilität: Windows/Linux; GPU-Backend optional deaktivierbar ohne Featureverlust.

Konkrete erste Tasks
- Add: `include/utils/geo/ewkb.h` + `src/utils/geo/ewkb.cpp` (Parser/Serializer, Z-Unterstützung).
- Add: `include/index/spatial_index.h` + `src/index/spatial_rtree.cpp`; Engine-API für `executeSpatial(...)`.
- Update: Parser/Translator um ST_* (MVP) + `spatialPredicate`.
- Update: HTTP-API `/index/create` (spatial) + Doku.
- Tests/Benches: Parser/Engine/HTTP + `benchmarks/bench_spatial_intersects.cpp`.
