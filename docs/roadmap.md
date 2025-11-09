# Roadmap

**Stand:** 09. November 2025  
**Version:** Post-Core-Release Roadmap

Diese Roadmap skizziert priorisierte Vorhaben für ThemisDB nach Abschluss der Core-Features. Zeitpläne sind indikativ; Änderungen ergeben sich aus Feedback und Prioritäten.

---

## ✅ Abgeschlossen (Q4 2025)

### Core Database Features
- ✅ AQL v1.3 (FOR/FILTER/SORT/LIMIT/RETURN, LET, COLLECT, Joins)
- ✅ Full-Text Search (BM25, Stemming DE/EN, Umlaut-Normalisierung)
- ✅ Vector Search (HNSW, 3 Metriken: COSINE/L2/DOT, Batch/Cursor)
- ✅ Graph Database (BFS/Dijkstra, Edge Type Filtering, Temporal Aggregations)
- ✅ Time-Series (Gorilla Compression, Continuous Aggregates, Retention)
- ✅ Security Stack (VCCPKIClient, PKIKeyProvider, JWT, Field Encryption)
- ✅ Change Data Capture (Changefeed, SSE Streaming, Retention)
- ✅ Observability (OpenTelemetry, Prometheus Metrics, Structured Logs)
- ✅ Dokumentation (Archiv-System, Index überarbeitet, Encoding-Fixes)

**Test-Status:** AQL 468/468, Full-Text 23/23, Vector 17+6, Graph 4+6, TS 6, Security 6+10+6

---

## Kurzfristig (Q4 2025 – Q1 2026)

### Performance & Stabilität
- ⏳ Policy-Konfiguration für Vector Write Routes finalisieren
- ⏳ Performance-Tuning für Bulk Encryption (Throughput-Test optimieren)
- ⏳ Vector Index: Warmstart-Optimierungen, Online Reindex
- ⏳ Query Optimizer: Cost-based Index Selection, Join Order Optimization

### Security & Compliance
- ⏳ PKI Hardening: Chain Validation, Revocation (CRL/OCSP), Canonical JSON, Mode/Flags in Audit
- ⏳ Column-Level Key Rotation APIs
- ⏳ Dynamic Data Masking (Erweiterte Regeln)
- ⏳ Externe KMS-Integration (Vault/AWS KMS optional)
- ⏳ RBAC: Row-Level Security (RLS) Policies

### Observability
- ⏳ CI: clang-tidy/cppcheck Gates, Coverage-Reporting
- ⏳ Secrets-Scanning (gitleaks/truffleHog)
- ⏳ Mehr Metriken (Query-Latenzen pro Typ, Index-Stats)
- ⏳ Trace-Sampling Regeln (adaptive Sampling)

### Backup & Recovery
- ⏳ Inkrementelle Backups mit Kompression
- ⏳ Automatisierung (systemd/K8s CronJobs)
- ⏳ Point-in-Time Recovery (PITR) Basis

---

## Mittelfristig (Q1 – Q2 2026)

### Distributed Systems (HIGH PRIORITY)
- 🚀 **URN-basiertes Föderales Sharding**
  - URN-Schema für Bundesland-basierte Partitionierung
  - Shard-Router mit Consistent Hashing
  - Cross-Shard Query Execution
  - **Ziel:** Horizontal Scaling auf 100+ Nodes
  
- 🚀 **Raft-basierte Replication (HA)**
  - Leader-Election & Log Replication
  - Read Replicas für Lese-Skalierung
  - Automatic Failover
  - **Ziel:** 99.99% Uptime, 11-nines Durability

### Client SDKs
- 🚀 **Python Client Library**
  - Connection Pooling, Retry Logic
  - AQL Query Builder
  - Vector/Graph/TS Utilities
  
- 🚀 **JavaScript/TypeScript Client**
  - Node.js & Browser Support
  - Promise-based API
  - Type Definitions
  
- 🚀 **Java Client** (Optional)
  - JDBC-ähnliche API
  - Spring Boot Integration

### Query Engine Enhancements
- ⏳ Window Functions (ROW_NUMBER, RANK, LAG, LEAD)
- ⏳ Recursive CTEs (WITH RECURSIVE)
- ⏳ Materialized Views
- ⏳ Query Result Caching

### Indexing Improvements
- ⏳ Composite Indexes (Multi-Column B-Tree)
- ⏳ GIN-like Indexes für JSON
- ⏳ Progressive Reindexing (Zero-Downtime)
- ⏳ Compressed Inverted Indexes (Full-Text)

---

## Langfristig (Q2 – Q3 2026)

### Admin UI
- 🎨 **React Admin Dashboard**
  - Query Editor mit Syntax Highlighting
  - Visual Schema Browser
  - Real-Time Metrics & Dashboards
  - Index Management UI
  - Backup/Restore UI

### Geo Features (Post-Release)
- 📍 **Geo Storage & Index**
  - WKB/EWKB(Z) Storage
  - R-Tree Spatial Index
  - Z-Range Index für 3D
  - ST_* AQL Functions (PostGIS-compatible)
  
- 📍 **Advanced Geo**
  - H3/S2 Pre-Filter Indexes
  - Prepared Geometries (GEOS optional)
  - GPU/SIMD Acceleration

### Vector Enhancements
- 🔬 **Quantization**
  - Product Quantization (PQ)
  - Scalar Quantization (SQ8)
  - **Ziel:** 4x Memory-Reduktion, 97% Recall
  
- 🔬 **GPU Acceleration**
  - CUDA/ROCm für Bulk Operations
  - 10-100x Speedup für Batch Insert/Search

### Analytics & OLAP
- 📊 **Apache Arrow Integration**
  - RecordBatch Import/Export
  - Zero-Copy Reads
  - SIMD-optimierte Aggregationen
  
- 📊 **OLAP Features**
  - Columnar Storage Option
  - Star/Snowflake Schema Support
  - Aggregation Pushdown

### Multi-Tenancy & Compliance
- 🏢 **Multi-Tenancy**
  - Tenant Isolation mit Quotas
  - Per-Tenant Encryption Keys
  - Billing/Usage Metering
  
- 📋 **Compliance Vorlagen**
  - GDPR/DSGVO Templates
  - ISO 27001 Controls
  - SOX/HIPAA Mappings

---

## Risiken und Gegenmaßnahmen

| Risiko | Wahrscheinlichkeit | Impact | Mitigation |
|--------|-------------------|--------|------------|
| **Performance-Regressionen** | Mittel | Hoch | Regelmäßige Benchmarks, Budget für Optimierungssprints |
| **Sicherheitslücken** | Niedrig | Kritisch | Security Reviews pro Release, Pen-Tests bei größeren Änderungen |
| **Architektur-Komplexität** | Hoch | Mittel | Modulare Architektur, klare Verantwortlichkeiten, Dokumentation aktuell halten |
| **Sharding-Komplexität** | Hoch | Hoch | Phased Rollout, MVP-first Approach, Extensive Testing |
| **Client SDK Adoption** | Mittel | Mittel | Community Feedback, Gute Docs, Beispiele |

---

## Priorisierungs-Matrix

```
High Priority, High Impact:
- Sharding & Replication (Skalierung)
- Client SDKs (Adoption)
- Admin UI (Usability)

High Priority, Medium Impact:
- Query Optimizer (Performance)
- Vector Quantization (Memory)
- Backup/Recovery (Operations)

Medium Priority:
- Geo Features (Specialized Use-Cases)
- Multi-Tenancy (Enterprise)
- OLAP/Arrow (Analytics)

Low Priority:
- GPU Acceleration (Niche)
- Compliance Templates (Regulatory)
```

---

## Community & Beitragen

- **GitHub Issues:** Feature Requests & Bug Reports
- **GitHub Discussions:** Architektur-Feedback, Use-Cases
- **Pull Requests:** Code-Beiträge willkommen (siehe `CONTRIBUTING.md`)
- **Dokumentation:** Verbesserungsvorschläge via PR

---

**Letzte Aktualisierung:** 09. November 2025  
**Nächste Review:** Q1 2026
