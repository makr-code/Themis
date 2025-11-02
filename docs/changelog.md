# Changelog

Alle nennenswerten Änderungen dieses Projekts werden in diesem Dokument festgehalten. Das Format orientiert sich lose an Keep a Changelog.

## [Unreleased]

### Added
- **RBAC/Authorization (MVP):** Token-basierte Zugriffskontrolle mit Scopes (admin, config:write, cdc:read, metrics:read)
  - AuthMiddleware Klasse mit Scope-Prüfung und Metriken
  - Dokumentation: `docs/rbac_authorization.md`
  - Tests: `tests/test_auth_middleware.cpp`
- **HNSW Persistenz & Warmstart:** Vektorindex-Persistierung für schnellere Startzeiten
  - `saveIndex()`, `loadIndex()`, `setAutoSavePath()`, `shutdown()` APIs
  - Automatisches Laden beim Init, Auto-Save bei Shutdown
  - Dokumentation: `docs/hnsw_persistence.md`
- Konsolidierte Seite „Performance & Benchmarks" mit Kompression, Pagination, MVCC/WriteBatch, Index-Rebuilds, Vector-Tuning
- Vector-Benchmarks: `BM_VectorSearch_efSearch`, `BM_VectorInsert_Batch100`
- Qualitätssicherung (QA) Dokument mit Teststrategie, CI/CD, Coverage, Static Analysis
- Security/Compliance Review Seite mit Checkliste und Verlinkungen

### Changed
- Navigation (mkdocs.yml) um RBAC, HNSW Persistenz, Performance, QA, Security Review, Roadmap & Changelog erweitert
- Pagination/Kompressionsdokus ergänzt und verlinkt

### Fixed
- Diverse Dokumentverlinkungen (Deployment, CDC, Index-Metriken) bereinigt
- MkDocs-Build-Fehler behoben (YAML-Indentation, docs_dir entfernt)

## [0.1.0] - 2025-10-20

Erste konsolidierte Doku-Fassung (Architektur, Storage & MVCC, Query Engine & AQL, Indexe, Content Pipeline, Deployment & Betrieb).

### Added
- Grundlegende Seitenstruktur und Navigation
- OpenAPI und Admin-Tools Dokus
