# RocksDB Storage – Layout & Betrieb

Dieser Leitfaden beschreibt das physische Storage-Verhalten der ThemisDB-Engine auf Basis von RocksDB: Schlüsselpräfixe, WAL, Snapshots und Compaction.

## Schlüsselräume & Präfixe

Themis nutzt ein Präfix-Schema zur logischen Trennung von Datenbereichen:

- Entities (Primärdaten): `entity:<table>:<pk>` → Blob (BaseEntity-Serialisierung)
- Secondary Index: `idx:<table>:<column>:<value>:<pk>`
- Range Index: `ridx:<table>:<column>:<value>:<pk>`
- Sparse/TTL/Fulltext: `sidx:`/`ttlidx:`/`ftidx:` entsprechend der Funktion
- Graph Adjazenz: `graph:out:<from_pk>:<edge_id>` → `<to_pk>`, `graph:in:<to_pk>:<edge_id>` → `<from_pk>`
- Vector Index (Metadaten/Mapping): `vector:<table>:<pk>` → Vektorinfo (Embedding out-of-store in Index-Struktur)
- Changefeed (Audit): `changefeed:<sequence>` → Event JSON
- Time-Series (TSStore): `ts:<metric>:<timestamp>:<tags>` → Wert(e)

Siehe auch: `docs/indexes.md`, `docs/temporal_time_range_queries.md`, `docs/change_data_capture.md`, `docs/time_series.md`.

## Column Families (CF)

- Standardbetrieb: Default Column Family (CF)
- Optional (für große Workloads): Trennung in CFs (z. B. `cf_entities`, `cf_indexes`, `cf_graph`, `cf_changefeed`, `cf_ts`) kann LSM-Compactions separieren.
- Hinweis: Aktuell verwendet Themis standardmäßig die Default CF; CF-Trennung ist als Betriebsoptimierung möglich und sollte konsistent in Engine-Config & Backups berücksichtigt werden.

## Write-Ahead Log (WAL)

- WAL stellt Durability sicher und dient für Recovery nach Abstürzen.
- Empfohlen:
  - `wal_bytes_per_sync` und `bytes_per_sync` passend zur Hardware
  - `max_total_wal_size` dimensionieren (Spitzen abfangen, aber Platte nicht vollschreiben)
  - Sync-Strategie nach Latenzanforderungen (`disableWAL=false`, fsync je nach Durability-Ziel)

## Snapshots & MVCC

- Snapshots fixieren ein Sichtfenster für Reads (Snapshot-Isolation)
- Transaktionen verwenden Snapshots, um Repeatable Reads zu ermöglichen
- Long-running Snapshots erhöhen Read Amplification: Überwachung und Begrenzung empfehlenswert

## Compaction & Performance

- Trennung heißer/cold Daten (optional via CF) kann Write Amplification reduzieren
- Kompressions-Strategie (z. B. LZ4 für L0/L1, ZSTD für tiefere Ebenen) je nach Profil
- Bloom Filter für Punktabfragen (Secondary Index) aktivieren
- Prefix-Extractor gemäß Key-Schema (z. B. bis zum `:value:`-Teil bei `idx:`) beschleunigt Prefix-Scans

## Backups & Restore

- RocksDB Backups (SST + MANIFEST + OPTIONS + ggf. WAL) regelmäßig erstellen
- Konsistenz über alle CFs sicherstellen (falls eingesetzt)
- Vor Restore: Version/Options-Kompatibilität prüfen

## Monitoring

- Wichtige Kennzahlen: L0 File Count, Compaction Pending, Stall Time, WAL Größe, Read-/Write-Amp
- Prometheus-Export aus Themis (`/metrics`) ergänzen um Storage-Kennzahlen (Roadmap)

## Troubleshooting (Kurz)

- Hohe Latenzen bei Scans: Prefix-Extractor/Bloom prüfen; CF-Trennung für Indizes erwägen
- Speicherverbrauch stark: Kompression/Block-Cache-Tuning; Retention (Changefeed/TS) aktivieren
- Lange Snapshots: Transaktionslaufzeiten begrenzen; `cleanupOldTransactions` nutzen

Weiterlesen:
- `docs/mvcc_design.md`
- `docs/transactions.md`
- RocksDB Tuning Guides (Block Cache, MemTables, Compaction)
