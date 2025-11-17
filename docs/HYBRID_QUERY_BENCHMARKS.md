# Hybrid Query Optimizations Benchmarks (Phase 1.5+)

Dieser Bericht dokumentiert die Messmethodik und Zielmetriken für die neuen Optimierungen:
1. Parallel Filtering (TBB + multiGet)
2. SIMD L2 Distance (AVX2/AVX512 Fallback Scalar)
3. Geo-aware Cost Optimizer (Vector-first vs. Spatial-first Plan)

## 1. Methodik

### 1.1 Microbenchmarks
- Datei: `benchmarks/bench_simd_distance.cpp`
  - Vergleicht `simd::l2_distance()` gegen eine reine Scalar-Implementierung für Dimensionen {64,128,256,512}.
  - Metrik: Zeit pro Distanzberechnung (µs).

- Datei: `benchmarks/bench_hybrid_vector_geo.cpp`
  - Misst ANN-Suchkosten für Hybrid Vector+Geo (vereinfachter Pfad; SpatialIndex hier nicht integriert um Setup klein zu halten).
  - Metrik: Zeit für Top-k Suche (ms) bei N=5000, k=10.

### 1.2 Geplante Integrationstests
- Datei: `tests/test_hybrid_optimizations.cpp` (Grundgerüst)
  - Testfälle:
    - Vector-first Plan (erzwungen via `bbox_ratio_threshold = 0.0`).
    - Brute-Force Pfad ohne Vektorindex (SIMD Distanz, Reihenfolge-Semantik).
  - Finalisierung nach Einbindung eines SecondaryIndexManager Test-Fixtures.

### 1.3 Vergleichs-Szenarien (Zielwerte)
| Szenario | Basis (vor Optimierung) | Ziel (nach Optimierung) | Erwartete Speedup |
|----------|-------------------------|--------------------------|-------------------|
| Brute-Force L2 (512 dim) | ~X µs | ~X/1.4 µs | 1.3–1.5× |
| Vector-first Hybrid k=10 (N=5k) | ~Y ms (Spatial-first) | ~Y/1.2 ms | 1.1–1.3× |
| Content+Geo 1000 FT Hits | ~Z ms | ~Z/2 ms | 1.8–2.2× |
| Graph+Geo BFS depth 5 (500 nodes) | ~A ms | ~A/1.5 ms | 1.4–1.6× |

Hinweis: Platzhalter (X,Y,Z,A) werden nach erstem Benchmark-Lauf ersetzt.

## 2. Ausführung

### 2.1 Build (mit AVX2 unter MSVC)
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release -DTHEMIS_ENABLE_AVX2=ON
cmake --build build --config Release --target themis_benchmarks
```

### 2.2 Benchmarks starten
```powershell
.\buildench_simd_distance.exe --benchmark_color=yes
.\build	hemis_benchmarks.exe --benchmark_filter=Hybrid_VectorFirst
```

### 2.3 Integrationstest-Lauf (nach Fertigstellung der Assertions)
```powershell
cmake --build build --config Debug --target themis_tests
.uild	hemis_tests.exe --gtest_filter=HybridOptimizations.*
```

## 3. Ergebnis-Erfassung
Nach dem Lauf die Rohdaten (JSON möglich via `--benchmark_format=json`) sichern:
```powershell
.uildench_simd_distance.exe --benchmark_format=json > simd_distance_results.json
```

Kennzahlen werden in diesen Bericht aufgenommen und im `DATABASE_CAPABILITIES_ROADMAP.md` konsolidiert.

## 4. Tuning-Parameter
`config:hybrid_query` erlaubt Feintuning:
```json
{
  "vector_first_overfetch": 6,
  "bbox_ratio_threshold": 0.25,
  "min_chunk_spatial_eval": 64,
  "min_chunk_vector_bf": 128
}
```

## 5. Nächste Schritte
- Assertions in `test_hybrid_optimizations.cpp` vervollständigen (Plan-Erkennung, Distanz-Sortierung).
- Erweiterung des Hybrid-Benchmarks um SpatialIndex Pfad (R-Tree).
- Dokumentation realer Messwerte und Vergleich mit Ziel-Schwellen.

## 6. Risiken & Validierung
- SIMD Pfad fällt bei fehlender AVX2/AVX512 CPU transparent auf Scalar zurück (Performance-Minderung, keine Korrektheitsrisiken).
- Parallelisierung nutzt Chunking, testweise obere Schranke für Speicherverbrauch kontrollieren.
- Overfetch-Faktor > 10 kann Speicher-/CPU-Druck erzeugen; Standard 5 ist konservativ.

---
Letzte Aktualisierung: 17.11.2025
