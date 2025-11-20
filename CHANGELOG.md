# Changelog

All notable changes to ThemisDB will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added - JavaScript SDK Transaction Support (2025-11-20 Phase 1 Start)

#### JavaScript/TypeScript SDK Transaction Implementation
- **Transaction Support** - Full BEGIN/COMMIT/ROLLBACK implementation
  - `ThemisClient.beginTransaction(options?)` method
  - `Transaction` class with CRUD operations
  - Isolation level support (READ_COMMITTED, SNAPSHOT)
  - Transaction state management (isActive, transactionId)
  - Automatic X-Transaction-Id header injection
  - Error handling with TransactionError class

- **Test Coverage** - Comprehensive transaction tests
  - Unit tests for Transaction class
  - State management tests (commit/rollback tracking)
  - API structure validation
  - Integration test placeholders (requires running server)

- **Documentation** - Updated JavaScript SDK docs
  - README.md with transaction examples
  - API reference for Transaction class
  - Quick start guide
  - Error handling examples

- **Package Updates**
  - Version bumped to 0.1.0-beta.1
  - Package name updated to @themisdb/client
  - Additional keywords (transaction, multi-model, graph, vector)

#### Implementation Details
- **Files Modified:**
  - `clients/javascript/src/index.ts` - Added Transaction class and support methods
  - `clients/javascript/tests/transaction.spec.ts` - New test file (7 tests passing)
  - `clients/javascript/tests/client.spec.ts` - Added basic client tests
  - `clients/javascript/README.md` - Comprehensive documentation update
  - `clients/javascript/package.json` - Version and metadata update

- **Server Integration:**
  - Uses existing `/transaction/begin` endpoint
  - Uses existing `/transaction/commit` endpoint
  - Uses existing `/transaction/rollback` endpoint
  - Injects `X-Transaction-Id` header for all operations

### Added - SDK Implementation Plan (2025-11-20 v5)

#### SDK Development Roadmap
- **SDK_IMPLEMENTATION_PLAN.md** - Detailed implementation plan for SDK finalization
  - Phase 1: JS/Python/Rust finalization (2-3 weeks)
    - Transaction Support (all SDKs) - Week 1
    - Missing features & Async (Python) - Week 2  
    - Package publishing & Documentation - Week 3
  - Phase 2: New SDKs roadmap (Q2-Q4 2026)
    - Go SDK (Q2 2026) - Cloud-Native, Kubernetes
    - Java SDK (Q2 2026) - Enterprise, Android
    - C# SDK (Q3 2026) - Microsoft, Azure, Unity
    - Swift SDK (Q4 2026) - iOS/macOS
  - Detailed feature specifications for Transaction Support
  - Package configuration templates
  - Success criteria and timeline

### Added - SDK Audit & Language Analysis (2025-11-20 v4)

#### SDK Status Documentation
- **SDK_AUDIT_STATUS.md** - Comprehensive audit of all existing SDKs
  - JavaScript/TypeScript SDK: 436 lines, Alpha status
  - Python SDK: 540 lines, Alpha status
  - Rust SDK: 705 lines, Alpha status
  - C++ SDK: Does not exist, NOT PLANNED (server already in C++)
  - Identified missing features: Transaction support (all SDKs)
  - Identified missing features: Package publishing (NPM, PyPI, Crates.io)
  - Identified missing features: Batch/Graph operations
  - Implementation plan for Beta release (3 phases, 2-3 weeks)

- **SDK_LANGUAGE_ANALYSIS.md** - Analysis of relevant programming languages for future SDKs
  - Priority 1: Go (cloud-native, Kubernetes), Java (enterprise, Android)
  - Priority 2: C# (.NET, Azure, Unity), PHP (web development), Swift (iOS/macOS)
  - Not planned: C++ (server already in C++), Scala/Clojure (Java SDK sufficient)
  - Market analysis: Stack Overflow, GitHub Octoverse, TIOBE Index
  - Competitor analysis: MongoDB (9 SDKs), Neo4j (5 SDKs), Weaviate (4 SDKs)
  - Recommended SDK roadmap: Post-Beta (Go, Java), Post-v1.0.0 (C#, PHP, Swift)

#### Documentation Updates
- **NEXT_IMPLEMENTATION_PRIORITIES.md Updates v4**
  - Updated SDK section to include Rust SDK (was missing)
  - Added note that C++ SDK is NOT PLANNED
  - Updated deliverables to include all three SDKs (JS, Python, Rust)
  - Updated success criteria for all three SDKs
  - Added reference to SDK_AUDIT_STATUS.md and SDK_LANGUAGE_ANALYSIS.md
  - Updated Quick Start guide to include Rust SDK

### Changed - CI/CD Timeline Update (2025-11-20 v3)

#### Priority Adjustment
- **CI/CD Workflows** - Moved from "next priority" to Post-v1.0.0
  - Decision: CI/CD workflows will be implemented with v1.0.0 release, not before
  - README badges remain (placeholder for future workflows)
  - Focus shifts to SDK Beta Release as next priority

#### Documentation Updates
- **NEXT_IMPLEMENTATION_PRIORITIES.md Updates v3**
  - Changed next branch back from CI/CD to SDK Finalization
  - Moved CI/CD Workflows to "Post-v1.0.0 Features" section
  - Updated implementation timeline: SDK Beta (Woche 1-3), then v1.0.0 prep (Woche 4-13)
  - Updated Quick Start guide back to SDK development
  - Updated recommendation section

- **DEVELOPMENT_AUDITLOG.md Updates v2**
  - Removed CI/CD from P1 priorities
  - Moved CI/CD to "Post-v1.0.0" section
  - SDK Finalization marked as "NÄCHSTE PRIORITÄT"

### Changed - Window Functions & CI/CD Status Update (2025-11-20 v2)

#### Code Audit Findings
- **Window Functions Clarification** - Identified that Window Functions are already fully implemented
  - Despite status "0% - geplant" in DEVELOPMENT_AUDITLOG.md, complete implementation exists
  - Implementation: `WindowEvaluator` class in `include/query/window_evaluator.h` (342 lines)
  - Core logic: `src/query/window_evaluator.cpp` (543 lines)
  - Features: ROW_NUMBER, RANK, DENSE_RANK, LAG, LEAD, FIRST_VALUE, LAST_VALUE
  - PARTITION BY and ORDER BY support
  - Window Frames (ROWS, RANGE)
  - Comprehensive tests: `tests/test_window_functions.cpp` (579 lines)
  
- **CI/CD Status** - Identified critical gap in CI/CD infrastructure
  - README badges reference non-existent workflows (ci.yml, code-quality.yml)
  - No GitHub Actions workflows exist in `.github/workflows/` directory
  - CI/CD Improvements moved from P1 to **highest priority**

#### Documentation Updates
- **DEVELOPMENT_AUDITLOG.md Updates**
  - Phase 6 Analytics updated from 60% to 85%
  - Window Functions marked as 100% complete
  - Updated "Offene Punkte" section:
    - ✅ Column-Level Encryption marked as COMPLETED
    - ✅ Window Functions marked as COMPLETED (removed from open items)
    - ⚠️ CI/CD Workflows marked as KRITISCH (critical priority)
    - ❌ Content Processors removed (not DB responsibility)

- **NEXT_IMPLEMENTATION_PRIORITIES.md Updates v2**
  - **New recommendation:** CI/CD Improvements as next branch (was SDK Finalization)
  - Added Window Functions to "Archiviert" section (already complete)
  - Removed Content Processors from roadmap (ingestion is not DB duty)
  - Moved Docker Runtime Optimization to "Post-v1.0.0 Enterprise Features"
  - Added GPU CUDA Support and REST API Extensions as future options
  - Updated implementation timeline: 1 week CI/CD, then 2-3 weeks SDK Beta
  - Removed 8-week block for Content Processors and Window Functions
  - Updated Quick Start guide for CI/CD workflow creation

#### Rationale for Priority Changes
1. **CI/CD CRITICAL:** README references workflows that don't exist (broken badges)
2. **Window Functions DONE:** 885 lines of code + 579 lines of tests already exist
3. **Content Processors OUT:** Ingestion/processing is not database responsibility
4. **Docker POST-v1.0.0:** Enterprise feature deferred until after v1.0.0 release

### Changed - Column-Level Encryption Status Update (2025-11-20)

#### Documentation Updates
- **Column-Level Encryption Clarification** - Identified that Column-Level Encryption is already fully implemented
  - Feature exists as "Field-Level Encryption" + "Schema-Based Encryption"
  - In document databases, field-level and column-level encryption are functionally equivalent
  - Implementation: `FieldEncryption` class (AES-256-GCM) in `include/security/encryption.h`
  - Schema API: `GET/PUT /config/encryption-schema` endpoints
  - Key rotation: `decryptAndReEncrypt()`, `needsReEncryption()` methods
  - Tests: `tests/test_schema_encryption.cpp` (809 lines, 19 test cases)
  - Tests: `tests/test_lazy_reencryption.cpp` (412 lines, key rotation)
  - Documentation: `docs/column_encryption.md` (25K design document)

- **DEVELOPMENT_AUDITLOG.md Updates** - Corrected security status
  - Phase 7 Security updated from 85% to 100%
  - Removed "Column-Level Encryption (Design-Phase)" from offene Punkte
  - Added clarification note that Column-Level Encryption is implemented as Field-Level Encryption
  - Updated executive summary: Security from 85% to 100%

- **NEXT_IMPLEMENTATION_PRIORITIES.md Updates** - Updated priority recommendation
  - Changed recommended next branch from Column-Level Encryption to SDK Finalization
  - Added "Status Update" section at top explaining Column-Level Encryption is complete
  - Moved Column-Level Encryption to "Archiviert" (Archived) section
  - Updated implementation timeline: removed Column-Level Encryption, adjusted weeks
  - Updated success criteria for SDK Finalization (JavaScript + Python)
  - Updated Quick Start guide for SDK development branch

- **ROADMAP.md Updates** - Marked Column-Level Encryption as complete
  - Section 1.2 (Column-Level Encryption) marked as ✅ COMPLETED
  - Added note explaining it's implemented as Field-Level + Schema-Based Encryption
  - Listed all implemented features, documentation, and tests
  - Marked section 1.3 (JavaScript/Python SDK) as "→ NEXT PRIORITY"

### Added - Next Implementation Priorities (2025-11-20)

#### Implementation Planning Documentation
- **Next Implementation Priorities** - Detailed prioritization of next development steps
  - New file: `NEXT_IMPLEMENTATION_PRIORITIES.md` (250+ lines)
  - Recommended next branch: Column-Level Encryption (P0, 1-2 weeks)
  - Alternative P0 priority: JavaScript/Python SDK Finalization (2-3 weeks)
  - P1 priorities roadmap: Content Processors, CI/CD, Window Functions, Docker Optimization
  - Implementation order for Q1 2026 (13-week plan)
  - Success criteria and acceptance criteria for each feature
  - Quick start guide for next development branch
  - References to existing documentation (ROADMAP.md, DEVELOPMENT_AUDITLOG.md)

- **ROADMAP.md Updates** - Marked documentation task as complete
  - Section 1.1 (Documentation & Consolidation) marked as ✅ COMPLETED
  - Added reference to NEXT_IMPLEMENTATION_PRIORITIES.md at the beginning
  - Updated deliverables list to include priorities document

- **README.md Updates** - Added navigation to priorities document
  - Added link to NEXT_IMPLEMENTATION_PRIORITIES.md in documentation section
  - Enhanced developer navigation with implementation planning reference

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
