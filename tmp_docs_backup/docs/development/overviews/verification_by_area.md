```markdown
# Verifikation: Funktionale Bereiche — Status & Fundorte

Datum: 16. November 2025

Kurzbeschreibung
- Dieses Dokument fasst pro Funktionsbereich zusammen, was ich im Quellbaum gefunden habe: Implementierungsstatus, zentrale Source‑Dateien/Tests, Empfehlungen für die nächsten Schritte, geschätzter Aufwand und klare DoD‑Punkte.

1) Search & Relevance
- Status: Implementiert (vollständig für v1)
- Wichtige Dateien:
  - `src/query/aql_translator.cpp` (FULLTEXT parsing)
  - `src/query/query_engine.cpp` (BM25 integration)
  - `src/server/http_server.cpp` (Score handling)
  - Search docs: `docs/search/*`, `docs/aql_syntax.md`
- Tests: mehrere unit/integration tests (referenziert in docs/tests)
- Nächste Schritte: Highlighting, Phrase‑Positionen, Learned Fusion (optional)
- DoD: End-to-end FULLTEXT→BM25 AQL queries + API examples + tests
- Aufwand (schätz.): Highlighting 1–2 Tage, Phrase search 2–3 Tage

2) Vector Index (ANN / HNSW)
- Status: Implementiert (HNSW optional)
- Wichtige Dateien:
  - `include/index/vector_index.h`
  - `src/index/vector_index.cpp`
  - `wiki_out/vector_ops.md` (API usage)
- Tests: `tests/test_vector_index.cpp` referenced
- Nächste Schritte: Batch‑Inserts, Reindex/Compaction, Score‑Pagination
- DoD: Batch insert API + persistence roundtrip + simple pagination tests
- Aufwand (schätz.): 1–3 Tage

3) Content / Filesystem (Blobs, Chunking)
- Status: Implementiert (Core import/store + ZSTD precompress)
- Wichtige Dateien:
  - `include/content/content_manager.h`
  - `src/content/content_manager.cpp` (importContent, blob storage, zstd logic)
  - `src/utils/zstd_codec.cpp`, `include/utils/zstd_codec.h`
- Tests: ContentManager examples present in wiki; unit tests may exist in `tests/` (spot check recommended)
- Nächste Schritte: Bulk chunk upload, extraction pipeline automation, more integration tests
- DoD: Bulk ingest test, streaming upload support, metrics for compression
- Aufwand (schätz.): 1–2 Tage

4) Time‑Series (TSStore) & Gorilla Codec
- Status: Implementiert
- Wichtige Dateien:
  - `include/timeseries/tsstore.h`
  - `src/timeseries/tsstore.cpp` (put/query/aggregate, gorilla chunking)
  - `include/timeseries/gorilla.h`, `src/timeseries/gorilla.cpp`
  - Tests: `tests/test_tsstore.cpp`, `tests/test_gorilla.cpp`
- Nächste Schritte: Continuous aggregates, automatic retention jobs
- DoD: Retention job + unit tests + metrics
- Aufwand (schätz.): 2–3 Tage (for MVP retention job)

5) Semantic Cache (LLM interaction cache)
- Status: Implementiert
- Wichtige Dateien:
  - `include/cache/semantic_cache.h`
  - `src/cache/semantic_cache.cpp`
- Tests: references in wiki; run specific tests to validate
- Nächste Schritte: similarity based retrieval (optional), CF tuning
- DoD: Read/Write TTL + stats tested
- Aufwand (schätz.): 0.5–1 Tag for tuning

6) Encryption / Key Derivation (HKDF)
- Status: HKDF cache implemented; FieldEncryption batch API partially missing
- Wichtige Dateien:
  - `include/utils/hkdf_cache.h`
  - `src/utils/hkdf_cache.cpp`
- Fehlende/teilweise Implementierungen:
  - `FieldEncryption::encryptEntityBatch` not clearly present
  - PKIKeyProvider / eIDAS signing wrappers not found (only docs/wiki)
- Nächste Schritte: implement batch encrypt API, add PKIKeyProvider (or Wire Vault), add OpenSSL wrappers for sign/verify
- DoD: Single base key fetch per batch, per-entity HKDF derive, AES‑GCM encrypt/decrypt verified, unit tests
- Aufwand (schätz.): 2–5 Tage depending on PKI/eIDAS scope

7) CDC / Changefeed / Audit Trail
- Status: Dokumentiert, Implementierung nicht gefunden
- Docs: many references in `docs/development/todo.md`, `wiki_out/*` and OpenAPI
- Erwartete Komponenten (MVP):
  - Changefeed writer (rocksdb CF `changefeed` or `changefeed:<seq>` keys)
  - Read API: `GET /changefeed`, `GET /changefeed/stream` (SSE), `GET /changefeed/stats`, `POST /changefeed/retention`
  - Retention job / admin endpoint
- Nächste Schritte (Empfohlen): Implement CDC MVP (writer hooks, simple append, reader + SSE)
- DoD: Append-only events stored, reader returns events from seq, SSE with heartbeats, retention endpoint
- Aufwand (schätz.): 1–3 Tage (MVP)

8) Observability / Ops / Backups
- Status: Core metrics & tracing implemented per docs; incremental backups partially TODO
- Wichtige Orte: tracing/wikI, `build` scripts, `docs/*`
- Nächste Schritte: implement incremental WAL archiving if required, add runbooks
- DoD: backup/restore test, retention + compaction metrics
- Aufwand (schätz.): 1–3 Tage

9) Graph & AQL Extensions
- Status: Graph traversal, temporal variants implemented; AQL features largely present
- Wichtige Dateien:
  - `include/index/graph_index.h`, `src/index/graph_index.cpp` (exists in repo per docs)
  - AQL translator and query_engine in `src/query/`
- Nächste Schritte: path constraints, shortestPath(), further optimizer improvements
- DoD: API tests + perf checks
- Aufwand (schätz.): 2–5 Tage

10) Compliance / PKI / eIDAS
- Status: Design & docs present; production eIDAS implementation not found
- Empfehlung: Treat as separate project slice (legal + crypto review). Implement PKIKeyProvider and OpenSSL eIDAS flows only after approval.
- DoD: OpenSSL sign/verify, test vector, optional HSM/PKCS#11 adapter
- Aufwand (schätz.): 3–7 Tage (depending on HSM)

Appendix — Hinweise zur Verifikation
- Vorgehen: Für jeden Bereich wurden die relevanten Schlüsselwörter, Header‑/Source‑Dateien und Tests per Repo‑Suche überprüft.
- Anmerkung: Einige Features waren nur in `wiki_out/` oder `site/` (staging docs) dokumentiert — diese wurden als "dokumentiert" gezählt; echte Implementierung wurde durch das Vorhandensein von `src/`/`include/` Dateien bewertet.

Empfohlener nächster Schritt
- Implementiere CDC/Changefeed (MVP) — hoher Integrationswert und aktuell in Docs aber nicht in Code. Nach Abschluss: FieldEncryption batch API + PKIKeyProvider für Compliance.

```
