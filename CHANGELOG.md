# Changelog

All notable changes to ThemisDB will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added - Documentation Consolidation (2025-11-20)

#### Consolidated Documentation
- **Development Audit Log** - Comprehensive development status documentation
  - New file: `DEVELOPMENT_AUDITLOG.md` (550+ lines)
  - Complete feature inventory (all 7 phases)
  - Code metrics: 63,506 lines, 279 docs, 303/303 tests PASS
  - Performance benchmarks
  - Roadmap priorities (GPU/CUDA planning)
  - Known issues & workarounds
  - Compliance status (GDPR/SOC2/HIPAA)

- **Consolidated Roadmap** - Unified development roadmap
  - New file: `ROADMAP.md` (450+ lines)
  - Q1 2026: SDK finalization, Column Encryption, Window Functions
  - Q2-Q3 2026: Distributed Sharding, GPU Acceleration (CUDA/DirectX), OLAP
  - Q4 2026+: Multi-DC, K8s Operator, In-Database ML, Streaming
  - Performance targets & benchmarks
  - Risk analysis & mitigation
  - Resource planning

- **README.md Updates** - Improved navigation
  - Added prominent links to DEVELOPMENT_AUDITLOG.md and ROADMAP.md
  - Consolidated documentation section
  - Better stakeholder vs. developer documentation separation

### Added - Critical/High-Priority Sprint (2025-11-17)

#### Security & Encryption
- **Lazy Re-Encryption for Key Rotation** - Zero-downtime key rotation with transparent migration
  - New methods: `FieldEncryption::decryptAndReEncrypt()`, `FieldEncryption::needsReEncryption()`
  - Automatically re-encrypts data on read if using outdated key version
  - Safe error handling: returns decrypted data even if re-encryption fails
  - Comprehensive test coverage: `tests/test_lazy_reencryption.cpp` (412 lines, 9 scenarios)
  - Enables compliance requirements (quarterly/annual key rotation) without service disruption

- **Encryption Prometheus Metrics** - Real-time monitoring of encryption operations
  - 42 atomic counters in `FieldEncryption::Metrics` struct
  - Operation counters: `encrypt_operations_total`, `decrypt_operations_total`, `reencrypt_operations_total`
  - Error tracking: `encrypt_errors_total`, `decrypt_errors_total`, `reencrypt_errors_total`
  - Performance histograms: Duration buckets (100µs, 500µs, 1ms, 5ms, 10ms, >10ms)
  - Data volume tracking: `encrypt_bytes_total`, `decrypt_bytes_total`
  - Integrated into `/api/metrics` Prometheus endpoint
  - Key rotation progress: `reencrypt_skipped_total / (reencrypt_operations_total + reencrypt_skipped_total)`
  - Documentation: `docs/encryption_metrics.md` (410 lines, Grafana queries, alerts, compliance mapping)

- **Schema-Based Encryption E2E Tests** - Comprehensive test coverage
  - 809 lines, 19 test cases in `tests/test_schema_encryption.cpp`
  - Coverage: Schema configuration, auto encrypt/decrypt, multiple fields/collections
  - Edge cases: Missing fields, empty values, non-string types, schema updates
  - Integration: Batch operations, key rotation, secondary index compatibility

- **Vector Metadata Encryption Edge Cases** - Additional security validation
  - 532 lines in `tests/test_vector_metadata_encryption_edge_cases.cpp`
  - Never encrypts embedding field (prevents vector search breakage)
  - Handles all metadata types (strings, numbers, nested objects)
  - Schema-driven encryption validation

#### Graph Database
- **BFS Bug Fix - GraphId in Topology** - CRITICAL bugfix
  - **Problem:** `GraphIndexManager::bfs()` returned no edges after `rebuildTopology()` when using type filtering
  - **Root Cause:** `rebuildTopology()` loaded `graphId` from RocksDB, but `addEdge()` didn't propagate it to in-memory topology
  - **Solution:** Added `graphId` parameter to `addEdgeToTopology_()` and `removeEdgeFromTopology_()` helper methods
  - Updated all 3 `addEdge()` variants (WriteBatch, Transaction, base) to extract `graphId` from edge entities
  - Files modified: `include/index/graph_index.h`, `src/index/graph_index.cpp`
  - Impact: Prevents BLOCKER bug that broke graph operations after topology rebuild

#### Documentation
- **PKI Integration Architecture** - Production deployment guide
  - 513 lines in `docs/pki_integration_architecture.md`
  - PKI components: PKIClient, PKIKeyProvider, LEK Manager
  - eIDAS compliance: Qualified signatures, timestamp authority integration
  - 4 deployment scenarios: Dev stub, Self-signed, CA-signed, eIDAS-qualified
  - Audit-log signing: Encrypt-then-sign pattern with SAGA events
  - ENV configuration reference, security recommendations, troubleshooting

- **PKI Signatures Technical Reference** - API documentation
  - 598 lines in `docs/pki_signatures.md`
  - Supported algorithms: RSA-SHA256/384/512
  - Sign/Verify workflows with sequence diagrams
  - C++ API reference with code examples
  - HTTP API reference: `/api/pki/sign`, `/api/pki/verify`
  - Vault HSM integration examples
  - Compliance mapping: eIDAS, DSGVO (GDPR)
  - Performance benchmarks: <5ms sign, <3ms verify

- **Encryption Metrics Documentation** - Monitoring guide
  - 410 lines in `docs/encryption_metrics.md`
  - Metric definitions and use cases
  - Grafana dashboard queries (PromQL examples)
  - Critical alerts: HighDecryptionErrorRate, EncryptionPerformanceDegradation
  - Warning alerts: SlowKeyRotation
  - Compliance reporting: GDPR Article 32, eIDAS
  - Troubleshooting guide with resolution steps
  - Key rotation monitoring workflow

### Fixed

- **Graph BFS with Type Filtering** - Fixed critical bug where `rebuildTopology()` caused BFS to return empty results
  - GraphId now properly propagated to in-memory topology structures
  - All 3 `addEdge()` variants extract and pass graphId from edge entities
  - Backward compatibility: Default empty string for graphId parameter

### Verified

- **Content-Blob ZSTD Compression** - Confirmed as already implemented
  - Implementation: `src/utils/zstd_codec.cpp` with compress/decompress methods
  - Integration: ContentManager with 50% storage savings
  - Metrics: Prometheus `/api/metrics` with compression ratio histogram
  - MIME type skipping: Images, videos, pre-compressed formats

- **Audit Log Encryption** - Confirmed as already implemented
  - Implementation: `src/utils/saga_logger.cpp`
  - Pattern: Encrypt-then-sign with FieldEncryption + PKIClient
  - Compliance: DSGVO Article 32, eIDAS-compliant signatures
  - All SAGA events encrypted before signing

### Statistics

**Sprint Summary (2025-11-17):**
- Branch: `feature/critical-high-priority-fixes`
- Commits: 2
- Files changed: 12 (7 new, 5 modified)
- Lines added: 3,633
- Tasks completed: 8 (4 CRITICAL, 4 HIGH)
- Test coverage: 1,753 lines of new tests
- Documentation: 1,521 lines of production-ready docs

**Test Status:**
- All 468 existing tests: PASSING ✅
- New tests: 3 comprehensive test suites created
- Integration: Zero breaking changes

## Previous Releases

### [0.9.0] - 2025-11-11

#### Added
- **Temporal Aggregation Support** - Graph edge property aggregation over time windows
  - New method: `GraphIndexManager::aggregateEdgePropertyInTimeRange()`
  - Aggregations: COUNT, SUM, AVG, MIN, MAX
  - Time-range filtering with optional edge type filtering
  - Backward compatibility: Supports both `edge:<graphId>:<edgeId>` and `edge:<edgeId>` key formats
  - Tests: `tests/test_temporal_aggregation_property.cpp`
  - Documentation: `docs/temporal_time_range_queries.md`

### [0.8.0] - 2025-11-08

#### Added
- **Time-Series Engine** - Full implementation with Gorilla compression
  - Gorilla compression: 10-20x compression ratio (+15% CPU overhead)
  - Continuous aggregates: Pre-computed rollups for analytics
  - Retention policies: Automatic deletion of expired data
  - API: TSStore, RetentionManager, ContinuousAggregateManager
  - Tests: `test_tsstore.cpp`, `test_gorilla.cpp` (all PASS)
  - Documentation: `docs/time_series.md`, `wiki_out/time_series.md`

- **PII Manager** - RocksDB-backed PII mapping management
  - CRUD operations: addMapping, getMapping, deleteMapping, listMappings
  - Column family: `pii_mappings` (separate from demo data)
  - API: PIIApiHandler with filtering and pagination
  - CSV export functionality
  - HTTP integration tests

#### Improved
- **HKDF Caching** - Thread-local LRU/TTL cache for encryption
  - Files: `include/utils/hkdf_cache.h`, `src/utils/hkdf_cache.cpp`
  - Integration: Hot-paths in ContentManager and encryption operations
  - Thread-safe with configurable capacity/TTL
  - Implicit invalidation on key rotation

- **Batch Encryption Optimization** - Parallelized entity batch encryption
  - New API: `FieldEncryption::encryptEntityBatch()`
  - Per-entity HKDF derivation with cache utilization
  - Intel TBB parallelization for encryption operations
  - Single base key fetch per batch

---

## Contributing

When adding entries to this changelog:
1. Add new entries under `[Unreleased]` section
2. Use semantic versioning for releases
3. Categorize changes: Added, Changed, Deprecated, Removed, Fixed, Security
4. Include file paths and line counts for significant changes
5. Reference issue/PR numbers when applicable
6. Update statistics section with sprint metrics

## Links

- [GitHub Repository](https://github.com/makr-code/ThemisDB)
- [Documentation](https://makr-code.github.io/ThemisDB/)
- [Issue Tracker](https://github.com/makr-code/ThemisDB/issues)
