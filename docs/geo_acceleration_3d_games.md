# 3D-/GameDev-Techniken für Geo-Beschleunigung

Ziel: Techniken aus der 3D-Games-/Echtzeit-Grafik nutzen, um Geo-Queries (ST_Intersects/Within/Contains, Distance/DWithin, Spatial Join, kNN) schneller und speichereffizienter auszuführen. Fokus auf CPU-First (portabel), optional GPU-Pfade. Lizenzkonform (permissiv), ohne harte Runtime-Abhängigkeit.

## Kurzüberblick (Mapping)
- Prädikate: ST_Intersects/Within/Contains → Kandidatenfilter + exakte Prüfung
- Distanz: ST_Distance/DWithin → MBR-Expand, SDF/ESDF (optional), vektorisierte Haversine
- JOIN/KNN: Block-/Tile-basierte Paare (Morton-Sortierung), BVH-Sweep & Prune (broadphase)
- 3D: ST_3DDistance, Z-Filter (z_min/z_max), Mesh-/Volumen-Operationen optional

## Beschleunigungsstrukturen
- BVH/HLBVH/LBVH (linear BVH via Morton-Codes)
  - Verwendung: Punkte/Segment-/Dreiecks-Primitive (3D), Polygone trianguliert
  - Build: LBVH (parallel sort by Morton → breadth-first node layout) für schnelle Builds; Persistenz: breitensuche-kontig in RocksDB/CF "spatial"
  - Einsatz: Intersects/Within/Contains/Distance exact-check; broadphase pruning ähnlich Raytracing
- Quadtree/Octree/SVO
  - Verwendung: 2D/3D Partitionierung; SVO für volumetrische Zonen (Geofencing/3D)
  - Speicher: brick-basierte Sparse-Strukturen (Clipmaps) für Multi-Resolution

## Speicher- & Datenlayout
- SoA/AoSoA
  - Koordinaten, BBox, Indizes in separaten Vektoren; AoSoA zur SIMD-Breite passend (z. B. 8/16)
- Space-Filling Curves
  - Morton/Z-Order (2D/3D inkl. Z) für Sortierung, Sharding, Range-Scans; verbessert Cache- und RocksDB-Locality
- Quantisierung & Packen
  - Tile-lokale Quantisierung (z. B. 16-bit), Delta-Encoding, Varint, Bitpacking; optional Draco (Apache-2.0) für Meshes

## SIMD-Kerne (CPU)
- Point-in-Polygon (PiP) branchlos (Ray-Cast/XOR) mit AVX2/AVX-512/NEON
- Vektorisierte Haversine/Geodätik, bbox-overlap, edge-intersection
- Bibliotheken: Google Highway (Apache-2.0) oder xsimd (BSD) – optional
- Mapping: schnellere exact-checks für ST_Intersects/Within/Contains; Distanz-basiert für DWithin

## GPU-Compute (optional)
- Compute-Shader-Pipeline (DX12/Vulkan/OpenCL/CUDA):
  - Eingaben SoA; BBox-first Test → Masken; Prefix-Sum/Stream-Compaction; Persistente Threads/Warp-Votes
  - LBVH-Build (Morton sort + node gen) für dynamische Punktwolken; Dispatch mit Multi-Draw-Indirect analog (CPU-Fallback zwingend)
- Use-Cases: Massives Spatial Join, Intersects auf großen Batches, Heatmap/Binning
- Risiken: Plattformabhängigkeit, Treiber; daher Feature-Flag + klare Fallbacks

## Set-Algebra & Culling
- Roaring Bitmaps (MIT): schnelle AND/OR/NOT auf PK-Sets; ideal für DNF/OR in AQL
- Hierarchische Min/Max-Maps (Hi-Z-analog): Mip-ähnliche Ebenen über Tiles/MBRs für frühes Pruning
- Signed Distance Fields (SDF/ESDF):
  - Vorberechnet für statische Regionen: O(1) Punkt-in-Region (Vorzeichenprüfung) und sehr schnelle DWithin-Tests
  - Hierarchische Mipchains; GPU-Build via Jump Flooding Algorithm (JFA) optional

## LOD/Streaming
- Geometry Clipmaps & Tiling (Zoom-Level): Multi-Resolution-Indexierung; Queries besuchen nur wenige relevante Levels
- 3D Tiles (OGC) als Import/Export (optional), intern weiterhin WKB/EWKB + Sidecar

## Integration in ThemisDB
- Index/Storage
  - RocksDB-Keyspace morton:{level}:{code} → kompakte PK-Listen; Statistik je Tile (density, hits/misses)
  - BVH-Struktur breitensuche-kontig für Cache-freundliche Traversal; optional pro-Partition
- Engine
  - Kandidatenphase: R-Tree oder Morton-Tiles → BVH/BB-Tests → exact-checks (SIMD)
  - Set-Algebra: Roaring Bitmaps für OR/AND; Bitset-basierte Merge-Pläne
  - Parallelität: Tile-/Bucket-Partitionierung; lock-arme Queues; Job-System (Task-Graph)
- Metriken
  - simd_hits, bvh_pruned, morton_tiles_visited, roaring_ops, gpu_batches (optional)

## Quick Wins (Reihenfolge)
1. Morton-Codes + Sortierung (CPU) für bessere Locality/Range-Scans
2. SIMD Point-in-Polygon + bbox-overlap Kernels
3. Roaring Bitmaps in Set-Algebra (AQL OR/AND)
4. LBVH-Prototyp für Punktwolken (CPU); Benchmark gg. R-Tree
5. Optional: GPU-Compute PoC (Batch ST_Intersects) mit zwingendem CPU-Fallback
6. Optional: SDF für statische Geofencing-Zonen (schnelle DWithin/Contains)

## Akzeptanzkriterien
- Korrektheit identisch zu Boost.Geometry/GEOS Referenztests
- Beschleunigung: ≥2× Speedup bei ST_Intersects (P95) auf 100k–1M Entities gg. R-Tree-only Baseline
- Speicher-Overhead: ≤20% zusätzlich für Sidecar/Index vs. Roh-WKB (ohne Kompression)
- Fallbacks ohne GPU jederzeit funktionsfähig

## Lizenzen & Abhängigkeiten
- Highway (Apache-2.0), xsimd (BSD), CRoaring (Apache-2.0/MIT), Draco (Apache-2.0)
- GPU: keine verpflichtende Runtime; optionales Backend via Feature-Flag; CI ohne GPU

## Nächste Schritte
- Design-Entscheidung: Highway vs. xsimd; CRoaring als optionales Paket über vcpkg
- PoC: Morton + SIMD-PiP in Engine; Benchmarks + Metriken
- Evaluierung: LBVH-Buildzeiten vs. Query-Gewinn; ggf. nur für Punktwolken aktivieren
- Optionale Pfade: GPU-Compute und SDF später, wenn CPU-Pfade saturiert sind
