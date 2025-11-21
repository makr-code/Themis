# ThemisDB Benchmark und Test Audit

**Datum:** 2025-11-21  
**Zweck:** Identifikation fehlender Benchmarks und Google Tests für vollständiges Source Code und Funktions-Audit

## Zusammenfassung

**Aktuelle Abdeckung:**
- **Benchmarks:** 16 Benchmark-Dateien
- **Tests:** 166 Test-Dateien (156 mit TEST-Makros)
- **Source-Komponenten:** 23 Hauptmodule

## 1. Fehlende Benchmarks

### 1.1 Hochpriorisierte Benchmarks

#### Sharding & Distributed Operations
**Status:** ❌ Keine Benchmarks vorhanden

**Benötigte Benchmarks:**
- `bench_shard_routing.cpp`: Routing-Latenz für verschiedene Shard-Topologien
  - Metriken: Latenz pro Shard-Lookup, Consistent Hashing Performance
  - Szenarien: 10, 100, 1000 Shards
  
- `bench_data_migration.cpp`: Rebalancing Performance
  - Metriken: GB/s Migrationsrate, Downtime während Rebalancing
  - Szenarien: 1GB, 10GB, 100GB Datenvolumen
  
- `bench_remote_execution.cpp`: Cross-Shard Query Performance
  - Metriken: Netzwerk-Overhead, Parallele vs Serielle Execution
  - Szenarien: 2-16 Shards gleichzeitig

**Existierende Tests:** test_sharding_core.cpp, test_rebalance_migration.cpp

#### Transaction Manager & SAGA
**Status:** ❌ Keine Benchmarks vorhanden

**Benötigte Benchmarks:**
- `bench_transaction_throughput.cpp`: ACID Transaction Throughput
  - Metriken: TPS (Transactions Per Second), Commit-Latenz
  - Szenarien: Read-Heavy (90%), Write-Heavy (90%), Mixed (50/50)
  
- `bench_saga_compensation.cpp`: SAGA Rollback Performance
  - Metriken: Compensation-Latenz, Partial Rollback Overhead
  - Szenarien: 2-10 Schritte, verschiedene Failure-Punkte

**Existierende Tests:** test_transaction_manager.cpp, test_saga_logger.cpp

#### CDC & Changefeed
**Status:** ❌ Keine Benchmarks vorhanden

**Benötigte Benchmarks:**
- `bench_changefeed_throughput.cpp`: Changefeed Event Processing
  - Metriken: Events/sec, SSE Connection Overhead
  - Szenarien: 1, 10, 100 gleichzeitige Subscriber
  
- `bench_cdc_replication.cpp`: CDC Replication Lag
  - Metriken: Replication Lag (ms), Catch-up Speed (events/s)
  - Szenarien: Burst Writes vs Steady State

**Existierende Tests:** test_http_changefeed.cpp, test_http_changefeed_sse.cpp

#### Timeseries & Retention
**Status:** ⚠️ Teilweise vorhanden

**Benötigte Benchmarks:**
- `bench_timeseries_ingestion.cpp`: Time-Series Write Throughput
  - Metriken: Points/sec Ingestion, Compression Ratio
  - Szenarien: Raw vs Gorilla Compression
  
- `bench_continuous_aggregation.cpp`: Continuous Aggregation Performance
  - Metriken: Aggregation Latency, Materialization Overhead
  - Szenarien: 1min, 5min, 1hour Windows
  
- `bench_retention_deletion.cpp`: Retention Policy Execution
  - Metriken: Deletion Throughput (GB/s), Index Rebuild Impact
  - Szenarien: 1%, 10%, 50% Data Deletion

**Existierende Tests:** test_http_timeseries.cpp, test_timeseries_retention.cpp

### 1.2 Mittlere Priorität

#### LLM Integration
**Status:** ❌ Keine Benchmarks vorhanden

**Benötigte Benchmarks:**
- `bench_llm_interaction_store.cpp`: LLM Interaction Logging Performance
  - Metriken: Write Throughput für Prompts/Responses
  - Szenarien: Verschiedene Prompt-Längen (100, 1K, 10K tokens)
  
- `bench_prompt_templating.cpp`: Prompt Template Rendering
  - Metriken: Template Expansion Latency
  - Szenarien: Einfache vs Komplexe Templates mit Variablen

**Existierende Tests:** test_prompt_manager.cpp

#### Graph Analytics
**Status:** ⚠️ Teilweise vorhanden (bench_hybrid_aql_sugar.cpp erwähnt Graph+Geo)

**Benötigte Benchmarks:**
- `bench_graph_traversal.cpp`: Graph Traversal Algorithmen
  - Metriken: BFS/DFS Latenz, Nodes Visited per Second
  - Szenarien: Sparse vs Dense Graphs (100, 1K, 10K Nodes)
  
- `bench_pagerank.cpp`: PageRank Berechnung
  - Metriken: Iterations/sec, Convergence Time
  - Szenarien: Web-Scale Graphs (100K+ Nodes)
  
- `bench_gnn_embeddings.cpp`: GNN Embedding Generation
  - Metriken: Embedding Generation Latency, Batch Processing
  - Szenarien: Node2Vec, GraphSAGE Algorithmen

**Existierende Tests:** test_graph_analytics.cpp, test_gnn_embeddings.cpp

#### Acceleration Backends
**Status:** ⚠️ Teilweise vorhanden (bench_simd_distance.cpp für SIMD)

**Benötigte Benchmarks:**
- `bench_gpu_backends.cpp`: GPU Acceleration Performance Vergleich
  - Metriken: CUDA vs HIP vs Metal vs Vulkan vs DirectX
  - Szenarien: Vector Search, Distance Computations
  
- `bench_oneapi_backend.cpp`: Intel oneAPI Performance
  - Metriken: CPU vs GPU Execution für verschiedene Workloads
  
- `bench_opencl_backend.cpp`: OpenCL Cross-Platform Performance

**Existierende Tests:** test_acceleration.cpp

#### Content Management
**Status:** ❌ Keine Benchmarks vorhanden

**Benötigte Benchmarks:**
- `bench_content_versioning.cpp`: Content Version Management
  - Metriken: Version Creation Latency, Diff Computation
  - Szenarien: Small (1KB) vs Large (100MB) Files
  
- `bench_mime_detection.cpp`: MIME Type Detection
  - Metriken: Detection Latency, Accuracy
  - Szenarien: Verschiedene Dateitypen (Text, Binary, Media)
  
- `bench_text_extraction.cpp`: Text Extraction from Various Formats
  - Metriken: Extraction Throughput (MB/s)
  - Szenarien: PDF, DOCX, HTML, etc.

**Existierende Tests:** test_content_policy.cpp, test_content_policy_manual.cpp

### 1.3 Niedrige Priorität

#### Governance & Policy Engine
**Status:** ❌ Keine Benchmarks vorhanden

**Benötigte Benchmarks:**
- `bench_policy_evaluation.cpp`: Policy Rule Evaluation
  - Metriken: Rules/sec Evaluation, Complex Policy Latency
  - Szenarien: 10, 100, 1000 aktive Policies
  
- `bench_ranger_adapter.cpp`: Apache Ranger Integration
  - Metriken: Authorization Check Latency
  - Szenarien: Mit/Ohne Caching

**Existierende Tests:** test_policy_engine_load.cpp, test_http_governance.cpp

#### Import/Export
**Status:** ❌ Keine Benchmarks vorhanden

**Benötigte Benchmarks:**
- `bench_postgres_import.cpp`: PostgreSQL Import Performance
  - Metriken: Rows/sec Import, Schema Mapping Overhead
  
- `bench_jsonl_export.cpp`: JSONL LLM Export
  - Metriken: Export Throughput (MB/s)

**Existierende Tests:** Keine spezifischen Tests gefunden

#### Blob Storage Backends
**Status:** ⚠️ bench_blob_zstd.cpp vorhanden

**Benötigte Benchmarks:**
- `bench_blob_backends_comparison.cpp`: S3 vs Azure vs WebDAV vs Filesystem
  - Metriken: Upload/Download Throughput, Latency
  - Szenarien: Verschiedene Blob-Größen (1KB - 100MB)

**Existierende Tests:** test_blob_storage.cpp

## 2. Fehlende Google Tests

### 2.1 Kritische Test-Lücken

#### Sharding Module (src/sharding/)
**Fehlende Tests:**

- `test_consistent_hash_distribution.cpp`: Consistent Hashing Verteilung
  - Unit Tests für Hash-Ring Balance
  - Edge Cases: Node Joins/Leaves
  
- `test_data_migrator_edge_cases.cpp`: Data Migration Fehlerbehandlung
  - Netzwerk-Fehler während Migration
  - Partial Migration Rollback
  
- `test_mtls_client.cpp`: mTLS Client Certificate Handling
  - Certificate Rotation
  - Certificate Validation Failures
  
- `test_prometheus_metrics.cpp`: Metrics Aggregation
  - Metric Export Korrektheit
  - High-Cardinality Labels

**Existierende Tests:** test_sharding_core.cpp, test_rebalance_migration.cpp, test_pki_shard_certificate.cpp

#### Transaction Module (src/transaction/)
**Fehlende Tests:**

- `test_transaction_isolation.cpp`: ACID Isolation Levels
  - Read Committed vs Serializable
  - Phantom Reads, Dirty Reads
  
- `test_saga_concurrent_execution.cpp`: Parallele SAGA Execution
  - Race Conditions bei Compensation
  - Idempotenz von Compensation Actions

**Existierende Tests:** test_transaction_manager.cpp, test_saga_logger.cpp

#### CDC Module (src/cdc/)
**Fehlende Tests:**

- `test_changefeed_ordering.cpp`: Event Ordering Guarantees
  - Per-Key Ordering
  - Global Ordering (falls unterstützt)
  
- `test_changefeed_backpressure.cpp`: Backpressure Handling
  - Slow Consumer Scenarios
  - Buffer Overflow Handling

**Existierende Tests:** test_http_changefeed.cpp, test_http_changefeed_sse.cpp, test_http_changefeed_sse_extended.cpp

#### Timeseries Module (src/timeseries/)
**Fehlende Tests:**

- `test_gorilla_codec_edge_cases.cpp`: Gorilla Compression Edge Cases
  - NaN/Inf Handling
  - Timestamp Out-of-Order
  
- `test_continuous_agg_correctness.cpp`: Continuous Aggregation Korrektheit
  - Verschiedene Window-Funktionen
  - Late-Arriving Data
  
- `test_tsstore_concurrent_writes.cpp`: Concurrent Writes zu TSStore
  - Write Conflicts
  - Lock Contention

**Existierende Tests:** test_http_timeseries.cpp, test_timeseries_retention.cpp

### 2.2 Wichtige Test-Lücken

#### LLM Module (src/llm/)
**Fehlende Tests:**

- `test_llm_interaction_store_pagination.cpp`: Pagination & Filtering
  - Large Result Sets
  - Complex Filter Queries
  
- `test_prompt_manager_security.cpp`: Template Injection Prevention
  - Malicious Template Variables
  - Sandbox Escapes

**Existierende Tests:** test_prompt_manager.cpp

#### Acceleration Module (src/acceleration/)
**Fehlende Tests:**

- `test_backend_fallback.cpp`: Backend Fallback-Logik
  - GPU Unavailable → CPU Fallback
  - Driver Fehler Handling
  
- `test_plugin_loader_security.cpp`: Plugin Security
  - Signature Verification
  - Sandbox Enforcement
  
- `test_vulkan_backend.cpp`: Vulkan-spezifische Tests
  - Device Selection
  - Memory Management
  
- `test_metal_backend.cpp`: Metal-spezifische Tests (macOS)
  - Metal Performance Shaders
  
- `test_hip_backend.cpp`: HIP-spezifische Tests (AMD)
  
- `test_directx_backend.cpp`: DirectX-spezifische Tests (Windows)
  
- `test_zluda_backend.cpp`: ZLUDA-spezifische Tests

**Existierende Tests:** test_acceleration.cpp (generisch)

#### Geo Module (src/geo/)
**Fehlende Tests:**

- `test_geo_gpu_backend.cpp`: GPU-beschleunigte Geo-Operationen
  - Distance Computations auf GPU
  - Spatial Join auf GPU
  
- `test_boost_cpu_exact_backend.cpp`: Boost Geometry Korrektheit
  - Complex Polygon Intersections
  - Edge Cases (Self-Intersecting Polygons)

**Existierende Tests:** Indirekt durch test_spatial_index.cpp, test_hybrid_queries.cpp

#### Content Module (src/content/)
**Fehlende Tests:**

- `test_content_fs_concurrent_access.cpp`: Concurrent File Access
  - Read/Write Conflicts
  - Lock Granularity
  
- `test_mock_clip_processor.cpp`: CLIP Mock Korrektheit
  - Embedding Consistency
  
- `test_text_processor_languages.cpp`: Multi-Language Text Processing
  - UTF-8 Edge Cases
  - RTL Languages

**Existierende Tests:** test_content_policy.cpp, test_content_policy_manual.cpp

#### Governance Module (src/governance/)
**Fehlende Tests:**

- `test_policy_engine_performance.cpp`: Policy Engine Performance unter Last
  - 1000+ aktive Policies
  - Complex Nested Rules
  
- `test_policy_yaml_validation.cpp`: YAML Policy Validation
  - Schema Validation
  - Cyclic Dependencies

**Existierende Tests:** test_policy_engine_load.cpp, test_policy_yaml.cpp, test_http_governance.cpp

#### Import/Export Module
**Fehlende Tests:**

- `test_postgres_importer_datatypes.cpp`: PostgreSQL Datentyp-Mapping
  - Array Types
  - JSONB
  - PostGIS Geometry
  
- `test_postgres_importer_error_handling.cpp`: Fehlerbehandlung
  - Connection Failures
  - Schema Mismatch
  
- `test_jsonl_llm_exporter_large_datasets.cpp`: Large Dataset Export
  - Memory Usage
  - Streaming Export

**Existierende Tests:** Keine gefunden

#### Plugin System (src/plugins/)
**Fehlende Tests:**

- `test_plugin_lifecycle.cpp`: Plugin Load/Unload Lifecycle
  - Hot Reload
  - Dependency Management
  
- `test_plugin_api_versioning.cpp`: API Version Compatibility
  - Backward Compatibility
  - Version Negotiation

**Existierende Tests:** test_plugin_manager.cpp

### 2.3 Storage Backend Tests

#### Blob Storage
**Fehlende Tests:**

- `test_blob_backend_s3_multipart.cpp`: S3 Multipart Upload
  - Large Files (> 5GB)
  - Upload Resume
  
- `test_blob_backend_azure_auth.cpp`: Azure Authentication
  - SAS Tokens
  - Managed Identity
  
- `test_blob_backend_webdav_auth.cpp`: WebDAV Authentication
  - Basic Auth
  - Digest Auth
  - Bearer Token

**Existierende Tests:** test_blob_storage.cpp (generisch)

#### Backup & Recovery
**Fehlende Tests:**

- `test_backup_incremental.cpp`: Incremental Backups
  - Delta Computation
  - Restore from Incremental Chain
  
- `test_backup_encryption.cpp`: Encrypted Backups
  - Key Rotation während Backup
  - Decrypt & Restore

**Existierende Tests:** test_backup_restore.cpp, test_backup_restore_integration.cpp, test_wal_backup_manager.cpp

## 3. Empfehlungen zur Priorisierung

### Phase 1 (Sofort): Kritische Lücken
1. **Sharding Benchmarks**: bench_shard_routing.cpp, bench_data_migration.cpp
2. **Transaction Tests**: test_transaction_isolation.cpp, test_saga_concurrent_execution.cpp
3. **CDC Tests**: test_changefeed_ordering.cpp, test_changefeed_backpressure.cpp
4. **Timeseries Benchmarks**: bench_timeseries_ingestion.cpp, bench_continuous_aggregation.cpp

### Phase 2 (Kurzfristig): Wichtige Lücken
1. **Acceleration Tests**: Backend-spezifische Tests (Vulkan, Metal, HIP, DirectX)
2. **Graph Benchmarks**: bench_graph_traversal.cpp, bench_pagerank.cpp
3. **LLM Tests**: test_llm_interaction_store_pagination.cpp
4. **Import/Export Tests**: test_postgres_importer_datatypes.cpp

### Phase 3 (Mittelfristig): Vollständige Abdeckung
1. **Content Benchmarks**: bench_content_versioning.cpp, bench_text_extraction.cpp
2. **Governance Benchmarks**: bench_policy_evaluation.cpp
3. **Blob Storage Tests**: Backend-spezifische Tests (S3, Azure, WebDAV)
4. **Plugin Tests**: test_plugin_lifecycle.cpp, test_plugin_api_versioning.cpp

## 4. Testabdeckungs-Metriken (Empfohlen)

Um den Fortschritt zu messen, sollten folgende Metriken erfasst werden:

### Code Coverage
- **Ziel:** >80% Line Coverage, >70% Branch Coverage
- **Tools:** gcov, lcov, codecov.io
- **Fokus-Module:**
  - Sharding: Aktuell vermutlich <50%
  - Transaction: Aktuell vermutlich ~60%
  - CDC: Aktuell vermutlich ~40%
  - Timeseries: Aktuell vermutlich ~50%

### Benchmark Coverage
- **Ziel:** Mindestens 1 Benchmark pro Performance-kritischem Modul
- **Metriken:**
  - Latenz (p50, p95, p99, p99.9)
  - Throughput (ops/sec, MB/s)
  - Resource Utilization (CPU%, RAM MB, GPU%)
  - Skalierungsverhalten (10x, 100x, 1000x)

### Integration Test Coverage
- **Ziel:** End-to-End Szenarien für jede Hauptfunktion
- **Beispiele:**
  - Sharding: Full Rebalance Operation
  - Transaction: Distributed Transaction über 3 Shards
  - CDC: Changefeed mit 100 Subscribern
  - Timeseries: 1 Million Points Ingestion & Query

## 5. Nächste Schritte

1. **Review Meeting**: Priorisierung der identifizierten Lücken mit Team
2. **Benchmark Template**: Standard-Benchmark-Template erstellen (ähnlich bench_hybrid_aql_sugar.cpp)
3. **Test Template**: Standard-Test-Template mit Fixtures erstellen
4. **CI Integration**: Benchmarks in Nightly CI Pipeline integrieren
5. **Coverage Reporting**: Code Coverage Dashboard einrichten
6. **Dokumentation**: Benchmark-Ergebnisse in Performance-Dokumentation aufnehmen

## Anhang: Statistik

### Aktueller Stand
- **Benchmark-Dateien:** 16
- **Test-Dateien:** 166 (156 mit TEST-Makros)
- **Source-Module:** 23

### Identifizierte Lücken
- **Fehlende Benchmarks:** ~25 (hochprioritär: 11)
- **Fehlende Tests:** ~40 (hochprioritär: 15)

### Geschätzte Implementierungszeit
- **Benchmarks (Phase 1):** ~2-3 Wochen (4 Benchmarks)
- **Tests (Phase 1):** ~3-4 Wochen (8 Tests)
- **Gesamt (alle Phasen):** ~12-16 Wochen
