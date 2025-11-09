# Lookup-Pattern Katalog – Caching & Parallelisierung

Dieser Katalog beschreibt Standard-Pfade und zugehörige Caching-Hooks.

## 1) GET by PK (URN)
- Router: SINGLE_SHARD
- Caching: L1.Get(URN) → Coalescer → Remote GET → L1/L2.Put
- Invalidierung: Changefeed Event: Invalidate(URN)
- Edge-Cases: Negative Caching (404, kurze TTL), Replication Lag Guard (ETag/Version)

## 2) BATCH GET (URN[])
- Router: Gruppierung nach Shard, parallele Multi-GETs
- Caching: Pro Key L1-Hit prüfen; Misses bündeln; Ergebnis zusammenführen
- API: `batch_get(model, collection, uuids[])`

## 3) RANGE SCAN (Index)
- Router: Shard-lokale Range-Queries
- Caching: Ergebnis-Seiten im ResultCache (plan_hash+page)
- Batching: Prefetch Next-Page, Read-Ahead

## 4) VECTOR SEARCH
- Router: SCATTER_GATHER (alle Shards)
- Caching: Top-K Ergebnis-Seiten (kurze TTL), optional Query-Normalisierung
- Optimierung: Per-Shard Top-K → Merge + Re-Rank

## 5) GRAPH TRAVERSE
- Router: NAMESPACE_LOCAL / CROSS_SHARD je nach Kantenlage
- Caching: Knoten-Adjazenz-Listen kurzzeitig cachen
- Sicherheitsgurt: Max-Depth, Backpressure

## Parallelisierung
- Futures/Thread-Pool im Router; Request Coalescing; Scatter-Gather mit Fan-out/Fan-in

## Metriken
- pro Pattern: Latenz (p50/p95/p99), Backend QPS, Cache Hit/Miss
