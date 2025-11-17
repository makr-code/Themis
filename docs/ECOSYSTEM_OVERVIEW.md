# ThemisDB Ecosystem Overview

**Stand:** 17. November 2025  
**Zweck:** Zentrale Übersicht über alle ThemisDB-Komponenten, APIs, Tools und SDKs

---

## Übersicht

Das ThemisDB-Ökosystem besteht aus mehreren Komponenten:

1. **Core Database** - Die Hauptdatenbank (themis_server)
2. **Client SDKs** - Programmatische Zugriffsbibliotheken
3. **Admin Tools** - Desktop-Anwendungen für Verwaltung und Monitoring
4. **Adapters** - Daten-Ingestion und Integration
5. **APIs** - HTTP REST Endpoints

---

## 1. Core Database (themis_server)

**Verzeichnis:** `src/server/`  
**Binary:** `themis_server` / `themis_server.exe`  
**Port:** 8765 (Standard)

### Features
- Multi-Model Database (Relational, Graph, Vector, Time-Series)
- MVCC Transactions mit Snapshot Isolation
- HNSW Vector Search mit Persistenz
- AQL Query Language
- Prometheus Metrics Export
- Change Data Capture (CDC)

### Dokumentation
- [deployment.md](deployment.md) - Installation und Deployment
- [operations_runbook.md](operations_runbook.md) - Operations-Guide
- [observability/prometheus_metrics.md](observability/prometheus_metrics.md) - Monitoring

---

## 2. Client SDKs

### 2.1 Python SDK ✅ MVP

**Verzeichnis:** `clients/python/`  
**Paket:** `themis-db` (PyPI - geplant)  
**Status:** ✅ Experimentell/MVP

**Features:**
- Topologie-Discovery (Multi-Node Support)
- CRUD Operations
- Query Execution (AQL)
- Vector Search
- Batch Operations
- Cursor Pagination

**Installation:**
```bash
cd clients/python
pip install -e .
```

**Beispiel:**
```python
from themis import ThemisClient

client = ThemisClient(["http://localhost:8765"], namespace="default")
print(client.health())

# Entity CRUD
client.put_entity("users:alice", {"name": "Alice", "age": 30})
entity = client.get_entity("users:alice")

# Query
results = client.query("FOR u IN users FILTER u.age > 25 RETURN u")

# Vector Search
results = client.vector_search([0.1, 0.2, ...], k=10)
```

**Dokumentation:**
- `clients/python/README.md` - Quickstart
- [docs/clients/python_sdk_quickstart.md](clients/python_sdk_quickstart.md) - Vollständiger Guide (geplant)

---

### 2.2 JavaScript/TypeScript SDK ⏳ Alpha

**Verzeichnis:** `clients/javascript/`  
**Paket:** `@themisdb/client` (npm - geplant)  
**Status:** ⏳ In Entwicklung

**Features (geplant):**
- TypeScript-Typen
- Query Execution
- Vector Search
- Batch Operations
- ESLint/TSC Setup

**Installation (geplant):**
```bash
npm install @themisdb/client
```

**Dokumentation:**
- `clients/javascript/README.md`
- [docs/clients/javascript_sdk_quickstart.md](clients/javascript_sdk_quickstart.md) - Vollständiger Guide (geplant)

---

### 2.3 Rust SDK ⏳ Alpha

**Verzeichnis:** `clients/rust/`  
**Crate:** `themis-client` (crates.io - geplant)  
**Status:** ⏳ Alpha

**Features:**
- Topologie-Cache
- CRUD Operations
- Query & Vector Search
- Cargo Library Configuration

**Installation (geplant):**
```toml
[dependencies]
themis-client = "0.1.0"
```

**Dokumentation:**
- [docs/clients/rust_sdk_quickstart.md](clients/rust_sdk_quickstart.md) - Vollständiger Guide

---

### 2.4 Weitere SDKs (Geplant)

- **Java SDK** - Geplant für Enterprise-Integration
- **C++ SDK** - Für High-Performance Anwendungen
- **Go SDK** - Für Cloud-Native Deployments

---

## 3. Admin Tools (.NET Desktop Applications)

**Verzeichnis:** `tools/`  
**Plattform:** Windows (.NET 8)  
**Status:** ✅ MVP AuditLogViewer abgeschlossen

### 3.1 Themis.AuditLogViewer ✅ MVP

**Features:**
- Anzeige verschlüsselter Audit-Logs
- Zeitbereichsfilter, Benutzerfilter, Aktionsfilter
- Paginierung (100 Einträge pro Seite)
- CSV-Export
- Moderne WPF-UI mit DataGrid

**Architektur:**
- MVVM-Pattern (CommunityToolkit.Mvvm)
- Dependency Injection
- Async/Await für API-Calls

**Voraussetzungen:**
- .NET 8 SDK
- Zugriff auf themis_server API (`/api/audit`)

**Installation:**
```powershell
cd tools
dotnet restore
dotnet build
cd Themis.AuditLogViewer
dotnet run
```

**Dokumentation:**
- [tools/README.md](../tools/README.md) - Vollständiger Guide
- [tools/STATUS.md](../tools/STATUS.md) - Entwicklungsstand

---

### 3.2 Weitere Admin Tools (In Entwicklung)

| Tool | Status | Beschreibung |
|------|--------|--------------|
| **Themis.SAGAVerifier** | ⏳ Geplant | Manipulationsschutz-Verifikation |
| **Themis.PIIManager** | ⏳ Geplant | PII-Detection und Redaction Management |
| **Themis.KeyRotationDashboard** | ⏳ Geplant | Encryption Key Management |
| **Themis.RetentionManager** | ⏳ Geplant | Retention Policy Management |
| **Themis.ClassificationDashboard** | ⏳ Geplant | Data Classification Viewer |
| **Themis.ComplianceReports** | ⏳ Geplant | DSGVO/eIDAS Compliance Reports |

**Shared Library:**
- **Themis.AdminTools.Shared** - HTTP-Client, DTOs, Utilities

---

## 4. Adapters (Daten-Ingestion)

### 4.1 Covina FastAPI Ingestion Adapter ✅

**Verzeichnis:** `adapters/covina_fastapi_ingestion/`  
**Typ:** Python FastAPI Application  
**Port:** 8001 (Standard)  
**Status:** ✅ Produktiv

**Features:**
- File-Upload (`POST /ingest/file`) - Text/PDF/DOCX → THEMIS
- JSON-Direktimport (`POST /ingest/json`)
- Optional: Embedding-Erzeugung (sentence-transformers)
- Minimal Dependencies (ohne UDS3-Framework)

**Use Cases:**
- Text-Dokumente ingestion
- Content/Chunks/Edges automatisch erzeugen
- Preprocessing für Vector Search

**Installation:**
```powershell
cd adapters\covina_fastapi_ingestion
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
$env:THEMIS_URL = "http://127.0.0.1:8765"
uvicorn app:app --host 127.0.0.1 --port 8001 --reload
```

**Beispiel:**
```powershell
# Textdatei ingestieren
Invoke-WebRequest -Uri http://127.0.0.1:8001/ingest/file `
  -Method POST -InFile .\document.txt -ContentType "text/plain"
```

**Dokumentation:**
- [adapters/covina_fastapi_ingestion/README.md](../adapters/covina_fastapi_ingestion/README.md)
- [adapters/covina_fastapi_ingestion/MODALITIES.md](../adapters/covina_fastapi_ingestion/MODALITIES.md)

---

### 4.2 Weitere Adapters (Geplant)

- **Kafka Adapter** - Stream-Processing Integration
- **S3 Adapter** - Cloud Storage Ingestion
- **Database Sync Adapter** - PostgreSQL/MySQL CDC

---

## 5. HTTP REST APIs

### 5.1 Core Entity API

**Endpoints:**
- `GET /entities/{key}` - Entity abrufen
- `PUT /entities/{key}` - Entity erstellen/aktualisieren
- `DELETE /entities/{key}` - Entity löschen
- `POST /entities/batch` - Batch-Operationen

**Dokumentation:** [apis/rest_api.md](apis/rest_api.md) - Geplant

---

### 5.2 Query API (AQL)

**Endpoints:**
- `POST /query` - AQL Query ausführen
- `POST /query/explain` - Query-Plan anzeigen

**Beispiel:**
```json
POST /query
{
  "query": "FOR u IN users FILTER u.age > 25 RETURN u",
  "bind_vars": {}
}
```

**Dokumentation:** 
- [aql_syntax.md](aql_syntax.md)
- [query_engine_aql.md](query_engine_aql.md)

---

### 5.3 Vector API

**Endpoints:**
- `POST /vector/search` - k-NN Suche
- `POST /vector/batch_insert` - Batch-Insert
- `DELETE /vector/by-filter` - Löschen nach Filter
- `POST /vector/index/save` - Index speichern
- `POST /vector/index/load` - Index laden
- `GET /vector/index/config` - Konfiguration abrufen
- `PUT /vector/index/config` - Konfiguration ändern

**Dokumentation:** [vector_ops.md](vector_ops.md)

---

### 5.4 Time-Series API

**Endpoints:**
- `POST /ts/put` - DataPoint hinzufügen
- `POST /ts/query` - Zeitreihen abfragen
- `POST /ts/aggregate` - Aggregationen berechnen
- `GET /ts/config` - Konfiguration abrufen

**Dokumentation:** [time_series.md](time_series.md)

---

### 5.5 Admin API

**Endpoints:**
- `POST /admin/backup` - Backup erstellen
- `POST /admin/restore` - Backup wiederherstellen
- `GET /api/audit` - Audit-Logs abrufen
- `GET /api/audit/export/csv` - Audit-Logs als CSV

**Dokumentation:** 
- [deployment.md](deployment.md#backup--recovery)
- [tools/README.md](../tools/README.md) - Audit API

---

### 5.6 Observability API

**Endpoints:**
- `GET /metrics` - Prometheus Metrics
- `GET /health` - Health Check
- `GET /stats` - System-Statistiken
- `GET /config` - Server-Konfiguration

**Dokumentation:** [observability/prometheus_metrics.md](observability/prometheus_metrics.md)

---

## 6. Development Tools

### 6.1 Debug Tools

**debug_graph_keys.cpp** - Graph Key Debugging Tool
- Verzeichnis: `tools/`
- Kompilieren: Manuell mit g++/clang++
- Verwendung: Debugging von Graph-Index-Keys

**sign_pii_engine.py** - PII Engine Signatur Tool
- Verzeichnis: `tools/`
- Python-Script für PKI-Signaturen
- Verwendung: PII Detection Engine signieren

**publish_wiki.py** - Wiki Publishing Tool
- Verzeichnis: `tools/`
- Automatisiertes Wiki-Publishing
- GitHub Wiki Integration

---

## 7. Konfiguration & Deployment

### Server-Konfiguration

**Datei:** `config/config.json`  
**Beispiel:**
```json
{
  "server": {
    "host": "0.0.0.0",
    "port": 8765
  },
  "storage": {
    "data_path": "./data/themis_server",
    "block_cache_size_mb": 2048
  },
  "vector_index": {
    "save_path": "./data/vector_index",
    "auto_save": true
  },
  "features": {
    "timeseries": true,
    "cdc": true
  }
}
```

**Dokumentation:** [deployment.md](deployment.md)

---

## 8. Testing & CI/CD

### Test-Suites

**Unit Tests:**
- Verzeichnis: `tests/`
- Framework: Google Test
- Ausführen: `ctest` oder `themis_tests.exe`

**Integration Tests:**
- Python-basierte Tests
- Docker Compose Stack

**CI/CD:**
- GitHub Actions Workflows
- `.github/workflows/ci.yml`
- `.github/workflows/code-quality.yml`

---

## 9. Dokumentations-Roadmap

### Abgeschlossen ✅
- [x] Core Database Dokumentation
- [x] Prometheus Metrics Reference
- [x] Vector Operations Guide
- [x] Time-Series Guide
- [x] Backup/Restore Guide
- [x] Admin Tools README

### In Bearbeitung ⏳
- [ ] API Referenz (OpenAPI/Swagger)
- [ ] Client SDK Tutorials
- [ ] Adapter Entwicklungs-Guide

### Geplant 📋
- [ ] Video-Tutorials
- [ ] Architektur-Diagramme (aktualisiert)
- [ ] Performance-Tuning Guide
- [ ] Migration Guide
- [ ] Disaster Recovery Playbook

---

## 10. Support & Community

### Dokumentation
- **GitHub Pages:** https://makr-code.github.io/ThemisDB/
- **Wiki:** https://github.com/makr-code/ThemisDB/wiki
- **PDF:** [themisdb-docs-complete.pdf](https://makr-code.github.io/ThemisDB/themisdb-docs-complete.pdf)

### Development
- **Repository:** https://github.com/makr-code/ThemisDB
- **Issues:** GitHub Issues
- **Pull Requests:** Contributions willkommen

### Lizenz
Siehe Hauptprojekt-Lizenz

---

## 11. Quick Links

| Komponente | Dokumentation | Status |
|-----------|---------------|--------|
| Core Database | [deployment.md](deployment.md) | ✅ Produktiv |
| Python SDK | [clients/python/README.md](../clients/python/README.md) | ✅ MVP |
| Admin Tools | [tools/README.md](../tools/README.md) | ✅ MVP |
| Covina Adapter | [adapters/covina_fastapi_ingestion/README.md](../adapters/covina_fastapi_ingestion/README.md) | ✅ Produktiv |
| Vector Search | [vector_ops.md](vector_ops.md) | ✅ Produktiv |
| Time-Series | [time_series.md](time_series.md) | ✅ Produktiv |
| Prometheus Metrics | [observability/prometheus_metrics.md](observability/prometheus_metrics.md) | ✅ Produktiv |
| AQL Syntax | [aql_syntax.md](aql_syntax.md) | ✅ Produktiv |

---

**Letzte Aktualisierung:** 17. November 2025  
**Version:** 1.0  
**Status:** Production Ready
