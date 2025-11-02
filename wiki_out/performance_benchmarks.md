# Performance & Benchmarks

Dieser Leitfaden konsolidiert die wichtigsten Performance-Themen und Microbenchmarks in ThemisDB: Kompression, Pagination, MVCC vs. WriteBatch, Index-Rebuilds und Vector-Suche. Er beschreibt Messmethodik, Interpretation und konkrete Tuning-Empfehlungen.

## Ziele und Erfolgsmetriken

- Schreibrate und -latenz unter realistischen Index-Setups
- Leselatenzen für typische Abfragepfade (Equal/Range/Cursor)
- Speicherbedarf und Write Amplification unter verschiedenen Kompressionsmodi
- Rebuild-/Reindex-Durchsatz und Fortschrittsmetriken
- Vector-Suche: Latenz vs. Genauigkeit (HNSW efSearch)

## Methodik

- Alle Microbenchmarks basieren auf Google Benchmark und laufen isoliert mit reproduzierbaren Seeds.
- Ergebnisse hängen stark von Hardware, OS, Compiler und Cache-Zustand ab; mehrere Läufe und Mittelwerte bilden.
- Metriken über Prometheus (/metrics) und RocksDB Properties ergänzen (z. B. SST-Größen, Compactions).

## Benchmarks ausführen (optional)

Hinweis: Beispiel für Windows PowerShell, Release-Build und aktivierte Benchmarks.

```powershell
# Im Build-Ordner (falls nicht vorhanden, erzeugen)
cmake -S .. -B . -DCMAKE_BUILD_TYPE=Release -DTHEMIS_BUILD_BENCHMARKS=ON
cmake --build . --config Release --parallel

# Alle Benchmarks
.\Release\themis_benchmarks.exe --benchmark_repetitions=3

# Nur Pagination
.\Release\themis_benchmarks.exe --benchmark_filter=BM_Pagination_.*

# Nur CRUD/MVCC
.\Release\themis_benchmarks.exe --benchmark_filter=CRUDFixture|MVCCFixture
```

Siehe auch: spezifische Seiten unten für Filter und Setups.

## Kompression: none vs lz4 vs zstd

Quelle: `benchmarks/bench_compression.cpp`, Dokumentation: [Kompressionsvalidierung & Benchmarks](compression_benchmarks.md)

Kernaussagen (aus Messungen):
- Kleine Entities (≤ 1 KB): lz4/zstd oft schneller als none (I/O-Reduktion > CPU-Overhead)
- Mittlere Größen (~4 KB): ähnlich; zstd minimal besser bei hoher Kompressibilität
- Große Blobs (≥ 16 KB): none schneller (CPU-Kosten der Kompression dominieren)
- Reads (warm cache): none am schnellsten; in I/O-limitierten Szenarien kann Kompression dennoch helfen

Empfohlene Hybrid-Konfiguration:

```json
"compression": {
  "default": "lz4",
  "bottommost": "zstd"
}
```

Write Amplification einschätzen und messen:
- SST-Größe reduziert Kompaktionsarbeit → geringere Write Amplification bei kompressiblen JSON
- Exakt messen über RocksDB `GetProperty("rocksdb.total-sst-files-size")` vor/nach Workloads

Weitere Details und Tabellen: siehe [compression_benchmarks.md](compression_benchmarks.md)

## Pagination: Offset vs Cursor (Anchor)

Quelle: `benchmarks/bench_query.cpp`, Dokumentation: [Pagination Benchmarks](search/pagination_benchmarks.md)

- Offset: Aufwand wächst linear mit dem Offset (Index traversiert alle Einträge bis zur Seite)
- Cursor/Anchor: konstante Arbeit pro Seite via start-after `(cursor_value, cursor_pk)` und `LIMIT count+1`
- Praxisempfehlung: Cursor-Pagination für große Datenmengen; siehe [Cursor/Pagination](cursor_pagination.md) für API/Beispiele

Optional reproduzieren:

```powershell
.\Release\themis_benchmarks.exe --benchmark_filter=BM_Pagination_.*
```

## MVCC vs WriteBatch und CRUD-Durchsatz

Quelle: `benchmarks/bench_mvcc.cpp`, `benchmarks/bench_crud.cpp`

- MVCC (Transaction) bietet Snapshot-Isolation und komfortable Rollbacks; leichter Overhead vs. WriteBatch
- WriteBatch ist minimal schneller bei Bulk-Inserts, aber ohne Isolation/Locks
- Indexschwere Workloads (mehrere Sekundärindizes) skalieren besser mit Batching (100+ pro Commit)

Empfehlungen:
- Einzel- und kleine Writes: MVCC für Korrektheit, besonders bei parallelen Reads
- Bulk-Import: WriteBatch nutzen, WAL optional deaktivieren, danach `flush()`
- Allgemein: Batches von 100–1000 Entities für Throughput optimieren

## Index-Rebuilds und Reindex

Quelle: `benchmarks/bench_index_rebuild.cpp`, Dokumentation: [Index-Statistiken & Wartung](index_stats_maintenance.md)

- Rebuild pro Index-Typ (Regular/Composite/Range/Sparse/Geo/TTL/Fulltext) separat messbar
- Gesamt-Reindex pro Tabelle berücksichtigt alle Indizes; IO- und CPU-limitierte Phasen möglich
- Fortschritt über Prometheus-Metriken und interne Counters beobachten

Wichtige Metriken (Auswahl):
- `themis_index_rebuild_count`, `themis_index_rebuild_duration_ms_total`
- `themis_index_rebuild_entities_processed_total`
- `themis_index_cursor_anchor_hits_total`, `themis_index_range_scan_steps_total`

## Vector-Suche (HNSW) Tuning

Voraussetzung: Build mit HNSW (`THEMIS_HNSW_ENABLED`). Konfiguration siehe Deployment:
- `engine`: "hnsw"
- `hnsw_m`: Nachbarschaftsgrad (Speicher/Genauigkeit)
- `hnsw_ef_construction`: Aufbau-Qualität (Indexierzeit/Genauigkeit)
- Laufzeit-Tuning: `setEfSearch(ef)` steigert Recall mit mehr Sucharbeit (höhere Latenz)

Empfehlungen:
- Startwerte: `m=16`, `ef_construction=200`, `efSearch=32–128` je nach k und Datenbankgröße
- Persistenz nutzen (`saveIndex`/`loadIndex`) für schnellere Warmstarts
- Bei reiner CPU-Suche: Vektoren normalisieren, kleineren Dimensionalitätsraum bevorzugen

Benchmarks (implementiert in `benchmarks/bench_vector_search.cpp`):
- BM_VectorSearch_efSearch(ef,k): Sweep über `efSearch` für k-NN (Latenz vs. Suchaufwand)
- BM_VectorInsert_Batch100(dim): Insert-Durchsatz in 100er Batches

Optional ausführen (PowerShell):

```powershell
.\Release\themis_benchmarks.exe --benchmark_filter=BM_Vector(Search|Insert)_.*
```

## Best Practices und Tuning-Checkliste

- Batching: Schreib- und Indexoperationen in Batches (100–1000) bündeln
- Cursor-Pagination statt Offset für große Offsets einsetzen
- Kompression hybrid (lz4 + zstd bottommost); große Binärblobs ggf. ohne Kompression
- RocksDB-Tuning: Memtable/Block-Cache passend zur Workload, Hintergrundjobs ausreichend hoch
- Kalte vs. warme Messungen getrennt betrachten; OS-Cache explizit berücksichtigen
- Rebuilds in Wartungsfenstern; Fortschritt/Metriken überwachen
- Vector-Suche: `efSearch` dynamisch an SLOs anpassen (Latenz/Recall)

## Referenzen

- [compression_benchmarks.md](compression_benchmarks.md)
- [search/pagination_benchmarks.md](search/pagination_benchmarks.md)
- [indexes.md](indexes.md)
- [index_stats_maintenance.md](index_stats_maintenance.md)
- [memory_tuning.md](memory_tuning.md)
