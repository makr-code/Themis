```markdown
# tsstore.cpp

Path: `src/timeseries/tsstore.cpp`

**Purpose:** Implementiert `TSStore` für Zeitreihen‑Ingest, Chunking, Abfragen und Aggregationen. Nutzt den Gorilla‑Codec zur effizienten Kompression von Zeitstempeln und numerischen Werten.

**Kernbestandteile:**
- `TSStore` — Verwaltung von Chunks, Put/Get/Query Operationen, Retention und Continuous Aggregates.
- `GorillaEncoder` / `GorillaDecoder` — Kompressionslogik für Zeit/Value Serien (Delta/Coding für hohe Kompressionsraten).

**Wichtige API‑Operationen (Übersicht):**
- `putPoints(series_id, points, options)` — schreibt Datenpunkte, erzeugt/füllt Chunks.
- `queryRange(series_id, options)` — Bereichsabfrage (from/to, limit, aggregation).
- `createContinuousAggregate(spec)` — konfiguriert Hintergrundaggregation (rollups).
- `retentionSweep()` — entfernt alte Chunks entsprechend Retention‑Regeln.
- `getChunk(chunk_id)` — liefert Metadaten + (dekomprimierten) Payload via `GorillaDecoder`.

**Chunk‑Format (konzeptionell):**
- Chunk‑Metadaten (time range, count, min/max, aggregate summaries) als JSON unter einem Key namespace.
- Payload: Gorilla‑komprimierter Binärblob mit Zeitstempeln und Werten.

**Design‑/Betriebshinweise:**
- Empfohlene Chunk‑Größe: Balance zwischen Kompressionseffizienz und Query‑Latency; typische Empfehlung: einige KB bis wenige MB (abhängig von Datenrate).
- Kompressions‑Tradeoff: kleinerer Chunk → schnellere Seek/Query; größerer Chunk → bessere Kompression.
- Retention & compaction sollten als Hintergrundjobs geplant werden (staggered windows).

**Beispiel (Pseudocode):**
```cpp
TSStore ts(db, config);
std::vector<Point> pts = { {ts_ms1, value1}, {ts_ms2, value2} };
ts.putPoints("series/orders", pts, PutOptions{});
auto r = ts.queryRange("series/orders", QueryOptions{.from = t0, .to = t1, .limit = 100});
for (auto &p : r.points) { /* use p.timestamp, p.value */ }
```

**Tests / Benchmarks (empfohlen):**
- Ingest‑Throughput: points/sec bei verschiedenen Batch‑Größen.
- Query‑Latency: range queries über kleine vs große Zeitbereiche.
- Compression Ratio: Gorilla vs raw für typische telemetry datasets.

**Ergänzungen / TODOs:**
- Referenztests: `tests/test_tsstore.cpp` verlinken und erweitern.
- Operationale Anleitung: empfohlene Chunk‑größen, Hintergrundjob Intervalle, memory/IO tuning.

```
