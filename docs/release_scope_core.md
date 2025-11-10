# Release Scope – Core Datenbank & Höhere Funktionen

Status: Draft (03.11.2025)

Ziel: Klarer, testbarer Umfang für den Core-Release ohne Geo-Module. Alle Punkte sind DoD-kritisch (Definition of Done) und mit Metriken/Tests belegbar.

## 1) AQL v1.3 – Kernsprache
- FOR/FILTER/SORT/LIMIT/RETURN vollständig
- LET: einfache Ausdrücke, Sequenzabhängigkeit, Nutzung in FILTER/RETURN
- COLLECT: COUNT/SUM/AVG/MIN/MAX (MVP)
- OR/AND/NOT mit DNF/Index-Merge (inkl. FULLTEXT+AND)
- Fulltext in AQL: `FILTER FULLTEXT(doc.field, "…")` mit Limit
- Neu: `BM25(doc)` Funktion + `SORT BM25(doc) DESC` (Scores aus FULLTEXT-Pfad)
- Neu: `FULLTEXT_SCORE()` optional als Alias
- Tests: Parser/Translator/Engine (Integration) + Explain-Validierung

## 2) Search & Relevance
- BM25 HTTP: stabil, konfigurierbare Analyzer (DE/EN), Stopwords, Umlaut-Normalisierung
- AQL-Integration (BM25/SORT) wie oben
- Phrase-Query v1 (Substring-Check) – optional
- Doku: `docs/search/fulltext_api.md` konsistent mit `docs/aql_syntax.md`

## 3) Vector Index – Stabilität & Features
- Batch-Inserts, Delete-by-Filter, Reindex/Compaction
- Cursor/Pagination inkl. Scores (HTTP & AQL)
- Neu: DOT-Metric (reines Skalarprodukt, keine Normalisierung)
- Tests: Recall/Ranking-Invarianz pro Metrik, Regression-Tests

## 4) Time-Series (MVP)
- Gorilla-Codec integriert in TSStore (Write/Read)
- Aggregationen: min/max/avg über Zeitfenster (rollen wir minimal aus)
- Retention Policies (Basis) – Integration mit bereits vorhandener Config
- Tests: Kompressionsrate, Latenz, Integrität

## 5) Content/Filesystem
- Chunk-Upload stabil (Bulk), Dokument-/Chunk-Schema finalisiert
- Optional: ZSTD-Komprimierung (konfigurierbar; Metadaten kompatibel)
- Tests: Roundtrip, Ratio, Performance-Budget

## 6) Security (Basis)
- RBAC (Basis-Rollen/Scopes) – Enforcement im HTTP/Query-Layer
- Dynamic Data Masking (Feldkonfiguration) – Mask/Unmask im RETURN
- PKI RSA Sign/Verify (OpenSSL) – Austausch des Demo-Stubs; eIDAS-Hinweis in Doku
- Tests: AuthZ-Pfade, Masking-Korrektheit, Signatur-Validierung

## 7) Observability & Ops
- Bereits fertig: OTEL Tracing, strukturierte Logs, POST /config (Hot-Reload)
- Inkrementelle Backups – Basis
- Metriken erweitert: fulltext.score_hits, vector.dot_calls, ts.compress_ratio

## 8) Doku & OpenAPI
- OpenAPI synchronisiert (AQL/Fulltext/Vector/TS)
- AQL-Syntax vollständig (inkl. BM25 und FULLTEXT+AND)
- Migrationshinweise (Stemming-Index-Rebuild, ZSTD-Option)

## Abnahme-Kriterien (Green Gates)
- Build: PASS (Windows, vcpkg)
- Lint/Typecheck: PASS (C++/Warnings auf akzeptablem Niveau)
- Tests: ≥ 95% der Unit-/Integration-Tests grün; neue Tests für oben aufgeführte Features vorhanden
- Benchmarks (informativ):
  - BM25 100k Docs, 5-Token: < 50ms p50
  - Vector Top-10: < 20ms p50 (cosine), < 20ms p50 (dot)
  - TS Gorilla Write+Read: Ziel-Latenz dokumentiert; Ratio 10–20x

## Out of Scope (Post-Release)
- Geo: Storage/Index/AQL ST_* (siehe `docs/geo_execution_plan_over_blob.md`)
- GPU/SIMD-Beschleuniger für Geo/Vector
- H3/S2, Spatial Joins, Heatmaps
