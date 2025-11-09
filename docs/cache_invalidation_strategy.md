# Cache-Invalidierungsstrategie

Ziel: Korrekte Freshness trotz Replikation/Rebalancing.

## Events
- WAL/Changefeed: PUT/DELETE → Entity-Invalidate(URN), ResultCache.InvalidatePlan(plan_hash betroffener Abfragen)
- Topology-Änderung: `cache_epoch` bump; veraltete Einträge validieren

## Versionierung
- Jede Entity erhält `version` (WAL-Index). Cache speichert `version` und akzeptiert Hits nur, wenn `cached.version >= applied_version` auf Replica oder Lag < Schwellwert.

## Negative Caching
- 404-Ergebnisse mit kurzer TTL (1–5s) zur Entlastung von Hot-Misses

## Replikationsbewusstsein
- Leader invalidiert authoritative; Replikas respektieren Lag-Grenzen

## Batch-Invalidierung
- Präfix-Invalidierung optional für Collections (z.B. Rebuilds)

## Sicherheit
- Namespace im Key → Tenant-Isolation
