# ThemisDB Dokumentation# ThemisDB Dokumentation



Willkommen bei ThemisDB – einer Multi-Modell-Datenbank mit Unterstützung für Relational, Graph, Vector, Document, Time-Series und Content-Management.Willkommen bei ThemisDB. Diese Dokumentation beschreibt Architektur, Datenmodell, Storage & MVCC, Query/AQL, Indexe, Content-Pipeline, Vektor-/Zeitreihenfunktionen, Sicherheit/Governance, APIs, Admin-Tools sowie Betrieb & Performance.



> **Status:** 09. November 2025  > Hinweis: Für Weitergabe/Archivierung

> **Version:** Core Release (MVP Features implementiert)  >

> **Archiv:** Veraltete Dokumente befinden sich unter `archive/` mit klaren Ersatz-Hinweisen.> - Gesamte Dokumentation (Druckansicht): [print_page/](print_page/)

> - Gesamt-PDF (CI generiert): Wird künftig als Build-Artefakt bereitgestellt

---

## Für wen ist das gedacht?

## Für wen ist das gedacht?- Benutzerinnen/Benutzer: Wie verwende ich AQL und die Server-APIs?

- Operator/DevOps: Deployment, Betrieb, Observability, Backup/Restore

- **Benutzer/innen:** AQL-Queries, REST-APIs, Vector Search, Full-Text, Graph Traversal- Entwicklerinnen/Entwickler: Architektur, interne Module, Erweiterungspunkte

- **Operator/DevOps:** Deployment, Betrieb, Monitoring, Backup/Restore, Security

- **Entwickler/innen:** Architektur, Module, Erweiterungspunkte, Build-System## Schnellstart

- Architekturüberblick: siehe „Architektur“

---- AQL Einstieg: siehe „Query & AQL → AQL Syntax“

- REST-APIs: siehe „APIs → OpenAPI & Endpunkte“

## Schnellstart

## Aktuelle Schwerpunkte

| Thema | Dokument | Beschreibung |- TSStore und Aggregationen (Stabilisierung)

|-------|----------|--------------|- Tracing/Observability

| **Überblick** | `architecture.md` | System-Architektur, Storage, MVCC, Indizes |- API-Hardening (Keys, Classification, Reports)

| **Query-Sprache** | `aql_syntax.md` | AQL Syntax, Joins, Aggregationen, FULLTEXT, BM25 |

| **REST-APIs** | `apis/openapi.md`, `openapi.yaml` | HTTP-Endpunkte, Schemas, Beispiele |## Nächste Schritte

| **Vector Search** | `vector_ops.md` | Batch Insert, KNN, Metriken (COSINE/L2/DOT) |- OpenAPI um Keys/Classification/Reports aktualisieren

| **Full-Text** | `search/fulltext_api.md` | BM25, Stemming, Umlaut-Normalisierung |- Konsolidierung Storage & MVCC-Dokumente

| **Graph** | `recursive_path_queries.md` | BFS/Dijkstra, Edge-Type Filtering, Temporal Stats |- Styleguide & Glossar finalisieren

| **Time-Series** | `time_series.md` | Gorilla Compression, Continuous Aggregates, Retention |
| **Security** | `pki_integration_architecture.md`, `encryption_strategy.md` | PKI, Field Encryption, JWT, RBAC |
| **Observability** | `tracing.md`, `operations_runbook.md` | OpenTelemetry, Prometheus Metrics, Logs |
| **Admin Tools** | `admin_tools_user_guide.md` | WPF Tools für Audit, Keys, PII, Classification |

---

## Kernfunktionen (Implementiert ✅)

### 1. Query Engine & AQL
- **FOR/FILTER/SORT/LIMIT/RETURN** – Vollständig
- **Joins** – Equality Join (Nested-Loop + Hash-Join Optimierung)
- **LET** – Variable Binding für Subqueries
- **COLLECT** – Aggregationen (COUNT, SUM, AVG, MIN, MAX)
- **FULLTEXT** – BM25 Scoring, Stemming (DE/EN), Umlaut-Normalisierung
- **BM25()** – Score-Funktion in AQL (`SORT BM25(doc) DESC`)
- **OR/AND/NOT** – DNF-Optimierung mit Index-Merge

**Docs:** `aql_syntax.md`, `query_engine_aql.md`, `aql_explain_profile.md`

### 2. Vector Search
- **HNSW Index** – Persistierung, Konfigurierbare Parameter (M, efSearch)
- **Metriken** – COSINE (normalisiert), L2 (Euklidisch), DOT (Skalarprodukt)
- **Batch Insert** – `/vector/batch_insert` für >1000 Items
- **Delete by Filter** – Präfix-basierte Bereinigung
- **Cursor Pagination** – Score-basierte Paginierung

**Docs:** `vector_ops.md`, `hnsw_persistence.md`

### 3. Full-Text Search
- **BM25** – Okapi BM25 Ranking
- **Analyzer** – Tokenization, Stopwords, Stemming (Porter DE/EN)
- **Umlaut-Normalisierung** – ä→a, ö→o, ü→u, ß→ss
- **AQL Integration** – `FILTER FULLTEXT(doc.field, "query")`

**Docs:** `search/fulltext_api.md`, `search/stemming.md`

### 4. Graph Database
- **BFS/Dijkstra** – Shortest Path, Multi-Hop Traversal
- **Edge Type Filtering** – Server-side `edge_type` Parameter
- **Temporal Graphs** – Time-Range Queries, Aggregationen (AVG, SUM, MIN, MAX)
- **Recursive Path Queries** – AQL `FOR v IN 1..3 OUTBOUND`

**Docs:** `recursive_path_queries.md`, `temporal_time_range_queries.md`, `property_graph_model.md`

### 5. Time-Series
- **Gorilla Compression** – 10-20x Ratio für Double-Werte
- **Continuous Aggregates** – Pre-computed Rollups
- **Retention Policies** – Auto-Deletion alter Daten
- **HTTP Config** – GET/PUT `/ts/config` für Runtime-Anpassung

**Docs:** `time_series.md`, `compression_benchmarks.md`

### 6. Security & Compliance
- **PKI Integration** – VCCPKIClient, PKIKeyProvider (Tests: 6/6, 10/10 PASS)
- **Field-Level Encryption** – Schema-basierte Auto-Encryption
- **JWT Validation** – Token-basierte AuthN/AuthZ (6/6 Tests PASS)
- **RBAC** – Scopes (admin, config:write, cdc:read, metrics:read)
- **Audit Logging** – Encrypt-then-Sign für Compliance

**Docs:** `pki_integration_architecture.md`, `encryption_strategy.md`, `rbac_authorization.md`, `compliance_audit.md`

### 7. Change Data Capture (CDC)
- **Changefeed** – Sequence-basiertes Event Log (PUT/DELETE)
- **SSE Streaming** – GET `/changefeed/stream` für Real-Time
- **Retention** – POST `/changefeed/retention` für Cleanup

**Docs:** `change_data_capture.md` (⚠️ `cdc.md` ist archiviert)

### 8. Content Management
- **Universal Content Manager** – Text, Image, Audio, Geo (Plugin-basiert)
- **Chunk Pipeline** – Extraktion, Chunking, Chunk-Graph
- **Hybrid Queries** – Vector + Graph + Relational kombiniert

**Docs:** `content_architecture.md`, `content_pipeline.md`, `content/ingestion.md`

---

## Strategische Dokumente

| Dokument | Thema |
|----------|-------|
| `STRATEGIC_OVERVIEW.md` | Executive Summary, Investment-Analyse, Timeline |
| `competitive_gap_analysis.md` | Vergleich mit MongoDB, PostgreSQL, Neo4j, Elasticsearch, Pinecone, InfluxDB |
| `infrastructure_roadmap.md` | Sharding, Replication, Client SDKs, Admin UI (Roadmap Q1-Q3 2026) |
| `development/todo.md` | Laufende Roadmap, Feature-Status, Sprint-Historie |
| `development/implementation_status.md` | Detaillierter Umsetzungsstatus aller Module |

---

## Archivierte Dokumente

Die folgenden Dokumente wurden archiviert (Stand: 09.11.2025) und sind **nicht mehr autoritativ**:

| Datei (Stub) | Ersatz | Grund |
|--------------|--------|-------|
| `cdc.md` | `change_data_capture.md` | War bereits als veraltet markiert |
| `release_scope_core.md` | Verteilte Feature-Docs + Roadmaps | Draft-Status, jetzt in Einzeldocs |
| `path_constraints.md` | `recursive_path_queries.md` | Konzept, nicht implementiert |
| `research_postgis_opensearch_h3s2_mvp.md` | (keine aktive Geo-Impl.) | Geo außerhalb Release-Scope |

Vollständige Inhalte befinden sich unter `archive/` mit README-Übersicht.

---

## Dokumentations-Struktur

```
docs/
├── index.md (diese Seite)
├── architecture.md                    # System-Überblick
├── aql_syntax.md                      # AQL Sprachreferenz
├── query_engine_aql.md                # Query Engine Internals
├── vector_ops.md                      # Vector Index Operations
├── time_series.md                     # Time-Series Engine
├── change_data_capture.md             # CDC/Changefeed
├── pki_integration_architecture.md    # PKI/Security
├── encryption_strategy.md             # Field-Level Encryption
├── tracing.md                         # OpenTelemetry/Jaeger
├── openapi.yaml                       # REST API Spec
├── STRATEGIC_OVERVIEW.md              # Executive Summary
├── competitive_gap_analysis.md        # Market Analysis
├── infrastructure_roadmap.md          # Scaling Roadmap
├── archive/                           # Veraltete Dokumente
│   ├── README.md
│   ├── cdc_legacy.md
│   ├── release_scope_core_draft.md
│   ├── path_constraints_concept.md
│   └── geo_research_report_mvp.md
├── apis/
│   └── openapi.md
├── search/
│   ├── fulltext_api.md
│   ├── stemming.md
│   └── migration_guide.md
├── content/
│   ├── ingestion.md
│   └── image_processor_design.md
├── development/
│   ├── todo.md
│   ├── implementation_status.md
│   └── priorities.md
└── security/
    ├── policies.md
    └── pki_rsa_integration.md
```

---

## Qualitäts-Status (09.11.2025)

| Kategorie | Status | Tests | Notizen |
|-----------|--------|-------|---------|
| **AQL Core** | ✅ Implementiert | 468/468 PASS | FOR, FILTER, SORT, LIMIT, RETURN, LET, COLLECT |
| **Full-Text** | ✅ Implementiert | 23/23 PASS | BM25, Stemming, Umlaut-Normalisierung |
| **Vector Search** | ✅ Implementiert | 17 Unit + 6 HTTP PASS | HNSW, 3 Metriken, Batch/Cursor |
| **Graph** | ✅ Implementiert | 4/4 Type-Filtering, 6/6 Temporal | BFS/Dijkstra, Edge Types, Temporal Stats |
| **Time-Series** | ✅ Implementiert | 6/6 Gorilla | Compression, Aggregates, Retention |
| **Security** | ✅ Implementiert | VCCPKIClient 6/6, PKI 10/10, JWT 6/6 | Field Encryption, PKI, JWT |
| **CDC** | ✅ Implementiert | Integration Tests | Changefeed, SSE Streaming |

---

## Bekannte Einschränkungen

- **Vector Metadata Encryption:** Erfordert Policy-Konfiguration (`Authorization` Header + matching policy)
- **Geo Features:** Außerhalb Release-Scope (siehe `archive/geo_research_report_mvp.md`)
- **Sharding/Replication:** Roadmap Q1-Q2 2026 (siehe `infrastructure_roadmap.md`)
- **Client SDKs:** Python/JS/Java in Planung (Q2-Q3 2026)

---

## Nächste Schritte (Roadmap)

### Kurzfristig (Q4 2025)
- ✅ Dokumentations-Cleanup (Archive, Encoding-Fixes)
- ⏳ Policy-Konfiguration für Vector Write Routes finalisieren
- ⏳ Performance-Tuning für Bulk Encryption (Throughput-Test)

### Mittelfristig (Q1-Q2 2026)
- URN-basiertes Föderales Sharding
- Raft-basierte Replication (HA)
- Client SDKs (Python, JavaScript, Java)

### Langfristig (Q2-Q3 2026)
- React Admin UI
- Geo-Modul (Storage, R-Tree Index, ST_* AQL)
- Vector Quantization (4x Memory-Reduktion)

**Vollständige Roadmap:** `infrastructure_roadmap.md`, `development/todo.md`

---

## Support & Beitragen

- **Issues:** GitHub Issues (makr-code/ThemisDB)
- **Diskussionen:** GitHub Discussions
- **Style Guide:** `styleguide.md`
- **Code Quality:** `code_quality.md`

---

## Lizenz & Credits

ThemisDB © 2025 makr-code  
Lizenz: Siehe `LICENSE` im Repository

**Abhängigkeiten:**
- RocksDB (Apache 2.0)
- OpenSSL (Apache 2.0)
- Boost (BSL-1.0)
- Intel TBB (Apache 2.0)
- HNSWlib (Apache 2.0)

---

**Letzte Aktualisierung:** 09. November 2025
