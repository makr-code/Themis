# ThemisDB - Multi-Model Database System

[![Docs](https://img.shields.io/badge/docs-GitHub%20Pages-0a7ea4)](https://makr-code.github.io/ThemisDB/)
[![Print](https://img.shields.io/badge/print-Gesamtansicht-555)](https://makr-code.github.io/ThemisDB/print_page/)
[![PDF](https://img.shields.io/badge/PDF-Gesamtdoku-blueviolet)](https://makr-code.github.io/ThemisDB/themisdb-docs-complete.pdf)
[![Wiki](https://img.shields.io/badge/wiki-GitHub%20Wiki-0366d6)](https://github.com/makr-code/ThemisDB/wiki)

[![CI](https://github.com/makr-code/ThemisDB/actions/workflows/ci.yml/badge.svg)](https://github.com/makr-code/ThemisDB/actions/workflows/ci.yml)
[![Code Quality](https://github.com/makr-code/ThemisDB/actions/workflows/code-quality.yml/badge.svg)](https://github.com/makr-code/ThemisDB/actions/workflows/code-quality.yml)
[![Coverage](https://img.shields.io/badge/coverage-view%20report-brightgreen)](https://makr-code.github.io/ThemisDB/coverage/)

> **A unified multi-model database supporting relational, document, graph, and vector data models with ACID transactions, built on RocksDB LSM-Tree storage.**

ThemisDB is a production-ready, high-performance database that combines multiple data models in a single, transactionally consistent system. Query across relational tables, graph relationships, and vector embeddings using a unified query language (AQL).

## ✨ Key Features

- **🔄 Multi-Model**: Relational, Document, Graph, and Vector data in one system
- **💪 ACID Transactions**: Full transactional consistency across all data models
- **🚀 High Performance**: LSM-Tree architecture optimized for write-intensive workloads
- **🔍 Hybrid Queries**: Combine relational filters, graph traversals, and vector similarity in single queries
- **📊 Advanced Indexing**: Secondary, Range, Geo, Full-text, TTL, and HNSW vector indexes
- **🔐 Security & Compliance**: Audit logging, PII management, column-level encryption (planned)
- **📈 Observability**: Prometheus metrics, OpenTelemetry tracing, detailed profiling

## 📋 Table of Contents

- [Quick Start](#quick-start)
- [Installation](#installation)
- [Architecture Overview](#architecture-overview)
- [Container Images](#container-images)
- [API Examples](#api-examples)
- [Configuration](#configuration)
- [Performance](#performance)
- [Documentation](#documentation)
- [Development](#development)
- [Contributing](#contributing)

## 🏗️ Architecture Overview

ThemisDB implements a true multi-model database based on three key architectural principles:

1. **Canonical Storage**: All data (relational, graph, vector, document) is stored as "Base Entities" in RocksDB
2. **Projection Layers**: Read-optimized indexes (secondary, graph adjacency, HNSW vector) enable fast queries
3. **Hybrid Query Engine**: Advanced Query Language (AQL) unifies operations across all data models

For a detailed technical analysis, see [docs/technical_architecture_analysis.md](docs/technical_architecture_analysis.md).

**Key Components:**
- **Storage Engine**: RocksDB LSM-Tree with ACID transactions (TransactionDB variant)
- **Query Language**: AQL (Advanced Query Language) - declarative, multi-model
- **Indexes**: Secondary, Range, Geo, Full-text, TTL, HNSW (persistent vector index)
- **Execution**: Task-based parallelism (Intel TBB), Apache Arrow for analytics
- **Observability**: OpenTelemetry tracing, Prometheus metrics

## Quick Start

```powershell
# 1. Clone and setup
git clone <repository-url>
cd THEMIS
.\setup.ps1

# 2. Build
.\build.ps1

### Container Images (GHCR + Docker Hub)

Images werden bei jedem Push auf `main` automatisch gebaut und veröffentlicht.

- GitHub Container Registry (empfohlen):
  - Repo: `ghcr.io/makr-code/themis`
  - Tags:
    - Multi-Arch Manifeste: `latest`, `g<shortsha>`
    - Arch-spezifisch: `latest-x64-linux`, `latest-arm64-linux`, sowie `g<shortsha>-<triplet>`
- Docker Hub (optional, falls Secrets gesetzt):
  - Repo: `themisdb/themis`
  - Gleiche Tag-Strategie wie oben

Pull-Beispiele:

```bash

# 3. Start server
.\build\Release\themis_server.exe

# 4. Test health endpoint
curl http://localhost:8765/health
```

**5 Minute Tutorial:**

Run-Beispiel (lokal):

```bash

```powershell
# Create an entity
curl -X PUT http://localhost:8765/entities/users:alice `
  -H "Content-Type: application/json" `

QNAP Compose:

```bash
  -d '{"blob":"{\"name\":\"Alice\",\"age\":30,\"city\":\"Berlin\"}"}'

# Create index for queries
curl -X POST http://localhost:8765/index/create `
  -H "Content-Type: application/json" `
  -d '{"table":"users","column":"city"}'

# Query by index
curl -X POST http://localhost:8765/query `
  -H "Content-Type: application/json" `
  -d '{"table":"users","predicates":[{"column":"city","value":"Berlin"}],"return":"entities"}'

# View metrics
curl http://localhost:8765/metrics
```

## Architecture

- **Canonical Storage:** RocksDB-basierter LSM-Tree für alle Base Entities
- **Projection Layers:**
  - Relational: Sekundärindizes für SQL-ähnliche Queries
  - Graph: Adjazenzindizes für Traversals (BFS/Dijkstra/A*)
  - Vector: HNSW Index (L2; Cosine/Dot geplant)
  - Document/Filesystem: JSON + Filesystem-Layer (geplant)
- **Query Engine:** Parallelisierung mit Intel TBB
- **Observability:** /stats, /metrics (Prometheus), RocksDB-Statistiken

## Prerequisites

- CMake 3.20 or higher
- C++20 compatible compiler (MSVC 2019+, GCC 11+, Clang 14+)
- vcpkg package manager
- Windows 10/11 (or Linux with appropriate modifications)

## Building

### 1. Install vcpkg

```powershell
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
$env:VCPKG_ROOT = (Get-Location).Path
```

### vcpkg Baseline (reproducible builds)

Dieses Repository nutzt den vcpkg Manifest-Modus mit einer festen Baseline für reproduzierbare Builds. Die Baseline ist in `vcpkg.json` unter `"builtin-baseline"` auf einen 40‑Hex Commit von `microsoft/vcpkg` gepinnt.

- Aktuelle Baseline (Stand): siehe `vcpkg.json` → `builtin-baseline`
- Warum? Stabile, deterministische Abhängigkeitsauflösung in CI und lokal

Baseline aktualisieren:

```powershell
# Im Repo-Root ausführen
vcpkg x-update-baseline
# Danach die geänderte vcpkg.json committen
```

Alternativ kann die Eigenschaft `builtin-baseline` manuell auf einen gewünschten vollen Commit‑Hash aus https://github.com/microsoft/vcpkg gesetzt werden.

### 2. Build the project

```powershell
cd c:\VCC\THEMIS
mkdir build
cd build

# Configure with vcpkg
cmake .. -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

# Build
cmake --build . --config Release

# Run tests
# 4. Test health endpoint
```

### Linux/macOS (Shell)

```bash
# 1. Setup (nur einmal)
./setup.sh
./build.sh


### Build Options

- `-DTHEMIS_BUILD_TESTS=ON/OFF` - Build unit tests (default: ON)
- `-DTHEMIS_BUILD_BENCHMARKS=ON/OFF` - Build benchmarks (default: ON)
- `-DTHEMIS_ENABLE_GPU=ON/OFF` - Enable GPU acceleration (default: OFF)
- `-DTHEMIS_ENABLE_ASAN=ON/OFF` - Enable AddressSanitizer (default: OFF)
 - `-DTHEMIS_STRICT_BUILD=ON/OFF` - Treat warnings as errors (default: OFF)

### GPU Support (Optional)

To enable GPU-accelerated vector search with Faiss:

```powershell
cmake .. -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
         -DTHEMIS_ENABLE_GPU=ON
```

## Project Structure

```
THEMIS/
├── CMakeLists.txt          # Main build configuration
├── vcpkg.json              # Dependency manifest
├── README.md               # This file
├── todo.md                 # Development roadmap
├── include/                # Public headers
│   ├── storage/            # Storage layer
│   ├── index/              # Index projections
│   ├── query/              # Query engine
│   └── utils/              # Utilities
├── src/                    # Implementation
│   ├── storage/            # RocksDB wrapper, Base Entity
│   ├── index/              # Secondary, Graph, Vector indexes
│   ├── query/              # Query parser, optimizer, executor
│   ├── api/                # HTTP/gRPC server
│   └── main.cpp            # Server entry point
├── tests/                  # Unit tests
├── benchmarks/             # Performance benchmarks
└── build/                  # Build output (generated)
```

## Indizes & Wartung

- Leitfaden Sekundärindizes: siehe `docs/indexes.md`
  - Equality/Unique, Composite, Range, Sparse, Geo, TTL, Fulltext
  - Key-Schemata und Code-Beispiele
- Statistiken & Wartung: siehe `docs/index_stats_maintenance.md`
  - `getIndexStats`, `getAllIndexStats`
  - `rebuildIndex`, `reindexTable`
  - TTL-Cleanup und Performance-Hinweise

## Content Ingestion (v0)

- Bulk-Ingestion von bereits vorverarbeiteten Inhalten (Content/Chunks/Edges/Blob): siehe `docs/content/ingestion.md`
  - Endpunkte: `POST /content/import`, `GET /content/{id}`, `GET /content/{id}/chunks`, `GET /content/{id}/blob`
  - Beispielpayloads und Felderläuterungen sind in der Doku enthalten.

## Dependencies

Core libraries managed via vcpkg:

- **RocksDB** - LSM-Tree storage engine
- **simdjson** - High-performance JSON parsing
- **Intel TBB** - Task-based parallelization
- **Apache Arrow** - Columnar data format for analytics
- **HNSWlib** - Approximate nearest neighbor search
- **Boost.Asio/Beast** - Async I/O and HTTP server
- **spdlog** - Fast logging library
- **Google Test** - Unit testing framework
- **Google Benchmark** - Performance benchmarking

## Running

After building, start the server:

```powershell
# YAML config (recommended)
.\build\Release\themis_server.exe --config config.yaml

# Or JSON
.\build\Release\themis_server.exe --config config.json
```

**Configuration Format:** THEMIS supports both YAML and JSON configuration files. YAML is recommended for better readability. See `config/config.json` or create `config.yaml` for examples.

### Docker

Schnellstart mit Docker (Linux-Container):

```bash
# Build Image
docker build -t vccdb:latest .

# Start mit Compose
docker compose up --build
```

Der Container lauscht auf Port 8765 und nutzt `/data` als Volume. Eine Beispielkonfiguration liegt in `config/config.json` und wird innerhalb des Containers nach `/etc/vccdb/config.json` gemountet.

### HTTP API (Auszug)

- Healthcheck:
  - GET `http://localhost:8765/health`
- Observability:
  - GET `http://localhost:8765/stats` (Server- & RocksDB-Statistiken)
- Entities (Schlüsselnotation: `table:pk`):
  - PUT `http://localhost:8765/entities/table:pk` mit Body `{ "blob": "{...json...}" }`
  - GET `http://localhost:8765/entities/table:pk`
  - DELETE `http://localhost:8765/entities/table:pk`
- Sekundärindizes:
  - POST `http://localhost:8765/index/create` Body `{ "table": "users", "column": "age" }` (Equality-Index)
  - POST `http://localhost:8765/index/create` Body `{ "table": "users", "column": "age", "type": "range" }` (Range-Index)
  - POST `http://localhost:8765/index/drop` Body `{ "table": "users", "column": "age" }` (Equality-Index löschen)
  - POST `http://localhost:8765/index/drop` Body `{ "table": "users", "column": "age", "type": "range" }` (Range-Index löschen)
- Graph-Traversierung:
  - POST `http://localhost:8765/graph/traverse` Body `{ "start_vertex": "user1", "max_depth": 3 }`

- Konfiguration:
  - GET `http://localhost:8765/config/content-filters` / PUT zum Laden/Speichern eines Filter‑Schemas (z. B. Feld‑Mappings für Content‑Suche)
  - GET `http://localhost:8765/config/edge-weights` / PUT zum Konfigurieren von Kanten‑Gewichten (Standardgewichte für `parent`, `next`, `prev`)

Beispiel: Edge‑Gewichte setzen

```powershell
curl -X PUT http://localhost:8765/config/edge-weights `
  -H "Content-Type: application/json" `
  -d '{
    "weights": { "parent": 1.0, "next": 0.8, "prev": 1.2 }
  }'
```

Die Gewichte werden beim Ingest neuer Kanten angewendet (Felder `_type` und `_weight` in der Kanten‑Entity). Dijkstra‑basierte Pfadkosten nutzen diese `_weight`‑Werte.

### Content Import

Endpoint: `POST /content/import`

Importiert vorverarbeitete Content-Daten (Metadaten, Chunks, Embeddings, Kanten) direkt in die Datenbank. Die Datenbank führt **keine** Ingestion durch – alle Daten müssen bereits strukturiert sein.

**Request-Body**:

```json
{
  "content": {
    "id": "doc123",
    "mime_type": "application/pdf",
    "filename": "report.pdf",
    "size_bytes": 1024000,
    "created_at": "2024-01-15T10:30:00Z",
    "metadata": {
      "author": "John Doe",
      "title": "Annual Report"
    }
  },
  "chunks": [
    {
      "id": "chunk_1",
      "content_id": "doc123",
      "chunk_index": 0,
      "text": "This is the first paragraph...",
      "start_offset": 0,
      "end_offset": 145,
      "embedding": [0.12, -0.34, 0.56, ...],
      "metadata": {
        "page": 1,
        "section": "Introduction"
      }
    },
    {
      "id": "chunk_2",
      "content_id": "doc123",
      "chunk_index": 1,
      "text": "The second paragraph continues...",
      "start_offset": 146,
      "end_offset": 298,
      "embedding": [0.23, -0.11, 0.78, ...],
      "metadata": {
        "page": 1,
        "section": "Introduction"
      }
    }
  ],
  "edges": [
    {
      "id": "edge_parent_1",
      "_from": "chunk_1",
      "_to": "doc123",
      "_type": "parent",
      "_weight": 1.0
    },
    {
      "id": "edge_next_1",
      "_from": "chunk_1",
      "_to": "chunk_2",
      "_type": "next",
      "_weight": 0.8
    }
  ],
  "blob": "optional base64 or raw binary data..."
}
```

**Felder**:
- `content` (erforderlich): Content-Metadaten mit mindestens `id`, `mime_type`, `filename`
- `chunks` (optional): Array von Chunk-Metadaten mit Pre-generierten Embeddings
- `edges` (optional): Array von Graph-Kanten (nutzt konfigurierte Edge-Gewichte falls `_weight` fehlt)
- `blob` (optional): Rohdaten des Contents (wird unter `content_blob:<id>` gespeichert)

**Response**:

```json
{
  "status": "success",
  "content_id": "doc123"
}
```

**Wichtig**: Alle Embeddings, Chunks und Kanten müssen von externen Tools vorverarbeitet werden. Die Datenbank speichert nur die bereitgestellten Daten.

- AQL Query API:
  - POST `http://localhost:8765/query/aql`
  - Unterstützt FOR/FILTER/SORT/LIMIT/RETURN sowie Traversals `FOR v,e,p IN min..max OUTBOUND start GRAPH 'g'`
  - EXPLAIN/PROFILE via `"explain": true` inkl. Traversal‑Metriken (siehe `docs/aql_explain_profile.md`)

**Hinweis**: Beim PUT/DELETE werden Sekundärindizes und Graph-Indizes automatisch aktualisiert, sofern für die betroffenen Spalten/Kanten Indizes existieren. Range-Indizes können unabhängig von Equality-Indizes erstellt werden.

### Query API

Endpoint: `POST /query`

Request-Body:

```json
{
  "table": "users",
  "predicates": [
    { "column": "age", "value": "30" },
    { "column": "city", "value": "Berlin" }
  ],
  "range": [
    {
      "column": "salary",
      "gte": "50000",
      "lte": "80000",
      "includeLower": true,
      "includeUpper": true
    }
  ],
  "order_by": {
    "column": "salary",
    "desc": false,
    "limit": 10
  },
  "optimize": true,
  "return": "keys",
  "allow_full_scan": false,
  "explain": false
}
```

Felder:
- **table**: Zieltabelle.
- **predicates**: AND-verknüpfte Gleichheitsfilter. Werte sind Strings; Typkonvertierung geschieht index-/parserseitig.
- **range** (optional): Liste von Range-Prädikaten (gte/lte) für Spalten mit Range-Index. Jedes Range-Prädikat wird ebenfalls per AND verknüpft.
  - `column`: Spalte mit Range-Index.
  - `gte` (optional): Untere Grenze (greater than or equal).
  - `lte` (optional): Obere Grenze (less than or equal).
  - `includeLower` (optional, default: true): Untere Grenze inklusiv?
  - `includeUpper` (optional, default: true): Obere Grenze inklusiv?
- **order_by** (optional): Sortierung über Range-Index. Erfordert Range-Index auf der angegebenen Spalte.
  - `column`: Spalte zum Sortieren.
  - `desc` (optional, default: false): Absteigend sortieren?
  - `limit` (optional, default: 1000): Maximale Anzahl zurückgegebener Keys.
- **optimize**: Nutzt den Optimizer, um selektive Prädikate zuerst auszuwerten (empfohlen: true).
- **return**: "keys" oder "entities".
- **allow_full_scan**: Wenn kein Index existiert, per Full-Scan fallbacken (Standard: false). Achtung: kann teuer sein.
- **explain**: Gibt den Ausführungsplan zurück (Modus, Reihenfolge, Schätzungen), sofern verfügbar.

**Hinweis**: Range-Prädikate und ORDER BY erfordern einen Range-Index auf der jeweiligen Spalte:

```bash
# Range-Index erstellen
curl -X POST http://localhost:8765/index/create \
  -H "Content-Type: application/json" \
  -d '{"table":"users","column":"salary","type":"range"}'
```

Antwort (keys mit Range/ORDER BY):

```json
{
  "table": "users",
  "count": 3,
  "keys": ["user1", "user2", "user3"],
  "plan": {
    "mode": "range_aware",
    "order": [{"column":"age","value":"30"},{"column":"city","value":"Berlin"}],
    "estimates": [
      {"column":"age","value":"30","estimatedCount":10,"capped":true},
      {"column":"city","value":"Berlin","estimatedCount":5,"capped":false}
    ]
  }
}
```

Antwort (entities):

```json
{
  "table": "users",
  "count": 1,
  "entities": ["{\"name\":\"Alice\",\"age\":30,\"city\":\"Berlin\"}"],
  "plan": { "mode": "full_scan_fallback" }
}
```

**Beispiele**:

1. **Gleichheitsfilter mit Range-Prädikat**:
```json
{
  "table": "employees",
  "predicates": [{"column": "department", "value": "Engineering"}],
  "range": [{"column": "salary", "gte": "60000", "lte": "100000"}],
  "return": "keys"
}
```

2. **ORDER BY ohne weitere Prädikate (Top-N)**:
```json
{
  "table": "products",
  "order_by": {"column": "price", "desc": true, "limit": 5},
  "return": "entities"
}
```

3. **Range mit exklusiven Grenzen**:
```json
{
  "table": "users",
  "range": [{"column": "age", "gte": "18", "lte": "65", "includeLower": true, "includeUpper": false}],
  "order_by": {"column": "age", "desc": false, "limit": 100},
  "return": "keys"
}
```

## Graph API

Endpoint: `POST /graph/traverse`

**Beschreibung**: Führt eine Breadth-First-Search (BFS) Traversierung ab einem Startknoten aus. Die Graph-Indizes (Outdex/Index) werden automatisch bei PUT/DELETE von Kanten aktualisiert.

Request-Body:

```json
{
  "start_vertex": "user1",
  "max_depth": 3
}
```

Felder:
- **start_vertex**: Primärschlüssel des Startknotens
- **max_depth**: Maximale Traversierungstiefe (0 = nur Startknoten)

Antwort:

```json
{
  "start_vertex": "user1",
  "max_depth": 3,
  "visited_count": 5,
  "visited": ["user1", "user2", "user3", "user4", "user5"]
}
```

**Beispiel**: Graph-Kanten erstellen und traversieren

```bash
# Kante erstellen: user1 -> user2
curl -X POST http://localhost:8765/entities \
  -H "Content-Type: application/json" \
  -d '{"key":"edge:e1","blob":"{\"id\":\"e1\",\"_from\":\"user1\",\"_to\":\"user2\"}"}'

# Kante erstellen: user2 -> user3
curl -X POST http://localhost:8765/entities \
  -H "Content-Type: application/json" \
  -d '{"key":"edge:e2","blob":"{\"id\":\"e2\",\"_from\":\"user2\",\"_to\":\"user3\"}"}'

# Traversierung von user1 aus (Tiefe 2)
curl -X POST http://localhost:8765/graph/traverse \
  -H "Content-Type: application/json" \
  -d '{"start_vertex":"user1","max_depth":2}'
```

**Hinweis**: Kanten müssen als Entities mit Feldern `id`, `_from`, `_to` gespeichert werden. Die Graph-Indizes werden automatisch erstellt und aktualisiert.

## Observability (/stats, /metrics)

- Endpoint: `GET /stats`
  - Liefert zwei Bereiche:
    - `server`: Laufzeitmetriken (uptime_seconds, total_requests, total_errors, queries_per_second, threads)
    - `storage`: RocksDB-Statistiken als strukturierte Werte unter `rocksdb` sowie vollständige Roh-Statistiken in `raw_stats`

Beispiel-Antwort (gekürzt):

```json
{
  "server": {
    "uptime_seconds": 42,
    "total_requests": 3,
    "total_errors": 0,
    "queries_per_second": 0.07,
    "threads": 8
  },
  "storage": {
    "rocksdb": {
      "block_cache_usage_bytes": 87,
      "block_cache_capacity_bytes": 1073741824,
      "estimate_num_keys": 0,
      "memtable_size_bytes": 2048,
      "cur_size_all_mem_tables_bytes": 2048,
      "estimate_pending_compaction_bytes": 0,
      "num_running_compactions": 0,
      "num_running_flushes": 0,
      "files_per_level": { "L0": 0, "L1": 0, "L2": 0, "L3": 0, "L4": 0, "L5": 0, "L6": 0 },
      "block_cache_hit": 0,
      "block_cache_miss": 0,
      "cache_hit_rate_percent": 0.0,
      "bytes_written": 0,
      "bytes_read": 0,
      "compaction_keys_dropped": 0
    },
    "raw_stats": "...vollständiger RocksDB-Stats-Text..."
  }
}
```

- Endpoint: `GET /metrics`
  - Prometheus Text Exposition Format (Content-Type: `text/plain; version=0.0.4`)
  - Enthält z. B.:
    - `process_uptime_seconds` (gauge)
    - `vccdb_requests_total` (counter)
    - `vccdb_errors_total` (counter)
    - `vccdb_qps` (gauge)
    - `vccdb_cursor_anchor_hits_total` (counter)
    - `vccdb_range_scan_steps_total` (counter)
    - `vccdb_page_fetch_time_ms_*` (histogram: `bucket`, `sum`, `count`)
    - `rocksdb_block_cache_usage_bytes` / `rocksdb_block_cache_capacity_bytes` (gauges)
    - `rocksdb_estimate_num_keys`, `rocksdb_pending_compaction_bytes`, `rocksdb_memtable_size_bytes` (gauges)
    - `rocksdb_files_level{level="L0".."L6"}` (gauge per Level)

Prometheus Scrape Beispiel:

```yaml
scrape_configs:
  - job_name: vccdb
    static_configs:
      - targets: ['localhost:8765']
    metrics_path: /metrics
```

Hinweise:
- Die Statistiken nutzen RocksDB DBStatistics und Property-Getter. Das Hinzufügen der Statistik-Erhebung verursacht geringen Overhead und ist im Standard aktiv.
- `raw_stats` ist ein menschenlesbarer Textblock (z. B. Compaction-, Cache- und DB-Stats), nützlich für Debugging und Tuning.

Troubleshooting:
- Fehler „Database not open": Prüfen Sie `storage.rocksdb_path` in `config.yaml` oder `config.json`. Unter Windows relative Pfade wie `./data/themis_server` verwenden.
- Kein Zugriff auf `/stats` oder `/metrics`: Server läuft? `GET /health` prüfen. Firewall/Port 8765 freigeben.

## Configuration

THEMIS supports **YAML** (recommended) and **JSON** configuration files.

**YAML Example** (`config.yaml`):

```yaml
storage:
  rocksdb_path: ./data/rocksdb
  memtable_size_mb: 256
  block_cache_size_mb: 1024
  enable_blobdb: true
  compression:
    default: lz4
    bottommost: zstd

server:
  host: 0.0.0.0
  port: 8765
  worker_threads: 8

vector_index:
  engine: hnsw
  hnsw_m: 16
  hnsw_ef_construction: 200
  use_gpu: false

features:
  semantic_cache: true
  llm_store: true
  cdc: true
```

**JSON Example** (`config.json`):

```json
{
  "storage": {
    "rocksdb_path": "./data/rocksdb",
    "memtable_size_mb": 256,
    "block_cache_size_mb": 1024,
    "enable_blobdb": true,
    "compression": {
      "default": "lz4",
      "bottommost": "zstd"
    }
  },
  "server": {
    "host": "0.0.0.0",
    "port": 8765,
    "worker_threads": 8
  },
  "vector_index": {
    "engine": "hnsw",
    "hnsw_m": 16,
    "hnsw_ef_construction": 200,
    "use_gpu": false
  }
}
```

**Policy Configuration:**

Policies for fine-grained authorization can be configured in YAML or JSON:
- `config/policies.yaml` (recommended) or `config/policies.json`
- See `docs/security/policies.md` for details

## Documentation

- Base Entity Layer — `docs/base_entity.md`
- Memory Tuning — `docs/memory_tuning.md`
- AQL Profiling & Metriken — `docs/aql_explain_profile.md`
- Pfad‑Constraints (Design) — `docs/path_constraints.md`
- Change Data Capture (CDC) — `docs/cdc.md`
  
Hinweis: Weitere Dokumente (Architecture, Deployment, Indexes, OpenAPI) werden nachgezogen; siehe auch `todo.md`.

## Development Status

Siehe `todo.md` für Roadmap und Prioritäten. Eine thematische Übersicht steht am Anfang der Datei.

## Performance

### Benchmarks

Run benchmarks after building:

```powershell
# CRUD operations benchmark
.\build\Release\bench_crud.exe

# Index rebuild benchmark (100K entities, 7 index types)
.\build\Release\bench_index_rebuild.exe

# Query performance benchmark
.\build\Release\bench_query.exe

# Vector search benchmark
.\build\Release\bench_vector_search.exe
```

**Typical Results (Release build, Windows 11, i7-12700K):**

| Operation | Throughput | Latency (p50) | Latency (p99) |
|-----------|------------|---------------|---------------|
| Entity PUT | 45,000 ops/s | 0.02 ms | 0.15 ms |
| Entity GET | 120,000 ops/s | 0.008 ms | 0.05 ms |
| Indexed Query | 8,500 queries/s | 0.12 ms | 0.85 ms |
| Graph Traverse (depth=3) | 3,200 ops/s | 0.31 ms | 1.2 ms |
| Vector ANN (k=10) | 1,800 queries/s | 0.55 ms | 2.1 ms |
| Index Rebuild (100K entities) | 12,000 entities/s | - | - |

**RocksDB Compression Benchmarks:**

| Algorithm | Write Throughput | Compression Ratio | Use Case |
|-----------|------------------|-------------------|----------|
| None | 34.5 MB/s | 1.0x | Development only |
| LZ4 | 33.8 MB/s | 2.1x | Default (balanced) |
| ZSTD | 32.3 MB/s | 2.8x | Bottommost level (storage optimization) |

See [docs/memory_tuning.md](docs/memory_tuning.md) for detailed compression configuration.

### Query Parallelization

The Query Engine uses Intel TBB for parallel execution:
- **Batch Processing**: Parallel entity loading for result sets >100 entities (batch size: 50)
- **Index Scans**: Parallel index lookups across multiple predicates
- **Throughput**: Up to 3.5x speedup on 8-core systems for complex queries

## API Examples

### Entity CRUD

```powershell
# Create
curl -X PUT http://localhost:8765/entities/products:p1 `
  -H "Content-Type: application/json" `
  -d '{"blob":"{\"name\":\"Laptop\",\"price\":999,\"stock\":42}"}'

# Read
curl http://localhost:8765/entities/products:p1

# Update
curl -X PUT http://localhost:8765/entities/products:p1 `
  -H "Content-Type: application/json" `
  -d '{"blob":"{\"name\":\"Laptop\",\"price\":899,\"stock\":38}"}'

# Delete
curl -X DELETE http://localhost:8765/entities/products:p1
```

### Index Management

```powershell
# Create equality index
curl -X POST http://localhost:8765/index/create `
  -H "Content-Type: application/json" `
  -d '{"table":"users","column":"email"}'

# Create range index for sorting/filtering
curl -X POST http://localhost:8765/index/create `
  -H "Content-Type: application/json" `
  -d '{"table":"products","column":"price","type":"range"}'

# Get index statistics
curl http://localhost:8765/index/stats

# Rebuild specific index
curl -X POST http://localhost:8765/index/rebuild `
  -H "Content-Type: application/json" `
  -d '{"table":"users","column":"email"}'

# Reindex entire table (all indexes)
curl -X POST http://localhost:8765/index/reindex `
  -H "Content-Type: application/json" `
  -d '{"table":"users"}'
```

### Advanced Queries

```powershell
# Range query with sorting
curl -X POST http://localhost:8765/query `
  -H "Content-Type: application/json" `
  -d '{
    "table":"products",
    "range":[{"column":"price","gte":"100","lte":"500"}],
    "order_by":{"column":"price","desc":false,"limit":10},
    "return":"entities"
  }'

# Multi-predicate query with optimization
curl -X POST http://localhost:8765/query `
  -H "Content-Type: application/json" `
  -d '{
    "table":"employees",
    "predicates":[
      {"column":"department","value":"Engineering"},
      {"column":"level","value":"Senior"}
    ],
    "optimize":true,
    "return":"keys",
    "explain":true
  }'
```

### Graph Operations

```powershell
# Create graph edges (social network example)
curl -X PUT http://localhost:8765/entities/edges:e1 `
  -H "Content-Type: application/json" `
  -d '{"blob":"{\"id\":\"e1\",\"_from\":\"user:alice\",\"_to\":\"user:bob\",\"type\":\"follows\"}"}'

curl -X PUT http://localhost:8765/entities/edges:e2 `
  -H "Content-Type: application/json" `
  -d '{"blob":"{\"id\":\"e2\",\"_from\":\"user:bob\",\"_to\":\"user:charlie\",\"type\":\"follows\"}"}'

# Traverse graph (BFS)
curl -X POST http://localhost:8765/graph/traverse `
  -H "Content-Type: application/json" `
  -d '{"start_vertex":"user:alice","max_depth":3}'
```

### Transactions (ACID)

THEMIS supports session-based transactions with atomic commits across all index types (Secondary, Graph, Vector).

```powershell
# 1. Begin transaction
$response = curl -s -X POST http://localhost:8765/transaction/begin `
  -H "Content-Type: application/json" `
  -d '{"isolation":"read_committed"}' | ConvertFrom-Json
$txnId = $response.transaction_id

# 2. Perform operations (currently via C++ API only, HTTP integration pending)
# C++ Example:
# auto txn = tx_manager->getTransaction($txnId);
# txn->putEntity("users", user1);
# txn->addEdge(edge);
# txn->addVector(document, "embedding");

# 3. Commit transaction
curl -X POST http://localhost:8765/transaction/commit `
  -H "Content-Type: application/json" `
  -d "{\"transaction_id\":$txnId}"

# Or rollback
curl -X POST http://localhost:8765/transaction/rollback `
  -H "Content-Type: application/json" `
  -d "{\"transaction_id\":$txnId}"

# 4. View transaction statistics
curl http://localhost:8765/transaction/stats
```

**Features:**
- **Atomicity**: All-or-nothing commits across all indexes
- **Isolation Levels**: `read_committed` (default), `snapshot`
- **Multi-Index Support**: Secondary, Graph, Vector indexes in single transaction
- **Statistics**: Success rate, durations, active count

**Documentation:** See [docs/transactions.md](docs/transactions.md) for detailed guide including:
- Transaction workflows and best practices
- C++ API examples (Direct & Session-based)
- Error handling strategies
- Performance considerations
- Known limitations (Vector cache consistency)

### Monitoring

```powershell
# Server configuration
curl http://localhost:8765/config

# Server + RocksDB statistics
curl http://localhost:8765/stats

# Prometheus metrics (includes transaction stats)
curl http://localhost:8765/metrics
```

## Documentation

- **[Architecture Overview](docs/architecture.md)** - System design and components
- **[Deployment Guide](docs/deployment.md)** - Production setup and configuration
- **[Transaction Management](docs/transactions.md)** - ACID transactions, isolation levels, best practices
- **[Base Entity](docs/base_entity.md)** - Entity serialization and storage
- **[Memory Tuning](docs/memory_tuning.md)** - Performance optimization
- **[OpenAPI Specification](docs/openapi.yaml)** - Complete REST API reference
- **[Change Data Capture (CDC)](docs/cdc.md)** - Changefeed API, Checkpointing, Backpressure, Retention

## License

MIT License - See LICENSE file for details.

## Contributing

This is currently a research/prototype project. Contributions are welcome!

### Code Quality

ThemisDB maintains high code quality standards through automated CI checks:

- **Static Analysis**: clang-tidy with modern C++17 best practices
- **Linting**: cppcheck for additional C++ quality checks
- **Code Coverage**: Comprehensive test coverage reporting
- **Secret Scanning**: Gitleaks prevents accidental credential commits

**Run checks locally before pushing:**

```bash
# Linux/macOS
./scripts/check-quality.sh

# Windows
.\scripts\check-quality.ps1
```

**View detailed guide:** [Code Quality Documentation](docs/code_quality.md)

**CI Workflows:**
- `.github/workflows/ci.yml` - Build and test
- `.github/workflows/code-quality.yml` - Static analysis, linting, coverage, secret scanning
- `.github/workflows/coverage-badge.yml` - Coverage reporting and badge generation

## References

Based on the architectural analysis in:
- `Hybride Datenbankarchitektur C++_Rust.txt`
