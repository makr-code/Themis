# ThemisDB Wiki - v0.1.0_alpha

Willkommen zur offiziellen Dokumentation von **ThemisDB** – einer Multi-Model-Datenbank mit LSM-Tree-Speicher-Engine, AQL-Query-Language und umfassenden Sicherheitsfeatures.

## 🚀 Quick Start

**Version:** v0.1.0_alpha  
**Repository:** [github.com/makr-code/ThemisDB](https://github.com/makr-code/ThemisDB)  
**Docker Images:** `ghcr.io/makr-code/themis:latest`, `themisdb/themis:latest`

### Für Benutzer
- **[Erste Schritte](deployment.md)** - Installation und Setup
- **[AQL Syntax](aql_syntax.md)** - Query Language Einführung
- **[REST API](apis/openapi.md)** - HTTP Endpunkte

### Für Entwickler
- **[Architektur](architecture.md)** - System-Design und Komponenten
- **[Build Anleitungen](#build-anleitungen)** - Windows, Linux/WSL, Docker
- **[Contributing](../CONTRIBUTING.md)** - Contribution Guidelines

### Für Operators
- **[Deployment Guide](deployment.md)** - Production Deployment
- **[Operations Runbook](operations_runbook.md)** - Betrieb und Wartung
- **[Security Hardening](security_hardening_guide.md)** - Sicherheitskonfiguration

## 📋 Dokumentations-Übersicht

### Core Konzepte
- [Architektur](architecture.md) - Multi-Model Design
- [Base Entity](base_entity.md) - Kanonisches Speichermodell
- [MVCC Design](mvcc_design.md) - Transaktionale Konsistenz
- [Property Graph](property_graph_model.md) - Graph-Datenmodell

### Storage & Indexe
- [Indexes](indexes.md) - Sekundärindizes, Geo, Fulltext
- [RocksDB Layout](storage/rocksdb_layout.md) - Storage-Engine
- [Memory Tuning](memory_tuning.md) - Performance-Optimierung
- [Time Series](time_series.md) - Zeitreihendaten mit Gorilla-Kompression

### Query Engine
- [AQL Syntax](aql_syntax.md) - Query Language
- [EXPLAIN/PROFILE](aql_explain_profile.md) - Query-Analyse
- [Recursive Path Queries](recursive_path_queries.md) - Graph-Traversierung
- [Temporal Queries](temporal_time_range_queries.md) - Zeitbasierte Abfragen

### Sicherheit
- [Security Overview](security/overview.md) - Sicherheitsarchitektur
- [RBAC & Authorization](rbac_authorization.md) - Zugriffskontrolle
- [TLS Setup](TLS_SETUP.md) - TLS/mTLS Konfiguration
- [Encryption Strategy](encryption_strategy.md) - Verschlüsselung
- [Audit Logging](AUDIT_LOGGING.md) - Audit-Trail

### APIs & Integration
- [OpenAPI Specification](apis/openapi.md) - REST API Referenz
- [Change Data Capture](change_data_capture.md) - CDC/Changefeed
- [Content Ingestion](content/ingestion.md) - Bulk Import
- [Admin Tools](admin_tools_user_guide.md) - Management Tools

### Operations
- [Deployment](deployment.md) - Installation und Konfiguration
- [Prometheus Metrics](observability/prometheus_metrics.md) - Monitoring
- [Operations Runbook](operations_runbook.md) - Day-2 Operations
- [Backup & Recovery](deployment.md#backup--recovery) - Datensicherung

## 🔧 Build Anleitungen

### Windows (PowerShell)
```powershell
# Setup
.\setup.ps1

# Build (Debug)
.\build.ps1 -BuildType Debug

# Build und Tests ausführen
.\build.ps1 -BuildType Release -RunTests

# Server starten
.\build-msvc\Release\themis_server.exe --config config.yaml
```

### Linux/WSL (Bash)
```bash
# Setup
./setup.sh

# Build
./build.sh BUILD_TYPE=Release RUN_TESTS=1

# Server starten
./build-wsl/themis_server --config config.yaml
```

### Docker (Multi-Platform)

**Docker Hub:**
```bash
docker pull themisdb/themis:latest
docker run -d -p 8765:8765 -v themis-data:/data themisdb/themis:latest
```

**GitHub Container Registry:**
```bash
docker pull ghcr.io/makr-code/themis:latest
docker run -d -p 8765:8765 -v themis-data:/data ghcr.io/makr-code/themis:latest
```

**QNAP Deployment:**
```bash
# docker-compose.qnap.yml verwenden
docker-compose -f docker-compose.qnap.yml up -d
```

**Eigenes Build:**
```bash
docker build -t themisdb:local .
docker run -d -p 8765:8765 themisdb:local
```

Siehe auch: [Docker Build Guide](../DOCKER_BUILD.md)

## 📊 Features (v0.1.0_alpha)

### ✅ Production-Ready
- **MVCC Transactions** - Snapshot Isolation, ACID-Garantien
- **Vector Search** - HNSW Index mit Persistenz
- **Time Series** - Gorilla Compression, Continuous Aggregates
- **Graph Traversals** - BFS, Dijkstra, temporale Graphen
- **Security** - TLS 1.3, RBAC, Rate Limiting, Audit Logging
- **Observability** - Prometheus Metrics, OpenTelemetry Tracing

### 🚧 In Development
- **Hybrid Search** - Kombination Vector + Graph + Relational (Phase 4)
- **Content Pipeline** - Image/Geo Processors (In Progress)
- **Column Encryption** - Field-Level Encryption (Design Phase)

Siehe auch: [Roadmap](roadmap.md), [Implementation Status](development/implementation_status.md)

## 🛠️ Admin Tools

ThemisDB bietet 7 WPF-basierte Admin-Tools für Betrieb und Compliance:

- **Audit Log Viewer** - Audit-Trail Analyse
- **SAGA Verifier** - Transaktionsintegrität prüfen
- **PII Manager** - Personenbezogene Daten verwalten
- **Key Rotation Dashboard** - Schlüsselrotation
- **Retention Manager** - Datenaufbewahrung
- **Classification Dashboard** - Datenklassifizierung
- **Compliance Reports** - Compliance-Berichte

Siehe: [Admin Tools User Guide](admin_tools_user_guide.md)

## 📚 Weitere Ressourcen

- **GitHub Pages:** [makr-code.github.io/ThemisDB](https://makr-code.github.io/ThemisDB/)
- **Druckversion:** [makr-code.github.io/ThemisDB/print_page/](https://makr-code.github.io/ThemisDB/print_page/)
- **PDF Dokumentation:** [themisdb-docs-complete.pdf](https://makr-code.github.io/ThemisDB/themisdb-docs-complete.pdf)
- **Issue Tracker:** [GitHub Issues](https://github.com/makr-code/ThemisDB/issues)

## 📖 Glossar & Styleguide

- [Glossar](glossary.md) - Begriffsdefinitionen
- [Styleguide](styleguide.md) - Dokumentationskonventionen

---

**Letzte Aktualisierung:** 2025-11-19  
**Version:** v0.1.0_alpha  
**Lizenz:** MIT
