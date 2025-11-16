```markdown
**Consolidated Development Overview**

Datum: 16. November 2025

Zweck: Zusammenführung der Verifikations‑ und Statusdokumente zu einem schnellen, durchsuchbaren Überblick für Entwickler und Planner. Diese Datei verlinkt zu tiefergehenden Dokumenten und listet priorisierte, umsetzbare Schritte.

1) Kurzübersicht (Status)
- **Implementiert & getestet:**
  - TSStore + Gorilla Codec (Time‑Series)
  - Semantic Cache (LLM)
  - VectorIndex / HNSW (optional)
  - FULLTEXT / BM25 / AQL
- **Teilweise / dokumentiert:**
  - Inkrementelle Backups / RocksDB Checkpoints (Konzept vorhanden, Archiver nicht verifiziert)
- **Dokumentiert, aber nicht implementiert:**
  - Changefeed / CDC (Writer + HTTP/SSE Endpoints)
  - FieldEncryption Batch API (`encryptEntityBatch`), PKIKeyProvider / eIDAS Signaturen

2) Fundorte (wichtige Dateien)
- `include/timeseries/tsstore.h`, `src/timeseries/tsstore.cpp`
- `include/timeseries/gorilla.h`, `src/timeseries/gorilla.cpp`
- `include/cache/semantic_cache.h`, `src/cache/semantic_cache.cpp`
- `include/index/vector_index.h`, `src/index/vector_index.cpp`
- `src/query/aql_translator.cpp`, `src/query/query_engine.cpp`, `src/server/http_server.cpp`
- `src/content/content_manager.cpp`, `src/utils/zstd_codec.cpp`
- `src/utils/hkdf_cache.cpp`, `include/utils/hkdf_cache.h`

3) Detaillierte Referenzen
- Detaillierte Verifikation nach Funktionsbereichen: `docs/development/overviews/verification_by_area.md`
- Content ZSTD + HKDF (Design, Tests, DoD): `docs/development/security/content_zstd_hkdf.md`
- Batch‑Encryption, Backups, Changefeed, PKI: `docs/development/overviews/feature_status_changefeed_encryption.md`
- OpenAPI‑Snippets (Changefeed): `docs/development/changefeed/changefeed_openapi.md`
- Changefeed Testspezifikation: `docs/development/changefeed/changefeed_tests.md`
- CMake Patch Vorlage: `docs/development/changefeed/changefeed_cmake_patch.md`

4) Empfehlungen & Prioritäten
- **Top (Kurzfristig):** Implementiere Changefeed (CDC) MVP
  - Begründung: hohe Integrations‑/Replications‑Relevanz; fehlt im Code, aber in vielen Docs erwähnt.
  - MVP DoD und Endpoints: siehe `docs/development/overviews/feature_status_changefeed_encryption.md` (GET/stream/stats/retention).
- **Mittelfristig:** FieldEncryption Batch API + PKIKeyProvider (eIDAS) — HKDF Cache ist bereits vorhanden und dient als Grundlage.
- **Nebenaufgabe:** Minimaler WAL‑Archiver MVP (Checkpoint/rotation + retention) falls Backup‑Policies gefordert sind.

5) Nächste Schritte (konkret)
- Konsolidierte Dokumentation ist erstellt (diese Datei + verlinkte Detailseiten).
- Optional: Code‑Implementierung starten für Changefeed (siehe TODO `Implement Changefeed MVP`).
- Optional: Testspezifikation und Benchmark‑Skript für `encryptEntityBatch` (wenn implementiert).

6) Ansprechpartner / Hinweise
- Bei Fragen zu internem Design: prüfe `docs/development/security/content_zstd_hkdf.md` und `docs/development/overviews/verification_by_area.md`.
- Für Compliance/PKI: frühzeitige Abstimmung mit Security/Legal empfohlen (eIDAS Anforderungen).

```
