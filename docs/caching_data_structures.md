# Caching – Datenstrukturen (Skizze)

Dieser Entwurf konkretisiert die in `docs/infrastructure_roadmap.md` (Abschnitt 2.5) beschriebenen Strukturen.

## L1 In-Process Cache
- Klasse: `themis::cache::L1TinyLFUCache` (Header vorhanden; aktuell LRU-Platzhalter, TinyLFU folgt)
- Interface: `themis::cache::CacheProvider`
- Keys: URN (Entity), "plan_hash|ns|scope|page" (Result-Seiten)
- Werte: `CacheValue { payload, version, ts_ms }`

## L2 Shard-Lokaler Cache
- Backend: RocksDB Secondary CF oder Shared Memory Cache (später)
- Verantwortlich für Prozess-übergreifende Hits und Reboots
- Invalidation über Changefeed/WAL (siehe `docs/cache_invalidation_strategy.md`)

## Request Coalescing
- Klasse: `themis::cache::RequestCoalescer` (API-Skizze, Singleflight-Pattern)
- Ziel: Duplicate concurrent GETs bündeln

## Result Cache (AQL)
- Interface: `themis::cache::ResultCache` (Key enthält plan_hash, namespace, scope, page)
- Speichert seitenweise Ergebnisse mit kurzer TTL

## Shard Directory Cache
- (Design) Etcd-Watch → lokaler Map-Cache (URN→ShardID) + Epoch-Bump bei Topology-Änderung

## Nächste Schritte
- TinyLFU-Admittance + Zählstruktur (Count-Min/CM-Sketch)
- L2-Adapter-Interface
- Messpunkte: Hit/Miss, Evictions, Coalesce-Wartezeit
