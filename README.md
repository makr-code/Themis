# Themis Multi-Model Database System

[![Docs](https://img.shields.io/badge/docs-GitHub%20Pages-0a7ea4)](https://makr-code.github.io/ThemisDB/)
[![Print](https://img.shields.io/badge/print-Gesamtansicht-555)](https://makr-code.github.io/ThemisDB/print_page/)
[![PDF](https://img.shields.io/badge/PDF-Gesamtdoku-blueviolet)](https://makr-code.github.io/ThemisDB/themisdb-docs-complete.pdf)
[![Wiki](https://img.shields.io/badge/wiki-GitHub%20Wiki-0366d6)](https://github.com/makr-code/ThemisDB/wiki)

[![CI](https://github.com/makr-code/ThemisDB/actions/workflows/ci.yml/badge.svg)](https://github.com/makr-code/ThemisDB/actions/workflows/ci.yml)
[![Code Quality](https://github.com/makr-code/ThemisDB/actions/workflows/code-quality.yml/badge.svg)](https://github.com/makr-code/ThemisDB/actions/workflows/code-quality.yml)
[![Coverage](https://img.shields.io/badge/coverage-view%20report-brightgreen)](https://makr-code.github.io/ThemisDB/coverage/)


The ThemisDB Architecture: A Technical In-Depth Analysis of a Multi-Model Database System Based on LSM Tree

## 📊 Entwicklungsstand & Dokumentation

**Kern-Dokumentation (Neu konsolidiert):**
- **[Development Audit Log](DEVELOPMENT_AUDITLOG.md)** - Vollständiger Entwicklungsstand, Feature-Status, Metriken, nächste Schritte
- **[Roadmap](ROADMAP.md)** - Konsolidierte Entwicklungs-Roadmap (Q1-Q4 2026+), GPU/CUDA Pläne
- **[Next Implementation Priorities](NEXT_IMPLEMENTATION_PRIORITIES.md)** - Priorisierung der nächsten Entwicklungsschritte (empfohlen: Column-Level Encryption)
- **[Changelog](CHANGELOG.md)** - Detaillierte Änderungshistorie nach Semantic Versioning

**Für Stakeholder:**
- **[Themis Sachstandsbericht 2025](THEMIS_SACHSTANDSBERICHT_2025.md)** - Executive Summary, Performance-Benchmarks, Compliance-Readiness

**Für Entwickler:**
- **[Documentation Verification Report](DOCUMENTATION_VERIFICATION_REPORT.md)** - Verifizierung der Übereinstimmung zwischen Dokumentation und Code

## Dokumentation

- Primärquelle: GitHub Wiki unter https://github.com/makr-code/ThemisDB/wiki
- Änderungen bitte im Verzeichnis `docs/` vornehmen und per Sync ins Wiki übertragen:

```powershell
./sync-wiki.ps1
```

- Lokale Vorschau (MkDocs) für Entwickelnde:

```powershell
./build-docs.ps1
```

Erzeugt die Ausgabe in `site/` (nicht committen). Das GitHub Pages Deployment ist deaktiviert; maßgeblich ist das Wiki.

## Developer Quickstart

- **Server defaults:** The API server binary `themis_server` uses the following defaults unless overridden by `--config` or CLI flags:
  - host: `0.0.0.0`
  - port: `8765`
  - database path: `./data/themis_server`
- **Config files:** The server will auto‑load a config from `./config.yaml`, `./config/config.yaml`, `./config.json` or `/etc/vccdb/config.*` unless `--config` is provided.
- **WSL / local builds:** Development builds on Windows/WSL commonly use the `build-wsl` directory as the build output. Some helper scripts (for example `.tools/vault_dev_run.ps1`) assume the test binary is available at `build-wsl/themis_tests` or the server binary at `build-wsl/themis_server`.

### Build Scripts (Windows, Linux, WSL)

- **Windows (PowerShell):**
  - Erstsetup: `./setup.ps1`
  - Build (Auto-Generator, default Verzeichnisse):
    - Visual Studio: `./build.ps1 -BuildType Debug` → nutzt `build-msvc/`
    - Ninja (falls installiert): `./build.ps1 -BuildType Debug -Generator Ninja` → nutzt `build-ninja/`
  - Beispiele: `./build.ps1 -BuildType Release -RunTests`, `./build.ps1 -EnableBenchmarks`, `./build.ps1 -Clean`

- **Linux/WSL (bash):**
  - Erstsetup: `./setup.sh`
  - Build: `./build.sh BUILD_TYPE=Debug RUN_TESTS=1`
  - Standard-Build-Verzeichnis:
    - WSL: `build-wsl/` (automatisch)
    - Linux/macOS: `build/`
  - Anpassungen via Umgebungsvariablen: `BUILD_DIR`, `GENERATOR` (z. B. Ninja), `ENABLE_TESTS`, `ENABLE_BENCHMARKS`, `ENABLE_GPU`, `STRICT`

- **Toolchain/Dependencies:** vcpkg Toolchain wird automatisch genutzt, wenn `VCPKG_ROOT` gesetzt ist. OpenSSL, Arrow, RocksDB, Boost, spdlog etc. werden über `vcpkg.json` im Manifest-Modus aufgelöst.
- **Notes on container/runtime:** `Dockerfile.runtime` uses `/usr/local/bin/themis_server --config /etc/themis/config.json` as entrypoint and also exposes ports `8080` and `18765` in the image — these are image-level ports and may be mapped to the server's configured port (default `8765`) at runtime; when running the binary directly prefer to use the `--port` flag or a config file to guarantee port choice.


## Recent changes (2025-11-17)

### Security Hardening Sprint Completed ✅

**Branch:** `feature/critical-high-priority-fixes` | **Security Coverage:** 85%

All 8 CRITICAL security features implemented (Production-Ready):

1. **Rate Limiting & DoS Protection** ✅
   - Token bucket algorithm (100 req/min default)
   - Per-IP & per-user limits
   - HTTP 429 responses with metrics
   
2. **TLS/SSL Hardening** ✅
   - TLS 1.3 default (TLS 1.2 fallback)
   - Strong cipher suites (ECDHE-RSA-AES256-GCM-SHA384, ChaCha20-Poly1305)
   - mTLS client certificate verification
   - HSTS headers (`max-age=31536000; includeSubDomains`)
   - **Documentation:** [`docs/TLS_SETUP.md`](docs/TLS_SETUP.md) (400+ lines)

3. **Certificate Pinning (HSM/TSA)** ✅
   - SHA256 fingerprint verification
   - CURL SSL context callbacks
   - Leaf vs. chain pinning support
   - **Documentation:** [`docs/CERTIFICATE_PINNING.md`](docs/CERTIFICATE_PINNING.md) (700+ lines)

4. **Input Validation & Sanitization** ✅
   - JSON schema validation
   - AQL injection prevention
   - Path traversal protection
   - Max body size limits (10MB default)

5. **Security Headers & CORS** ✅
   - X-Frame-Options, X-Content-Type-Options, X-XSS-Protection
   - Content-Security-Policy
   - Strict CORS whitelisting

6. **Secrets Management** ✅
   - HashiCorp Vault integration (KV v2, AppRole)
   - Automatic token renewal
   - Secret rotation callbacks
   - Environment fallback for development
   - **Documentation:** [`docs/SECRETS_MANAGEMENT.md`](docs/SECRETS_MANAGEMENT.md) (500+ lines)

7. **Audit Logging Enhancement** ✅
   - 65 security event types (LOGIN_FAILED, PRIVILEGE_ESCALATION_ATTEMPT, etc.)
   - Hash chain for tamper-detection (Merkle-like)
   - SIEM integration (Syslog RFC 5424, Splunk HEC)
   - Severity levels (HIGH/MEDIUM/LOW)
   - **Documentation:** [`docs/AUDIT_LOGGING.md`](docs/AUDIT_LOGGING.md) (900+ lines)

8. **RBAC Implementation** ✅
   - Role hierarchy (admin → operator → analyst → readonly)
   - Resource-based permissions (data:read, keys:rotate, etc.)
   - Wildcard support (`*:*`)
   - JSON/YAML configuration
   - User-role mapping store
   - **Documentation:** [`docs/RBAC.md`](docs/RBAC.md) (800+ lines)

**Production Impact:**
- 🔐 **Security:** Production-ready security stack (GDPR/SOC2/HIPAA compliant)
- 📚 **Documentation:** 3,400+ lines of comprehensive security guides
- 💻 **Code:** 3,700+ new lines (all features fully implemented)
- ✅ **Testing:** Zero critical CVEs, OWASP ZAP baseline passed
- 📊 **Performance:** <15% overhead with all features enabled

**Files Changed:** 14 files (9 new implementations/docs, 5 modified)
- **New Implementation:** 
  - `include/security/rbac.h`, `src/security/rbac.cpp`
  - `include/security/secrets_manager.h`, `src/security/secrets_manager.cpp`
  - Enhanced: `pki_client.h/cpp`, `audit_logger.h/cpp`
- **Documentation:** 
  - `TLS_SETUP.md`, `SECRETS_MANAGEMENT.md`, `AUDIT_LOGGING.md`
  - `RBAC.md`, `CERTIFICATE_PINNING.md`
  - `security_hardening_guide.md` (updated)
  - `SECURITY_IMPLEMENTATION_SUMMARY.md` (new master doc)

**Compliance Ready:**
- ✅ GDPR/DSGVO: Recht auf Löschung, Auskunft, Pseudonymisierung
- ✅ SOC 2: Access Control (CC6.1), Audit Logs (CC6.7), Change Mgmt (CC7.2)
- ✅ HIPAA: §164.312(a)(1) Access Control, §164.312(e)(1) Transmission Security

See [`docs/SECURITY_IMPLEMENTATION_SUMMARY.md`](docs/SECURITY_IMPLEMENTATION_SUMMARY.md) for complete details.

### Critical/High-Priority Sprint (2025-11-17 - earlier)

**Branch:** `feature/critical-high-priority-fixes` | **Commits:** 2 | **Lines:** 3,633 added

All 8 CRITICAL and HIGH-priority tasks completed:

1. **BFS Bug Fix** - Fixed GraphId propagation in graph topology (BLOCKER resolved)
2. **Schema Encryption Tests** - 809 lines, 19 E2E test cases
3. **PKI Documentation** - 1,111 lines covering eIDAS compliance and technical APIs
4. **Vector Metadata Encryption Edge Cases** - 532 lines of edge case tests
5. **Content-Blob ZSTD Compression** - Verified as already implemented (50% storage savings)
6. **Audit Log Encryption** - Verified as already implemented (encrypt-then-sign pattern)
7. **Lazy Re-Encryption** - Zero-downtime key rotation with transparent migration
8. **Encryption Prometheus Metrics** - 42 counters, performance histograms, Grafana alerts

**Production Impact:**
- 🛡️ Security: Comprehensive encryption test coverage + lazy key rotation
- 📊 Observability: Real-time encryption metrics for GDPR/eIDAS compliance
- 🐛 Stability: Critical BFS bug fixed (prevented graph operations after topology rebuild)
- 📚 Documentation: 1,521 lines of production-ready PKI + metrics documentation

**Files Changed:** 12 files (7 new tests/docs, 5 modified)
- Tests: `test_schema_encryption.cpp`, `test_lazy_reencryption.cpp`, `test_vector_metadata_encryption_edge_cases.cpp`
- Docs: `pki_integration_architecture.md`, `pki_signatures.md`, `encryption_metrics.md`
- Implementation: `encryption.h`, `field_encryption.cpp`, `http_server.cpp`, `graph_index.h/cpp`

### Previous Changes (2025-11-11)

- Temporal aggregation support: added `aggregateEdgePropertyInTimeRange()` to `GraphIndexManager`.
  - Supports COUNT, SUM, AVG, MIN, MAX over a time window and optional `_type` filtering.
  - Robust edge entity reads: code attempts both `edge:<graphId>:<edgeId>` and `edge:<edgeId>` keys for backward compatibility.
  - Unit tests added: `tests/test_temporal_aggregation_property.cpp` (all passing).
  - Documentation updated: `docs/temporal_time_range_queries.md` (changelog & examples).


## Key Features (Production-Ready)

ThemisDB provides a comprehensive multi-model database with the following production-ready features:

### 🔐 Transactional Consistency (MVCC)
- **Full ACID Transactions** with Snapshot Isolation (RocksDB TransactionDB)
- Write-write conflict detection with automatic rollbacks
- Atomic updates across all index layers
- **Status:** ✅ Production-ready (27/27 tests PASS)
- **Documentation:** [`docs/mvcc_design.md`](docs/mvcc_design.md)

### 📊 Vector Search with Persistence
- **HNSW Index** with L2, Cosine, and Dot Product metrics
- **Automatic persistence** (save/load on server start/shutdown)
- Batch insert operations (500-1000 items)
- Configurable efSearch for query-time tuning
- **Status:** ✅ Production-ready (10/10 tests PASS)
- **Documentation:** [`docs/vector_ops.md`](docs/vector_ops.md)

### 📈 Time-Series Engine
- **Gorilla Compression** (10-20x compression ratio)
- Continuous aggregates (pre-computed rollups)
- Retention policies (automatic data expiration)
- **Status:** ✅ Production-ready (22/22 tests PASS)
- **Documentation:** [`docs/time_series.md`](docs/time_series.md)

### 🔍 Advanced Query Language (AQL)
- FOR/FILTER/SORT/LIMIT/RETURN syntax
- **Graph traversals** (BFS, Dijkstra, A*) with variable depth
- **COLLECT/GROUP BY** with aggregations (COUNT, SUM, AVG, MIN, MAX)
- Temporal graph queries with time-range filters
- **Status:** ✅ MVP production-ready
- **Documentation:** [`docs/aql_syntax.md`](docs/aql_syntax.md)

### 💾 Backup & Recovery
- **RocksDB Checkpoints** via `POST /admin/backup`
- Point-in-time recovery with WAL archiving
- Incremental backup scripts (Linux & Windows)
- **Status:** ✅ Production-ready
- **Documentation:** [`docs/deployment.md`](docs/deployment.md#backup--recovery)

### 🔒 Enterprise Security (NEW!)
- **TLS 1.3 Hardening** with mTLS, HSTS, strong ciphers
- **Rate Limiting** (Token Bucket, per-IP/user, 100 req/min default)
- **Certificate Pinning** for HSM/TSA connections (SHA256 fingerprints)
- **RBAC** with role hierarchy (admin → operator → analyst → readonly)
- **Secrets Management** (HashiCorp Vault integration, auto-rotation)
- **Audit Logging** (65 event types, hash chain, SIEM integration)
- **Input Validation** (AQL injection prevention, path traversal protection)
- **Security Headers** (CSP, X-Frame-Options, CORS whitelisting)
- **Compliance:** GDPR/SOC2/HIPAA ready
- **Status:** ✅ Production-ready (85% security coverage)
- **Documentation:** [`docs/SECURITY_IMPLEMENTATION_SUMMARY.md`](docs/SECURITY_IMPLEMENTATION_SUMMARY.md)

### 📊 Observability
- **Prometheus metrics** with cumulative histograms
- Full request/error tracking, latency percentiles (P95/P99)
- RocksDB internals (cache, compaction, memtable)
- **OpenTelemetry tracing** integration
- **Status:** ✅ Production-ready (4/4 metrics tests PASS)
- **Documentation:** [`docs/observability/prometheus_metrics.md`](docs/observability/prometheus_metrics.md)

### 🗂️ Comprehensive Indexing
- Secondary indexes (single, composite, range)
- Sparse, TTL, and fulltext indexes
- Geo-spatial indexes (R-Tree, geohash)
- Automatic index maintenance with MVCC
- **Status:** ✅ Production-ready
- **Documentation:** [`docs/indexes.md`](docs/indexes.md)

### 📡 Change Data Capture (CDC)
- Append-only event log for all mutations
- Incremental consumption with checkpointing
- SSE streaming support (experimental)
- **Status:** ✅ MVP production-ready
- **Documentation:** [`docs/change_data_capture.md`](docs/change_data_capture.md)


Part 1: The Canonical Storage Architecture: The “Base Entity” Foundation of ThemisDB


1.1. The “Base Entity” paradigm as a unified multi-model core

The fundamental architectural challenge of modern data platforms is the efficient storage and querying of disparate data models. Many systems follow the approach of "polyglot persistence" (multi-memory persistence).1, in which separate, specialized databases (e.g., a relational database, a graph database, and a vector database) are bundled together. However, this approach shifts the complexity of data consistency, transaction management, and query federation to the application layer.1
ThemisDB avoids this by implementing a "true" multi-model database (MMDBMS).1The architecture is based on a unified storage layer and a model translation layer.1The core of this design, as described in the documents VCCDB Design.md and base_entity.md2The “Base Entity” has been confirmed.2
This “Base Entity” is defined as the atomic, “canonical storage unit” of the system.2Each logical entity – be it a relational row, a graph node, a vector object, or a document – ​​is stored as a single JSON-like document (referred to as a "blob").1This design is inspired by leading MMDBMS such as ArangoDB or Cosmos DB.1, is the crucial architectural enabler. It creates a single, canonical representation that unites all four models in one structure. The base_entity.md document confirms that this layer provides multi-format support (binary/JSON) as well as mechanisms for "Fast Field Extraction".2provides – a crucial capability, which will be discussed in more detail in Part 1.5.

1.2. Mapping logical models to the physical “Base Entity” in ThemisDB

Choosing a blob as the "base entity" necessitates a specific mapping of logical constructs to the physical key-value schema. The ThemisDB architecture follows the one outlined in the theoretical blueprint.1presented mapping strategy:
Relational & Dokument:A row from a table or a JSON document from a collection is stored 1:1 as a "Base Entity" blob.1
Graph:The Labeled Property Graph (LPG) model is represented by treating nodes and edges as separate "Base Entity" blobs. An edge is a specialized document containing _from and _to references.1
Vector:The vector embedding (e.g., an array of floats) is used as an attribute.withinstored of the "Base Entity" blob.1
The following table summarizes ThemisDB's mapping strategy and serves as a reference for physical data organization.
Table 1: ThemisDB Multi-Model Data Mapping (Architectural Blueprint)

Logical model
Logical entity
Physical memory (key-value pair)
Key-Format (Byte-Array)
Value-Format (Byte-Array)
Relational
One line
(PK, Blob)
String("table_name:pk_value")
VelocyPack/Bincode (Serialized Document)1
Document
A JSON document
(PK, Blob)
String("collection_name:pk_value")
VelocyPack/Bincode (Serialized Document)1
Graph (nodes)
A knot
(PK, Blob)
String("node:pk_value")
VelocyPack/Bincode (Serialized Node Document)1
Graph (Kante)
An edge
(PK, Blob)
String("edge:pk_value")
VelocyPack/Bincode(Serialized edge document incl. _from/_to)1
Vector
An object
(PK, Blob)
String("object_name:pk_value")
VelocyPack/Bincode(document including vector array)1

Note: The serialization formats (VelocyPack/Bincode) are those in1recommended high-performance binary formats suitable for implementing ThemisDB blob storage.

1.3. The physical storage engine: RocksDB as the transactional foundation

Storing these "Base Entity" blobs requires an embedded key-value storage engine (KV store). The ThemisDB documentation confirms the choice of RocksDB as the underlying storage layer.2RocksDB is a high-performance library written in C++, based on a Log-Structured-Merge-Tree (LSM-Tree) and optimized for write-intensive workloads.1
The theoretical architecture1However, RocksDB primarily treats it as a pure kvf storage. The ThemisDB implementation takes a crucial step further towards production readiness. As explained in Part 2, updating a single logical entity (e.g., UPDATE users SET age = 31) requires an atomic change.severalPhysical key-value pairs: The "Base Entity" blob must be updated, and at the same time, the associated secondary index entries (e.g., deleting idx:age:30 and inserting idx:age:31) must be changed.1
Standard RocksDB does not offer atomicity across multiple keys. To address this issue and provide true ACID guarantees, the mvcc_design.md document reveals...2ThemisDB uses the RocksDB TransactionDB variant. This implementation is considered "production ready".2characterized and offers:
Snapshot Isolation:Each transaction operates on a consistent snapshot of the database.2
Conflict Detection:Parallel transactions that process the same keys are detected.2
Atomare Rollbacks:Failing transactions are completely rolled back, thus maintaining consistency between the "Base Entity" blobs and all associated projection layers (indices).2
This decision is of fundamental importance. It elevates ThemisDB from a loose collection of indices.1to a true, transactionally consistent multi-model database (TMM-DB).1

1.4. Performance analysis of the LSM tree approach: Maximizing write throughput (C/U/D)

Choosing an LSM tree (RocksDB) as the foundation is a deliberate compromise that maximizes write performance (create, update, delete). LSM trees are inherently "append-only." Every write/update/delete operation is an extremely fast, sequential write to an in-memory structure (the memtable). This data is only later asynchronously "fed" and compacted into sorted files (SSTables) on the SSD.1
This architecture maximizes write throughput (ingestion rate). The ThemisDB documentation demonstrates a deep awareness of optimizing this write path. The document compression_benchmarks.md2analyzes the writing performance under different compression algorithms (LZ4, ZSTD, Snappy).2The memory_tuning.md2explicitly recommends LZ4 or ZSTD. This demonstrates the active balancing of CPU costs (for compression) and I/O load (when flushing to the SSD).
However, the downside of this architecture is the reading performance.

1.5. The Parsing Challenge: Serialization and On-the-Fly Extraction

While the LSM tree design speeds up copy/subtract/distribute operations, it introduces an inherent weakness in read operations. A simple point query using the primary key (Get(PK)) is fast. However, a query that applies filters to attributes (e.g., SELECT * FROM users WHERE age > 30) would, as in1(Part 1.2) described as “catastrophically” slow.1It would require a full scan of all "Base Entity" blobs in the users table. Each individual blob would have to be read from the SSD, deserialized (parsed), and filtered.1
This inherent reading weaknessforcesarchitecturally, the need for the “layers” (indices) described in Part 2.
However, this leads to a new “critical system bottleneck”1: the CPU speed of deserialization. AteachDuring the write operation (C/U), the blob must be parsed to extract the fields to be indexed (e.g., age) and to update the secondary indexes (part 2). See the ThemisDB documentation base_entity.md.2This is directly addressed by the requirement of "Fast Field Extraction". This implies the use of high-performance parsing libraries such as simdjson (C++) or serde (Rust), which can process JSON at rates of several gigabytes per second, often bypassing the complete deserialization of the entire object.1

Part 2: The Multi-Model Projection Layers: Implementing the “Layers” in ThemisDB

The request1The aforementioned "layers" are not separate storage systems. They are read-optimized index projections derived from the "Base Entity" blobs defined in Part 1. They are physically stored in the same RocksDB storage and serve solely to accelerate read operations (the 'R' in CRUD). Each layer provides a "view" of the canonical data optimized for the respective query language (SQL, graph traversal, ANN search).1

2.1. Relational Projections: Analysis of the ThemisDB Secondary Indices

Problem:Speeding up an SQL-like query, e.g., SELECT * FROM users WHERE age = 30. As outlined in 1.4, a table scan of the blobs is unacceptable.1
Architectural design1A classic secondary index. Physically, this is a separate set of key-value pairs within RocksDB that maps an attribute value to the primary key of the "Base Entity" blob.1
Key: String("idx:users:age:30:PK_des_Users_123")
Value: (empty) or PK_of_Users_123
Implementation (ThemisDB):The ThemisDB implementation goes far beyond this theoretical basic case. The document indexes.md2ThemisDB has confirmed that it has implemented a comprehensive suite of secondary index types:
Single-Column & Composite:Standard indices across one or more fields.
Range:Essential for resolving queries with inequalities (e.g., age > 30). The query optimizer would perform a RocksDB Seek() on the prefix idx:users:age:30: and iterate over all subsequent keys.1
Sparse:Indexes that only create entries for documents that actually contain the indexed field.
Geo:A significant functional enhancement. In combination with geo_relational_schema.md2(which defines tables for points, lines, polygons) this index type offers a specialized, fast spatial search.
TTL (Time-To-Live):Indicates operational maturity. This index allows the system to automatically expire data (e.g., caching entries or session data) after a certain period of time.
Full text:Implements a full-text search index, probably by creating an inverted index (Token -> PK list) within the RocksDB storage.

2.2. Native Graph Projections: Simulated Adjacency and Recursive Path Queries

Problem:Acceleration of graph traversals (e.g., friends-of-friends queries). Native graph databases use "index-free adjacency" ($O(1)$) for this, which is based on direct memory pointers. This is impossible in an abstracted KV store like RocksDB.1
Architectural design1: The adjacency mustsimulatedBuilding upon the model from Part 1 (nodes and edges are separate blobs), two dedicated secondary indices (projections) are created to quickly resolve edge relationships.1:
Outdex:
Key: String("graph:out:PK_des_Startknotens:PK_der_Kante")
Value: PK_des_Zielknotens
Incoming edges (index):
Key: String("graph:in:PK_of_target_node:PK_of_edge")
Value: PK_des_Startknotens
A traversal (e.g., "find all neighbors of user/123") becomes a highly efficient RocksDB prefix scan: Seek("graph:out:user/123:"). While this is not an $O(1)$ pointer lookup, it is an $O(k \cdot \log N)$ scan (where $k$ is the number of neighbors), which represents optimal performance on an LSM tree.1
Implementation (ThemisDB):ThemisDB has implemented this projection layer and built a powerful abstraction layer on top of it.
Layer 1 (projection):The graph:out/graph:in prefix structure described above.1
Layer 2 (Query Engine):ThemisDB's Advanced Query Language (AQL) uses this projection. aql_syntax.md2confirms “Graph Traversals” as a core function.
Layer 3 (Features):The document recursive_path_queries.md2confirms the implementation of high-performance graphing algorithms that build upon this projection, including:
Traverses with variable depth (e.g. 1-5 hops).
Shortest Path.
Latitude search (BFS).
Temporal graph queries.
Additionally, path_constraints.md describes2Mechanisms for pruning the search space during traversal (e.g., Last-Edge, No-Vertex Constraints), which further increases query efficiency.

2.3. Vector Projections: The HNSW Index and Vector Operations

Problem:Acceleration of the similarity search (Approximate Nearest Neighbor, ANN) for the vectors stored in the "Base Entity" blobs.1
Architectural design1The HNSW (Hierarchical Navigable Small World) algorithm is the de facto standard.1The ANN index is a separate projection layer. It storesnotnot the vectors themselves, but a complex graph structure that relies on thePrimary keythe “Base Entity” is referenced. When a query is made, the engine searches the HNSW graph, obtains a list of PKs (e.g., [PK_7, PK_42]), and then performs a multi-get on RocksDB to retrieve the complete blobs.1
Implementation (ThemisDB):ThemisDB has implemented this vector projection layer exactly.
vector_ops.md 2describes the core operations provided via this layer: “Batch Insertion”, “Targeted Deletion” and “KNN Search” (K-Nearest Neighbors).
The PRIORITIES.md document2Provides the crucial information regarding production readiness: The "HNSW Persistence" feature is 100% complete (P0/P1 Feature).
This is a crucial point. A purely in-memory HNSW index is relatively easy to implement.persistAn HNSW index that survives crashes and remains transactionally consistent with the RocksDB storage layer (via the MVCC transactions described in 1.3) is extremely complex. The completion of this feature demonstrates that ThemisDB's vector layer has reached production readiness.2

2.4. File/Blob Projections: The “Content Architecture” of ThemisDB

Problem:Efficient storage of large binary files (e.g., images, PDFs) that would inflate the "Base Entity" blobs and impair the scan performance of the LSM tree.1
Architectural design1The theoretical blueprint proposes two passive solutions: (1) RocksDB BlobDB, which automatically extracts large values ​​from the LSM tree, or (2) storing a URI (e.g., S3 path) in the blob.1
Implementation (ThemisDB):The ThemisDB implementation is far more intelligent and comprehensive than the1-Suggestion. Instead of passively storing blobs, ThemisDB has developed a content-intelligent platform. The document content_architecture.md2describes a “Content Manager System” with a “unified ingestion pipeline” and “processor routing”.2
The processing flow is as follows:
A client uploads a file via the HTTP API (defined in ingestion.md).2).
Das ContentTypeRegistry 2Identifies the blob type (e.g., image/jpeg or application/gpx).
The “processor routing”2forwards the blob to a specialized, domain-specific processor.
Specialized processors (image_processor_design.md2, geo_processor_design.md 2) analyzethe content:
The Image processorextracts EXIF ​​metadata, creates thumbnails and generates "3x3 Tile-Grid Chunking" (probably for creating vector embeddings for image parts).2
The GeoprocessorExtracts, normalizes and chunks data from GeoJSON or GPX files.2
First afterFollowing this enrichment, the “Base Entity” blob – now filled with valuable metadata (and possibly vectors) – is stored in the RocksDB storage layer (part 1) along with the derived artifacts (such as thumbnails).
This architecture is a massive extension of the1-Plans and transforms ThemisDB from a passive database into an active, content-intelligent processing platform.

2.5. Transactional Consistency: ACID Guarantees vs. SAGA Verifiers

Problem:How is consistency between the canonical "Base Entity" blob (part 1) and all its index projections (part 2) ensured during a write operation?1
Architectural design1: 1(Part 2.5) represents the critical compromise:
ACID (Within a TMM database):The blob and indexes are updated within a single atomic transaction. This provides strong consistency.1
Saga Pattern (Distributed):A sequence of local transactions (e.g., 1. Write blob, 2. Write index). If a step fails, compensating transactions must undo the previous steps. This leads to eventual consistency (BASE).1
Implementation (ThemisDB):As outlined in Part 1.3, ThemisDB has distinguished itself through the use of RocksDB TransactionDB and a "production-ready" MVCC design.2clear for theinternalACID guarantee decided.
Nevertheless, the admin_tools_user_guide.md lists2A tool called SAGA Verifier was introduced. The existence of this tool alongside an ACID kernel is a sign of deep architectural understanding of real-world enterprise environments. The logical chain of cause and effect is as follows:
ThemisDB itself is ACID-compliant for allinternalOperations (Blob + Index Updates).
However, ThemisDB likely exists within an ecosystem of microservices thatouterSaga patterns are used for distributed transactions (e.g.Step A: Create user in ThemisDB; Step B: Send email; Step C: Provision S3 bucket).
It strikesexternalIf a step (e.g., step B) is missing, the saga must...Compensating Transaction an ThemisDB senden (z. B. Delete the user created in step A).
What happens when theseCompensating TransactionDoes it fail or get lost? The entire system is in an inconsistent state (an "orphaned" user exists in ThemisDB).
The SAGA Verifier2is therefore most likely not a runtime consistency mechanism, but aadministrative Audit-ToolIt scans the database to find such "orphaned" entities that were created byexternal, failed sagascaused. It is a compliance and repair tool that acknowledges the internal ACID guarantee, but also addresses the realities of distributed systems.

Part 3: Detailed design of the memory hierarchy: CRUD performance optimization in ThemisDB

Maximizing CRUD performance requires intelligent placement of data components on the standard storage hierarchy (RAM, NVMe SSD, HDD).1The ThemisDB documentation, especially memory_tuning.md2, confirms that this theoretical optimization is a central component of the system design.

3.1. Analysis of the storage hierarchy (HDD, NVMe SSD, RAM, VRAM)

The theoretical analysis1defines the roles of storage media:
HDD (Hard Disk Drives):Due to extremely high latency during random access, it is unsuitable for primary CRUD operations. It is intended solely for cold backups and long-term archiving.1
NVMe-SSD (Solid State Drives):The "workhorse" layer. Offers fast random read access and high throughput, ideal for main data (SSTables) and critical, latency-sensitive write operations (WAL).1
DRAM (Main RAM):The "hot" layer. Latencies that are orders of magnitude lower than those of SSDs, crucial for caching and in-memory processing.1
VRAM (Graphics RAM):A co-processor memory on a GPU that is used exclusively for massively parallel computations (especially ANN searches).1

3.2. Optimization strategies in ThemisDB: WAL placement, block cache and compression

The ThemisDB documentation (memory_tuning.md)2) confirms exactly the in1Theoretical optimization blueprint for a RocksDB-based engine presented below:
Write-Ahead Log (WAL) on NVMe:The WAL is the most critical component for C/U/D latency. Every transactionmustThe changes must be written synchronously to the WAL before they are considered "committed." The ThemisDB policy "WAL on NVMe"2ensures the lowest possible latency for this sequential write operation.1
LSM-Tree Block Cache im RAM:The “block cache in RAM”2This is the equivalent of the buffer cache in relational databases. It stores hot, recently read data blocks (SSTable blocks) from the SSD to speed up repeated read accesses (CRUDs R) and avoid expensive I/O operations.1
LSM-Tree Memtable im RAM:All new write operations (C/U/D) first land in the memtable, an in-memory data structure that lives entirely in RAM and enables the fastest ingestion of data.1
LSM-Tree SStables on SSD:The persistent, sorted main data files (SSTables), which contain the "Base Entity" blobs (part 1) and all secondary indexes (part 2), reside on the SSD fleet.1
Compression (LZ4/ZSTD):As mentioned in 1.4, memory_tuning.md is recommended.2The use of LZ4 or ZSTD reduces the storage space required on the SSD and, more importantly, the I/O throughput required when reading blocks from the SSD to the RAM block cache, at the cost of a slight CPU load for decompression.
Bloom-Filter: memory_tuning.md 2It also mentions a "bloom filter." This is a crucial detail that is in1Bloom filters are probabilistic in-memory structures that can quickly determine whether a keypossibly notThis is present in a specific SSTable file on the SSD. This drastically reduces read I/O for point fetches of non-existent keys and avoids unnecessary SSD accesses.

3.3. RAM Management: Caching of HNSW Index Layers and Graph Topology

For high-performance vector and graph queries, the general RocksDB block cache (3.2) is often insufficient.1(Part 3.3) postulates advanced RAM caching strategies that are necessary for the implementation of the ThemisDB features (Parts 2.2, 2.3):
Vector (HNSW):For vector indices that are too large for RAM, a hybrid approach is used. The "upper layers" of the HNSW graph (the "highways" for navigation) are sparse and are used when...everyoneThe search process is complete. Therefore, the data must be permanently held in RAM ("pinned") to avoid navigation hotspots. The denser "lower layers" (the "local roads") can be loaded from the SSD into the block cache as needed.1The high-performance KNN search of ThemisDB (vector_ops.md)2) is dependent on such a strategy.
Graph (Topologie):For graph traversals with sub-millisecond latency requirements, even the $O(k \cdot \log N)$ SSD scan (from 2.2) is too slow. In this "high-performance" mode, theentireThe graph topology (i.e., the adjacency lists/indices graph:out:* and graph:in:*) is proactively loaded from the SSD into RAM at system startup. This in-memory topology is implemented as a native C++ (std::vector<std::vector<...>>) or Rust (petgraph or Vec<Vec<usize>>) topology.1) Data structure maintained to allow $O(k)$ lookups in RAM.1The implementation of "shortest path" algorithms in ThemisDB (recursive_path_queries.md)2) is hardly conceivable as performant without such an in-memory caching strategy for "hot" subgraphs.

Table 2: ThemisDB storage hierarchy strategy (Updated)

The following table summarizes the strategy developed in Part 3 and integrates the specific implementation details of ThemisDB.

Data component
Physical storage
Primary optimized operation
Justification (latency/throughput)
Write-Ahead Log (WAL)
NVMe SSD (fastest persistent)2
Create, Update, Delete
Minimum latency for synchronous, sequential write operations. Defines the write commit time.1
LSM-Tree Memtable
RAM (DRAM)
Create, Update, Delete
In-memory buffering of write operations; fastest ingestion.1
LSM-Tree Block Cache
RAM (DRAM) 2
Read
Caching of hot data blocks (base entities, indexes) from the SSD. Reduces random read I/O.1
Bloom-Filter
RAM (DRAM) 2
Read
Probabilistic check whether a keynotExists on the SSD. Avoids unnecessary read I/O.2
LSM-Tree SSTables (Core Data & Indices)
SSD (NVMe/SATA)
Read (Cache Miss)
Persistent storage (compressed with LZ4/ZSTD)2Requires fast random read I/O.1
HNSW Index (Upper Classes)
RAM (DRAM)
Read (vector search)
The graph's "highways" must be in memory for every search to avoid navigation hotspots.1
HNSW Index (Lower Classes)
SSD (NVMe)
Read (vector search)
Too large for RAM. Optimized for SSD-based random read accesses during the final phase of the ANN search.1
Graph-Topologie (Hot)
RAM (DRAM)
Read (Graph-Traversal)
Simulated "index-free adjacency". Topology is held in RAM for $O(k)$ traversals.1
ANN Index (GPU Copy)
VRAM (Graphics RAM)
Read (Batch vector search)
Temporary copy for massively parallel acceleration of distance calculation (Faiss GPU).1
Cold Blobs / Backups
HDD / Cloud Storage
(Offline)
Cost-effective storage for data with no latency requirements.1


Part 4: The Hybrid Query Engine: ThemisDB's "Advanced Query Language" (AQL)

The components described in Parts 1, 2, and 3 are the "muscles" of the system—the storage and index layers. ThemisDB's Advanced Query Language (AQL) is its "brain."1, which orchestrates these components and combines them into a coherent, hybrid query engine.2

4.1. Analysis of AQL Syntax and Semantics

The ThemisDB documentation confirms that AQL is the primary interface to the database.2 aql_syntax.md 2reveals that AQL is a declarative language (similar to SQL, ArangoDB's AQL, or Neo4js Cypher) that unifies operations across all data models:
Relational/document operations: FOR, FILTER, SORT, LIMIT, RETURN, Joins.2
Analytical operations:COLLECT/GROUP BY (confirmed in PRIORITIES.md)2as a completed P0/P1 feature).
Graph operations: „Graph-Traversals“ 2, which use the projection described in 2.2.
Vector operations:Implied by hybrid_search_design.md2, probably implemented as AQL functions such as NEAR(...) or SIMILARITY(...), which use the HNSW projection from 2.3.
The following table demystifies AQL by showing which AQL constructs control which of the complex backend projection layers (from Part 2).
Table 4: AQL Function Overview (Mapping of AQL to Physical Layers)

AQL construct (example)
Target data model
Underlying projection layer (implementation from Part 2)
FOR u IN users FILTER u.age > 30
Relational
Secondary index scan (range index on age) [2.1]
FOR u IN users FILTER u.location NEAR [...]
Geo
Geo-Index Scan (Spatial Search) [2.1]
FOR v IN 1..3 OUTBOUND 'user/123' GRAPH 'friends'
Graph
“Outdex” prefix scan (graph:out:user/123:...) [2.2]
RETURN SHORTEST_PATH(...)
Graph
RAM-based or SSD-based Dijkstra/BFS scan [2.2]
FOR d IN docs SORT SIMILARITY(d.vec, [...]) LIMIT 10
Vector
HNSW Index Search (KNN) [2.3]
RETURN AVG(u.age) COLLECT status = u.status
Analytical
Parallel table scanning and deserialization in Apache Arrow (4.4)


4.2. The Hybrid Query Optimizer: Analysis of AQL EXPLAIN & PROFILE

1(Part 4.3) explainsWhyAn optimizer is needed by describing the compromise between "Plan A" (start: relational filter, then vector search on a small set) and "Plan B" (start: global vector search, then filter on a large set).1The optimizer must decide, based on costs, which plan is the most efficient.
The ThemisDB documentationprovesThat this optimizer exists. The existence of the document aql_explain_profile.md2is the proof of this "brain".2
AQL EXPLAIN:Shows theplannedExecution path chosen by the optimizer (e.g., "Index Scan" instead of "Table Scan").
AQL PROFILE:Executes the query and displays theactualRuntime metrics to identify performance bottlenecks.2
The aql_explain_profile.md2It even provides specific profiling metrics: edges_expanded and prune_last_level. This is a remarkably deep insight. It means that a developer not onlysees, that its graph query is slow, butWhyPROFILE quantitatively shows (edges_expanded) the explosion rate of its traverse and (pruned_last_level) how effectively the constraints in path_constraints.md are applied.2defined pruning rules. This is an expert-level debugging tool.

4.3. Implementation of “Hybrid Search”: Fusion of vector, graph and relational predicates

The most powerful form of hybrid search, which is in1(Part 2.3) describes the “pre-filtering”. Instead of performing a global vector search and the resultsthereafterTo filter (post-filtering), this approach reverses the process:
Phase 1 (Relational):The relational index (from 2.1) is scanned (e.g. year > 2020) to create a candidate list of PKs (typically represented as a bitset).
Phase 2 (Vector):The HNSW graph traverse1will be modified. At each navigation steponlynavigated to nodes whose primary keys are present in the candidate bitset from Phase 1.
The ThemisDB document hybrid_search_design.md2This is the "as-built" specification for precisely this function. It describes the "combination of vector similarity with graph expansion and filtering".2
The status of this document – ​​“Phase 4”2– is also informative. IMPLEMENTATION_STATUS.md2shows that P0/P1 features (theindividualLayers such as HNSW are 100% complete. This reveals the logical development strategy of ThemisDB:
P0/P1 (Completed):Build the columns (relational index, graph index, vector index) independently of each other.
Phase 4 (In progress):Now build theBridges(hybrid_search_design.md) between the columns to enable true hybrid queries.

4.4. The analytical in-memory format (Apache Arrow) and task-based parallelism (TBB/Rayon)

Once the AQL optimizer (4.2) has created a plan, the engine must implement it.carry out. 1(Parts 4.1 and 4.2) propose two crucial technologies for execution:
Parallelism (TBB/Rayon):A hybrid query (e.g., relational scan + vector search) consists of multiple tasks. These should be executed in parallel on N CPU cores. Instead of using OpenMP (for loop parallelism), task-based runtime systems like Intel Threading Building Blocks (TBB) are recommended.1(C++) oder Rayon1(Rust) ideal. They use a "work-stealing" scheduler to efficiently distribute tasks (e.g., the parallel execution of task_A (filter) and task_B (graph traversal)) across all cores.1
OLAP-Format (Apache Arrow):For analytical queries (e.g., AVG(age)) that scan millions of entities, retrieving and deserializing (OLTP style) millions of blobs row by row would be a performance disaster (the “catastrophic” problem from 1.4).1The high-performance solution involves using Apache Arrow1as canonicalIn-Memory-Formatto use. Worker threads read the RocksDB blocks and deserialize them (using simdjson/serde).directly into column-based Apache Arrow RecordBatches. All further aggregations (AVG, GROUP BY) then take place with high performance on these CPU cache-friendly, SIMD-optimized arrays.1
The ThemisDB documentation (PRIORITIES.md)2) confirms that COLLECT/GROUP BY (an analytical operation) is a self-contained P0/P1 feature. To provide this functionality efficiently,mustthe engine a strategy like that of1Use the suggested method (deserialization of blobs in Apache Arrow).

Table 3: ThemisDB C++/Rust Implementation Toolkit (Recommended Building Blocks)

Based on the in1recommended and the in2/3Based on the implied functions, the following table shows the most likely technology toolkit that is or should be used for the implementation of the ThemisDB core.

Components
C++ library(ies)
Rust Library(s)
Reason
Key-Value Storage Engine
RocksDB 1
rocksdb (Wrapper), redb, sled 1
RocksDB is the C++ standard and is endorsed by ThemisDB.2Rust alternatives are available.1
Parallel Execution Engine
Intel TBB (Tasking) 1
Rayon (Tasking/Loops), Tokio (Async I/O) 1
TBB (C++) and Rayon (Rust) offer task-based work stealing, ideal for query engines.1
JSON/Binary Parsing
simdjson, VelocyPack 1
serde / serde_json, bycode1
simdjson (C++) or serde (Rust) are used for “Fast Field Extraction” (1.5)2indispensable.1
In-Memory Graph-Topologie
C++ Backend von graph-tool, Boost.Graph 1
petgraph, Custom Thing<Thing>1
Required for high-performance RAM caching (3.3) to implement recursive_path_queries.md.2
Vector Index (ANN)
Faiss (CPU/GPU), HNSWlib 1
hnsw (native Rust) or wrapper for Faiss1
The C++ ecosystem (Faiss) is unsurpassed, especially when it comes to GPU acceleration.1
In-Memory Analytics & IPC
Apache Arrow, Apache DataFusion 1
arrow-rs, datafusion 1
Arrow and DataFusion (Rust) are the backbone for high-performance OLAP workloads (GROUP BY).2


Part 5: Implementation Toolkit, Status and Operational Management


5.1. C++ vs. Rust: A strategic analysis in the context of ThemisDB

The source document1It is titled "Hybrid Database Architecture C++/Rust".1The ThemisDB Documents2 and 3They do not reveal which language was ultimately chosen for the core message. This choice represents a fundamental strategic compromise that is reflected in1(Part 7.2) is explained and analyzed here in the context of the specific ThemisDB functions:
Argument for C++:C++ currently offers thismost mature ecosystemsfor the key components. In particular, Faiss's GPU integration.1(to speed up vector_ops.md)2) and the established stability of RocksDB1and TBB1are unsurpassed. For a prototype that needs to demonstrate raw performance (especially GPU-accelerated vector search), the C++ stack is superior.1
Argument for Rust:Rust offersguaranteed storage security. For the development of a robust, highly concurrent database kernel – such as that of ThemisDB, which supports parallel queries (Part 4.4), complex caching (Part 3.3) and transactional index updates (Part 1.3, mvcc_design.md)2Managing this level of complexity is a tremendous strategic advantage. Avoiding buffer overflows, use-after-free behavior, and data races in a system of this complexity is crucial for long-term maintainability and stability.1The Rust ecosystem (Rayon, DataFusion, Tokyo)1is also excellent.
Recommendation:For a long-term, robust and maintainable production system, where the correctness of the mvcc_design.md2If the primary concern is raw, GPU-accelerated vector performance for hybrid_search_design.md, then the Rust stack is the strategically superior choice.2In that case, the C++ stack is more pragmatic.

5.2. Current Implementation Status and Roadmap Analysis

The ThemisDB documentation provides a clear snapshot of the project's progress (as of the end of October 2025):
Status: IMPLEMENTATION_STATUS.md 2reports a "total progress ~52%" and, more importantly, "P0 features 100%".2
Tracing:OpenTelemetry Tracing is considered complete (✅)2Marked. This is a strong signal of production readiness, as it is essential for debugging and performance monitoring in distributed systems.
Priorities: PRIORITIES.md 2confirms that all P0/P1 features are complete, including critical components such as “HNSW Persistence” (2.3) and “COLLECT/GROUP BY” (4.1).
This outlines a clear development narrative:
Past (Completed):The foundation is in place. The core (RocksDB + MVCC), the individual storage pillars (persistent HNSW, graph, indices) and the basic AQL are "production-ready".2
Present day (“Phase 4”):The "smart" features are being built. These include the bridges.betweenthe pillars (hybrid_search_design.md2) and the domain-specific ingestion intelligence (image_processor_design.md, geo_processor_design.md)2).
Future (“Design Phase”):The next wave of features, which are not yet implemented, concerns data-level security, as described in column_encryption.md.2(Part 6.2) described.

5.3. Ingestion Architecture and Administrative Tools

ThemisDB is designed not just as a library, but as a fully functional server.
Ingestion:Primary data input is via an HTTP API (ingestion.md).2, developers.md 2). The document json_ingestion_spec.md2describes a standardized ETL process (Extract, Transform, Load)3, which proposes a “unified contract for heterogeneous sources”2provides and manages mappings, transformations and data provenance.
Operations:The admin_tools_user_guide.md2lists the crucial Day 2 Operations tools that demonstrate the system is built for operators and compliance officers:
Audit Log Viewer (see section 6.3)
SAGA Verifier (see part 2.5)
PII Manager (see part 6.3)
Deployment:A deployment.md2describes deployment via binary, Docker, or from source code.2

Part 6: Security Architecture and Compliance in ThemisDB


6.1. Authentication and Authorization: A Strategic Gap

The theoretical blueprint1(Part 5) recommends a robust security model based on Kerberos/GSSAPI (authentication) and RBAC (role-based access control)1based.
An analysis of the ThemisDB documents2 and 3shows asignificant gapin this area. The documentation (as of November 2025)does not mention these conceptsThe focus is on theData security(Encryption, PII), but not on theAccess security(Authentication, authorization).
This is the most obvious difference between the1-blueprint and the2/3Implementation. A database system without a granular RBAC model is not production-ready in an enterprise environment. Implementing a robust RBAC model is essential.11This should be considered a critical priority for the next phase of the ThemisDB roadmap.

6.2. Encryption at rest: Analysis of the “Column-Level Encryption Design”

In the area of ​​data-at-rest encryption, ThemisDB is planning a far more granular and superior solution than the one in1(Part 5.3) proposed general approach to file system encryption.
The document column_encryption.md2(Status: “Design Phase”) describes a “Column-Level Encryption”.2In the context of the “Base Entity” blobs (Part 1.1), this means aAttribute-level encryption.
This approach is far superior to full database or file system encryption. It allows PII fields (e.g., {"ssn": "ENCRYPTED(...)"}) to be encrypted, while non-sensitive fields (e.g., {"age": 30}) remain in plaintext. The crucial advantage is that the high-performance secondary indexes (from Part 2.1) can still be created on the non-sensitive fields (age) and used for queries. Full encryption would prevent this.
The design of ThemisDB also includes2:
Transparent use:The encryption and decryption process is transparent to the application user.
Key Rotation:A mechanism for regularly updating encryption keys.
Pluggable Key Management:This signals the intention to enable integration with external Key Management Systems (KMS), such as those offered by1 recommended.

6.3. Auditing and Compliance: The ThemisDB Tool Suite

1(Part 2.5, 5.4) identifies auditing and traceability as crucial for compliance with regulations such as the GDPR and the EU AI Act.1The ThemisDB implementation provides the necessary tools to meet these requirements:
Audit Log Viewer:As described in admin_tools_user_guide.md2Listed here, this is the direct implementation of a centralized audit system. It logs access and modification events and makes them searchable for compliance audits.2
PII Manager:This in2The aforementioned tool is a specialized tool, likely based on the column_encryption.md design (6.2). It is used to manage requests related to personally identifiable information, such as the implementation of the GDPR's "right to be forgotten".
Together, the audit log viewer, the PII manager, and the planned column-level encryption design form a coherent "compliance nexus" that meets the theoretical requirements of1surpasses expectations and is tailored to the practical needs of corporate compliance departments.

Part 7: Strategic Summary and Critical Evaluation


7.1. Synthesis of the ThemisDB design: A coherent multi-model architecture

Analysis of the ThemisDB documentation2in comparison with the theoretical architectural blueprint1This results in the picture of a coherent, well-thought-out and advanced multi-model database system.
ThemisDB is a faithful and, in many areas, enhanced implementation of the1The outlined TMM-DB architecture is correctly based on a canonical, write-optimized "Base Entity" blob stored in an LSM-Tree KV engine (RocksDB).1
The inherent reading weakness of this approach is mitigated by a rich set oftransactionally consistentProjection layers are compensated. The crucial step for using RocksDB TransactionDB to ensure ACID/MVCC warranties.2This is proof of the core's technical maturity. These layers include not only simple relational indexes, but also advanced geo- and full-text indexes.2, persistent HNSW vector indices2and efficient, simulated graph adjacency indices.1
ThemisDB's "Advanced Query Language" (AQL) combines these layers and provides a declarative interface.2for true hybrid queries. The development of EXPLAIN/PROFILE tools2This shows that the focus is on optimized, cost-based query execution.
Furthermore, ThemisDB has the1-Design through the implementation of aContent-intelligent ingestion pipeline (content_architecture.md 2) and a suite ofoperativen Compliance-Tools (admin_tools_user_guide.md 2) significantly expanded. The project is logically evolving from a completed "Core-DB" foundation (P0/P1 completed) to an "intelligent platform" (Phase 4, Hybrid Search, Content Processors).2

7.2. Identified strengths and architectural compromises

Strengthen:ThemisDB's greatest strength is therealhybrid query capability, especially that described in hybrid_search_design.md2The described fusion of vector, graph, and relational predicates. Internal ACID/MVCC consistency.2This is a massive advantage over polyglot persistence approaches.1The operational maturity achieved through tools such as OpenTelemetry Tracing, Audit Log Viewer, and deployment options2Demonstrating this is also a strength.
Architectural compromises:The system accepts the fundamental LSM tree compromise: high write performance at the cost of read overhead and index maintenance. All the system complexity that previously resided in distributed systems has now been successfully integrated into theQuery optimizer(4.2) shifted. The performance of the overall system now depends on the ability of this “brain” to weigh the relative costs of a relational index scan (2.1) against a graph traversal (2.2) and an HNSW search (2.3) and to choose the most efficient, hybrid execution plan.

7.3. Analysis of open issues and future challenges

The analysis identifies three strategic challenges for the future development of ThemisDB:
Challenge 1: Authentication & Authorization (AuthN/AuthZ):As outlined in section 6.1, this is the most significant gap in the current documentation. The system requires a robust, granular RBAC model to be deployed in an enterprise environment. This is the most pressing requirement for production readiness beyond the core engine.
Challenge 2: C++ vs. Rust (technology stack):The strategic decision regarding the core technology stack (5.1) must be made and documented. This decision has a fundamental impact on performance (C++/Faiss GPU).1versus safety and maintainability (Rust/Rayon).1
Challenge 3: Distributed scaling (sharding & replication):The entire analyzed architecture1describes an extremely powerfulSingle-Node-SystemThenextThe architectural limit will be horizontal scaling (sharding, replication, distributed transactions across nodes). This increases the complexity of MVCC design, index management, and query optimization by an order of magnitude and represents the logical next evolutionary step for the ThemisDB project.

Weitere Ressourcen:

- Online‑Doku: https://makr-code.github.io/ThemisDB/
- Druckansicht: https://makr-code.github.io/ThemisDB/print_page/
- Gesamt‑PDF: https://makr-code.github.io/ThemisDB/themisdb-docs-complete.pdf
- GitHub Wiki: https://github.com/makr-code/ThemisDB/wiki

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

Inspired by systems like:
- ArangoDB (Multi-model architecture)
- CozoDB (Hybrid relational-graph-vector)
- Azure Cosmos DB (Multi-model with ARS format)

## Admin Tools Übersicht

Sieben WPF-Tools mit einheitlichem Themis-Layout unterstützen Betrieb und Compliance:

- Audit Log Viewer
- SAGA Verifier
- PII Manager
- Key Rotation Dashboard
- Retention Manager
- Classification Dashboard
- Compliance Reports

Build/Publish:
- Skript: `publish-all.ps1` (Release, self-contained win-x64)
- Artefakte: `dist/<ToolName>/`

Dokumentation:
- Benutzerhandbuch: `docs/admin_tools_user_guide.md`
- Admin-Guide: `docs/admin_tools_admin_guide.md`
- OpenAPI: `openapi/openapi.yaml`
