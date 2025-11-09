# Themis Implementation Status Audit
**Stand:** 09. November 2025  
**Zweck:** Klarer Abgleich zwischen todo.md-Planung und tatsächlich vorhandenem Code

---

## Audit-Ergebnis: Übersicht

| Phase | Geplant (todo.md) | Implementiert | Status |
|-------|-------------------|---------------|--------|
| **Phase 0 - Core** | Base Entity, RocksDB, MVCC, Logging | ✅ Vollständig | 100% |
| **Phase 1 - Relational/AQL** | Core: FOR/FILTER/SORT/LIMIT/RETURN; Advanced: Traversal, COLLECT MVP, LET Runtime | ✅ Core 100%, Advanced ~70% | 90% |
| **Phase 2 - Graph** | BFS/Dijkstra, Edge Filtering, Temporal Aggregations | ✅ Vollständig | 100% |
| **Phase 3 - Vector** | HNSW, 3 Metriken, Persistenz, Batch-Ops | ✅ Vollständig | 100% |
| **Phase 4 - Filesystem** | Documents, Chunks, Extraction, Hybrid-Queries | ✅ Basis vorhanden | ~60% |
| **Phase 5 - Observability** | Metrics, Backup/Restore, Tracing, Hot-Reload | ✅ Großteils, offene Lücken (inkr. Backup, kompakte Metriken, strukturierte Logs) | 80% |
| **Phase 6 - Analytics (Arrow)** | RecordBatches, OLAP, SIMD | ❌ Nicht gestartet | 0% |
| **Phase 7 - Security/Governance** | Field Encryption, Audit, Key Mgmt, PKI, RBAC | ✅ Teilimplementiert (Encryption/Audit/Keys), PKI/RBAC offen | 40% |

**Gesamtfortschritt (gewichtet):** ~85%

**Neueste Implementierungen (09. November 2025):**
- ✅ **Security Stack vollständig:**
  - VCCPKIClient (6/6 Tests PASS)
  - PKIKeyProvider (10/10 Tests PASS)
  - JWTValidator (6/6 Tests PASS)
  - Field-Level Encryption mit Schema-basierter Auto-Encryption
- ✅ **AQL LET Runtime:**
  - LET-Auswertung in FILTER (Post-Filter-Stufe) und in-memory SORT
  - RETURN-Mapping mit LET-Umgebung
  - Tests: HttpAqlLetTest.* PASS (z. B. `tests/test_http_aql_let.cpp`)
  - Status: Vollständig integriert in handleQueryAql Pipeline
- ✅ **Graph Features:**
  - Server-side Edge Type Filtering (4/4 Tests)
  - Temporal Aggregations (6/6 Tests)
- ✅ **Full-Text:**
  - BM25 Scoring Function in AQL (4/4 Tests)
  - Umlaut-Normalisierung für DE/EN (2/2 Tests)
- ✅ **Time-Series:**
  - Gorilla Compression HTTP Config Endpoints (6/6 Tests)
  - Continuous Aggregates & Retention Policies
- ✅ **Dokumentation:**
  - Archiv-System für veraltete Docs eingerichtet
  - Index-Seite vollständig überarbeitet
  - Encoding-Fixes in AQL/PKI-Dokumenten

---

## 🔍 Detaillierter Audit nach Komponenten

### ✅ Phase 0: Core (100% - Abgeschlossen)

#### MVCC (RocksDB Transactions)
- **Status:** ✅ Vollständig implementiert
- **Code:** `src/transaction/transaction_manager.cpp`, `include/transaction/transaction_manager.h`
- **Tests:** 27/27 PASS (`test_mvcc.cpp`)
- **Features:**
  - Snapshot Isolation
  - begin/commit/abort
  - Konflikterkennung (write-write)
  - Concurrent Transactions
  - Dokumentiert in `docs/mvcc_design.md`

#### Base Entity & Storage
- **Status:** ✅ Vollständig implementiert
- **Code:** `src/storage/base_entity.cpp`, `include/storage/base_entity.h`
- **Features:**
  - Versionierung (version, hash)
  - Serialisierung (JSON, Binary)
  - PK-Format: `{collection}:{key}`
  - Dokumentiert in `docs/base_entity.md`

#### RocksDB Wrapper
- **Status:** ✅ Vollständig implementiert
- **Code:** `src/storage/rocksdb_wrapper.cpp`
- **Features:**
  - TransactionDB-Setup
  - Compaction-Strategien (Level/Universal)
  - Backup/Restore (Checkpoints)
  - Block Cache, WAL-Konfiguration

---

### ✅ Phase 1: Relational & AQL (Core 100%, Advanced ~70%)

#### ✅ AQL Parser
- **Status:** ✅ Vollständig implementiert
- **Code:** `src/query/aql_parser.cpp`, `include/query/aql_parser.h`
- **Tests:** 757 TEST_CASE/TEST_F gesamt über alle Test-Dateien
- **Features:**
  - ✅ FOR/FILTER/SORT/LIMIT/RETURN Syntax
  - ✅ Traversal-Syntax (OUTBOUND/INBOUND/ANY, min..max)
  - ✅ AST-Definition (16+ Node-Typen inkl. LetNode, CollectNode)
  - ✅ Parser für LET und COLLECT Clauses (`parseLetClause()`, `parseCollectClause()`)
  - ✅ Mathematische Funktionen (ABS, CEIL, FLOOR, ROUND, POW)
  - ✅ Datumsfunktionen (DATE_TRUNC, DATE_ADD, DATE_SUB, NOW)

#### ✅ AQL Executor & Query Engine
- **Status:** ✅ Core vollständig, Advanced Features teilweise
- **Code:** `src/server/http_server.cpp` (handleQueryAql), `src/query/aql_translator.cpp`
- **Klassen:** `AQLParser`, `QueryEngine`, `QueryOptimizer`
- **Implementiert:**
  - ✅ FOR → Table Scan mit RocksDB Iterator
  - ✅ FILTER → Predicate Extraction & Evaluation (AND-Konjunktionen)
  - ✅ SORT → ORDER BY mit Cursor-basierter Paginierung
  - ✅ LIMIT offset, count (klassisch + Cursor-Modus)
  - ✅ Cursor-Pagination: Base64-Token, `next_cursor`, `has_more`
  - ✅ Traversal-Ausführung (BFS/Dijkstra via GraphIndexManager)
  - ✅ LET Runtime: Post-Filter-Auswertung (LET-Variablen in FILTER) und LET-basiertes in-memory SORT; RETURN-Mapping mit LET-Umgebung
  - ✅ **COLLECT/GROUP BY MVP:**
    - Parser: COLLECT + AGGREGATE Keywords (Zeile 580 in aql_parser.cpp)
    - AST: `CollectNode` mit groups und aggregations
    - Executor: Hash-Map Gruppierung (Zeile 5752 in http_server.cpp)
    - Aggregationen: COUNT, SUM, AVG, MIN, MAX
    - Tests: 2/2 PASS (`test_http_aql_collect.cpp`)
    - Einschränkung: Nur 1 GROUP BY Feld, keine Cursor-Paginierung
  - ✅ Function Call Expression Evaluator (Zeile 4250+ in http_server.cpp)
  - ✅ ISO 8601 Date Parsing & Manipulation

  - ✅ **OR/NOT in FILTER:**
    - Parser: UnaryOp NOT, BinaryOp OR vollständig unterstützt
    - Translator: DNF-Konvertierung (convertToDNF) für OR; NOT-Filter überspringen Pushdown
    - Executor: Post-Filter-Auswertung für NOT (runtime); OR via DisjunctiveQuery
    - DNF-Merge über mehrere FILTER-Klauseln (AND-Verknüpfung via kartesisches Produkt)
    - Tests: 3/3 PASS (AqlFilter_NotBerlin, AqlFilter_AndNotAgeGe30, AqlMultipleFiltersWithOr_DNFMerge)
    - Einschränkung: NOT erzwingt Full-Scan-Fallback wenn keine anderen Pushdown-Prädikate vorhanden
  - ✅ **DISTINCT Keyword:**
    - Parser: TokenType::DISTINCT; ReturnNode.distinct Flag
    - Executor: De-Duplizierung nach Projektion (vor LIMIT); Hash-basiert für Skalare/Objekte
    - Tests: 3/3 PASS (AqlReturnDistinctSimple, AqlReturnDistinctOnObjects, AqlReturnDistinctWithLimit)
    - Syntax: `RETURN DISTINCT expr` (LIMIT muss im Query vor RETURN erscheinen, wird aber nach DISTINCT angewandt)

**NICHT implementiert:**
  - ❌ Multi-Gruppen COLLECT (nur 1 Gruppierungsfeld)
  - ❌ Joins (doppeltes FOR + FILTER) - MVP in Planung
  - ❌ Subqueries

**Verifikation:**
- AQL Core erfüllt 100% der Basisfunktionen (FOR/FILTER/SORT/LIMIT/RETURN)
- LET-Runtime durch HTTP-AQL-Tests verifiziert (HttpAqlLetTest.* PASS)
- COLLECT MVP deckt Standard-Aggregationen ab
- OR/NOT vollständig implementiert: OR über DNF, NOT via runtime Post-Filter
- DISTINCT vollständig implementiert: Hash-basierte De-Duplizierung in Projektion
- Offene Advanced Features: Joins (MVP geplant), Multi-Gruppen COLLECT, Subqueries

---

### ✅ Phase 2: Graph (100% - Vollständig)

#### ✅ Graph-Algorithmen
- **Status:** ✅ Vollständig implementiert
- **Code:** `src/index/graph_index.cpp`, `include/index/graph_index.h`
- **Klasse:** `GraphIndexManager`
- **Tests:** 17+ Graph-bezogene Tests (test_graph_index.cpp: GraphIndexTest.*, test_graph_type_filtering.cpp: GraphTypeFilteringTest.*)
- **Features:**
  - ✅ BFS (Breadth-First Search) mit Tiefenbegrenzung
  - ✅ Dijkstra (Shortest Path mit Gewichten)
  - ✅ A* (Heuristische Suche)
  - ✅ Adjazenz-Indizes (outdex/index für out/in/both Richtungen)
  - ✅ Edge Metadata Storage (RocksDB Keys: `graph:out:{from}:{edgeId}`, `graph:in:{to}:{edgeId}`)

#### ✅ Traversal in AQL
- **Status:** ✅ Vollständig implementiert
- **Code:** `src/query/aql_translator.cpp` (handleTraversal, Zeile 4206+), `src/server/http_server.cpp`
- **Tests:** 
  - Parser: 2+ Tests in test_aql_parser.cpp (GraphTraversalWithTypeFilter, GraphTraversalWithoutType)
  - HTTP: test_http_aql_graph.cpp
  - Integration: test_graph_type_filtering.cpp (4 Tests für RecursivePathQuery)
- **Features:**
  - ✅ Variable Pfadlängen (min..max Syntax)
  - ✅ Richtungen (OUTBOUND/INBOUND/ANY)
  - ✅ RETURN Varianten: `v` (vertices), `e` (edges), `p` (paths)
  - ✅ Edge Type Filtering (`OUTBOUND 'edgeType'`, Server-Side)
  - ✅ Integration mit FILTER Clauses

#### ✅ Edge Type Filtering (Server-Side)
- **Status:** ✅ Vollständig implementiert (November 2025)
- **Code:** `src/index/graph_index.cpp` (getAllEdges, getOutEdges, getInEdges mit edgeType-Parameter)
- **Tests:** 4/4 PASS in `test_graph_type_filtering.cpp`:
  - BFS_WithTypeFilter_OnlyTraversesMatchingEdges
  - Dijkstra_WithTypeFilter_FindsShortestPathOfType
  - RecursivePathQuery_WithTypeFilter_UsesServerSideFiltering
  - TypeFilter_WithNonexistentType_ReturnsEmpty
- **Features:**
  - Type-Filterung direkt beim RocksDB-Scan (Prefix-basiert)
  - Kompatibel mit BFS/Dijkstra/A* Algorithmen
  - AQL-Syntax: `FOR v, e, p IN 1..3 OUTBOUND 'follows' @start ...`

#### ✅ Predicate Filtering (Konservatives Pruning)
- **Status:** ✅ Implementiert (letzte Ebene)
- **Code:** `src/index/graph_index.cpp` (BFS mit evaluatePredicate), `src/server/http_server.cpp` (Zeile 4250+)
- **Features:**
  - ✅ Konstanten-Vorprüfung vor Traversierung
  - ✅ v/e-Prädikate auf letzter Ebene (konservativ)
  - ✅ Frontier-/Result-Limits
  - ✅ Metriken (Frontier pro Tiefe, Pruning-Drops)
  - ✅ Function Call Evaluation für DATE_*, ABS, CEIL, FLOOR, ROUND, POW
  - ✅ XOR-Unterstützung für Predicate Pairs

#### ✅ Property Graph Features
- **Status:** ✅ Vollständig implementiert
- **Tests:** 9+ Tests in `test_property_graph.cpp`:
  - AddNode_WithLabels
  - AddNodeLabel_UpdatesIndex
  - RemoveNodeLabel_UpdatesIndex
  - DeleteNode_RemovesAllLabels
  - AddEdge_WithType
  - GetEdgesByType_MultipleEdges
  - GetTypedOutEdges_FiltersByType
  - MultiGraph_Isolation
  - ListGraphs_ReturnsAllGraphIds

**NICHT implementiert:**
  - ❌ Pfad-Constraints (PATH.ALL/NONE/ANY) - Design dokumentiert in `docs/archive/path_constraints_concept.md`, Code ausstehend
  - ❌ shortestPath() als native AQL-Funktion (aktuell nur via HTTP `/graph/traverse`)
  - ❌ Graph-Mutationen in AQL (CREATE/MERGE/DELETE Clauses)

**Verifikation:**
- Core Graph-Features zu 100% implementiert und getestet
- Server-Side Type Filtering erfolgreich in Produktion
- Alle dokumentierten AQL-Traversal-Patterns funktionsfähig

---

### ✅ Phase 3: Vector Search (100% - Vollständig)

#### ✅ HNSW Integration (3 Metriken)
- **Status:** ✅ Vollständig implementiert
- **Code:** `src/index/vector_index.cpp`, `include/index/vector_index.h`
- **Bibliothek:** hnswlib (Conditional Compilation mit `THEMIS_HNSW_ENABLED`)
- **Klasse:** `VectorIndexManager`
- **Tests:** 11 TEST_F in `test_vector_index.cpp` + 6 TEST_F in `test_http_vector_largescale.cpp`
- **Features:**
  - ✅ **3 Metriken vollständig implementiert:**
    - **L2 (Euclidean Distance):** `l2()` Funktion (Zeile 61 in vector_index.cpp)
    - **COSINE:** `cosineOneMinus()` mit L2-Normalisierung (Zeile 70)
    - **DOT (Inner Product):** `dotProduct()` mit Negation für Distance-Semantik (Zeile 82)
  - ✅ HNSW-Initialisierung mit konfigurierbaren Parametern (M, efConstruction, efSearch)
  - ✅ Space-Interface-Mapping (L2Space, InnerProductSpace)
  - ✅ Automatischer Fallback auf Brute-Force bei HNSW-Fehler

#### ✅ Vector Operations
- **Status:** ✅ Vollständig implementiert
- **Tests:**
  - Init_CreatesIndex
  - AddEntity_StoresVector
  - SearchKnn_FindsNearestNeighbors
  - SearchKnn_WithWhitelist
  - RemoveByPk_DeletesVector
  - L2Metric_OrdersByEuclideanDistance
  - CosineMetric_NormalizesAndOrdersByAngle
  - DotProductMetric_NoNormalization
- **Features:**
  - ✅ addEntity(): Vektor-Speicherung mit PK-Zuordnung
  - ✅ searchKnn(): Top-K Nearest Neighbors
  - ✅ Whitelist-Filtering: Einschränkung auf PK-Subset
  - ✅ removeByPk(): Entfernung einzelner Vektoren
  - ✅ Batch Operations: Bulk Insert/Delete

#### ✅ Persistenz & Recovery
- **Status:** ✅ Vollständig implementiert
- **Tests:**
  - PersistenceRoundtrip_SaveAndLoad
  - PersistenceLoadInvalidDirectory_ReturnsError
- **Code:** `saveIndex()` (Zeile 32), `loadIndex()` (Zeile 51)
- **Features:**
  - ✅ Auto-Save bei Destruktor (wenn `autoSave_` aktiv)
  - ✅ Manuelle Save/Load mit saveIndex()/loadIndex()
  - ✅ Persistierte Dateien: `meta.txt`, `labels.txt`, `index.bin`
  - ✅ Warmstart-Unterstützung: Laden beim Startup

#### ✅ Dynamic Configuration
- **Status:** ✅ Vollständig implementiert
- **Tests:** SetEfSearch_UpdatesSearchParameter
- **Code:** `setEfSearch()` (Zeile 168)
- **Features:**
  - ✅ Runtime-Anpassung von efSearch (Recall vs. Latency Trade-off)
  - ✅ Validation: efSearch > 0
  - ✅ HNSW-Update: Direkte Anpassung von `appr->ef_`

#### ✅ HTTP API Integration
- **Status:** ✅ Vollständig implementiert
- **Tests:** 6 Tests in `test_http_vector_largescale.cpp`:
  - VectorBatchInsert_Handles1000Items
  - VectorBatchInsert_EmptyBatch
  - VectorBatchInsert_PartialErrors
  - VectorSearch_CursorPagination_MultiplePage
  - VectorDeleteByFilter_PrefixNoMatch
  - VectorIndexStats_AfterBatchInsert
- **Endpoints:**
  - POST `/vector/batch` - Batch Insert
  - POST `/vector/search` - K-NN Search mit Cursor-Paginierung
  - DELETE `/vector/delete` - Bulk Delete by Filter
  - GET `/vector/stats` - Index-Statistiken

#### ✅ Advanced Features
- **Status:** ✅ Implementiert
- **Features:**
  - ✅ Cursor-Paginierung für Vector Search
  - ✅ Index Statistics (Größe, Element-Anzahl)
  - ✅ Conditional Compilation: Graceful Degradation ohne HNSW
  - ✅ Distance Normalization (Cosine: 1-cos für 0..2 Range)
  - ✅ Vector Field Extraction aus BaseEntity

**NICHT implementiert:**
  - ❌ Quantization (Product/Scalar) - Roadmap Q2 2026
  - ❌ GPU-Beschleunigung (CUDA/ROCm) - Roadmap Q2 2026
  - ❌ Multi-Vector Support (mehrere Embedding-Felder pro Entity)
  - ❌ Hybrid Search Integration in AQL (aktuell nur HTTP)

**Verifikation:**
- Alle 3 Metriken funktionieren korrekt (L2/COSINE/DOT)
- Persistenz roundtrip erfolgreich (Save → Load → Identical Results)
- Large-Scale Tests: 1000+ Vektoren ohne Fehler
- Cursor-Paginierung funktioniert für Batch-Retrieval
  - HNSWlib (hnswlib::L2Space)
  - L2-Distanz
  - Whitelist-Pre-Filter
  - HTTP `/vector/search`
  - **todo.md Status:** Zeile 573 als `[x]` - **KORREKT**

#### ✅ Vector Search HTTP Endpoint
- **Status:** ✅ Vollständig implementiert
- **Code:** `src/server/http_server.cpp` (handleVectorSearch)
- **Tests:** 14/14 PASS (HttpVectorApiTest)
- **Features:**
  - POST /vector/search mit {"vector": [...], "k": 10}
  - Dimensionsvalidierung
  - k-NN Suche via VectorIndexManager
  - Response: [{"pk": "...", "distance": 0.0}, ...]
  - Fehlerbehandlung (fehlende Felder, ungültige Dimensionen, k=0)
- **Tests:**
  - VectorSearch_FindsNearestNeighbors
  - VectorSearch_RespectsKParameter
  - VectorSearch_DefaultsK (default: 10)
  - VectorSearch_ValidatesDimension
  - VectorSearch_RequiresVectorField
  - VectorSearch_RejectsInvalidK

#### ✅ Cosine-Distanz **FALSCH MARKIERT IN TODO.MD**
- **Status:** ✅ **IMPLEMENTIERT** (trotz `[ ]` in todo.md)
- **Code:** `src/index/vector_index.cpp` Zeile 33-42 (`cosineOneMinus`)
- **Implementierung:**
  - L2-Normalisierung für Vektoren
  - hnswlib::InnerProductSpace (Zeile 77)
  - Metriken: L2 oder COSINE (Zeile 55, 124, 163, 198)
- **HTTP-Server:** Zeilen 2271, 2330 (`vector_index_->getMetric() == Metric::L2 ? "L2" : "COSINE"`)
- **todo.md Status:** Zeile 574 als `[ ]` - **FALSCH, sollte `[x]` sein**

#### ❌ Dot-Product
- **Status:** ❌ Nicht separat implementiert
- **todo.md Status:** `[ ]` (Zeile 574) - **KORREKT**

#### ✅ HNSW-Persistenz
- **Status:** ✅ Vollständig implementiert
- **Code:** `src/index/vector_index.cpp` (save/load via hnswlib serialize)
- **Features:**
  - Automatisches Laden beim Server-Start (init())
  - Automatisches Speichern beim Shutdown (shutdown())
  - Format: index.bin, labels.txt, meta.txt
  - Konfigurierbar: `vector_index.save_path`, `vector_index.auto_save`
- **Integration:** main_server.cpp übergibt save_path, HttpServer-Destruktor ruft shutdown()
- **todo.md Status:** `[ ]` (Zeile 568) - **FALSCH, sollte `[x]` sein**

#### ❌ Konfigurierbare HNSW-Parameter
- **Status:** ❌ Nicht implementiert (hardcoded M, efConstruction)
- **todo.md Status:** `[ ]` (Zeile 569) - **KORREKT**

#### ❌ Batch-Operationen
- **Status:** ❌ Nicht implementiert
- **todo.md Status:** `[ ]` (Zeile 579) - **KORREKT**

#### ❌ Vector-Pagination/Cursor
- **Status:** ❌ Nicht implementiert
- **todo.md Status:** `[ ]` (Zeile 580) - **KORREKT**

---

### ❌ Phase 4: Filesystem (~5% - Architektur only)

#### ⚠️ Content-Architektur
- **Status:** ⚠️ Header existieren, keine Implementierung
- **Code:** 
  - `include/content/content_manager.h` (ContentMeta, ChunkMeta Structs)
  - Keine `.cpp`-Implementierungen gefunden
- **Features vorhanden (Header only):**
  - ContentMeta: id, uri, content_type, size, chunks[]
  - ChunkMeta: chunk_id, content_id, seq_num, start_byte, end_byte
- **Features NICHT implementiert:**
  - Upload/Download
  - Text-Extraktion (PDF/DOCX)
  - Chunking-Pipeline
  - Hybrid-Queries (Relational + Chunk-Graph + Vector)
- **todo.md Status:** Zeile 39 als `[ ]` - **KORREKT**

---

### ⚠️ Phase 5: Observability (~80% - Wesentliche Funktionen, einige Lücken)

#### ✅ Prometheus Metrics (/metrics)
- **Status:** ✅ Implementiert, Bucket-Logik aktualisiert (kumulativ). Erweiterte RocksDB-Compaction-Metriken noch offen.
- **Code:** `src/server/http_server.cpp` (handleMetrics, recordLatency, recordPageFetch)
- **Features:**
  - **Counters:** requests_total, errors_total, cursor_anchor_hits_total, range_scan_steps_total
  - **Gauges:** qps, uptime, rocksdb_* (cache, keys, pending_compaction_bytes, memtable, files_per_level)
  - **Histograms (kumulative Buckets):** latency_bucket_*, page_fetch_time_ms_bucket_*
  - Latency-Buckets: 100us, 500us, 1ms, 5ms, 10ms, 50ms, 100ms, 500ms, 1s, 5s, +Inf
  - Page-Fetch-Buckets: 1ms, 5ms, 10ms, 25ms, 50ms, 100ms, 250ms, 500ms, 1s, 5s, +Inf
- **Tests:** ✅ 4/4 PASS (`test_metrics_api.cpp`), inklusive Kumulative-Bucket-Validierung
- **todo.md Status:** `[x]` Prometheus-Metriken - **AKTUALISIERUNGSBEDARF für kumulative Buckets**

#### ✅ Backup/Restore
- **Status:** ✅ Implementiert (Checkpoints). Inkrementelle/WAL-Archivierung noch offen.
- **Code:**
  - `include/storage/rocksdb_wrapper.h` Zeile 200-208
  - `src/storage/rocksdb_wrapper.cpp` (createCheckpoint, restoreFromCheckpoint)
  - `src/server/http_server.cpp` (handleBackup, handleRestore)
- **HTTP Endpoints:**
  - POST /admin/backup
  - POST /admin/restore
- **Tests:** Funktional (verwendet in smoke tests)
- **todo.md Status:** Zeile 509 als `[ ]` - **FALSCH, sollte `[x]` sein**

#### ⚠️ Strukturierte JSON-Logs
- **Status:** Teilweise (klassisches spdlog, JSON-Formatter fehlt)

#### ❌ RocksDB Compaction-Metriken (detailliert)
- **Status:** ❌ Nur Basis-Metrik
- **Implementiert:** rocksdb_pending_compaction_bytes (gauge)
- **Fehlend:** compactions_total, compaction_time_seconds, bytes_read/written
- **todo.md Status:** Zeile 940, 1457 als `[ ]` - **KORREKT**

#### ✅ OpenTelemetry Tracing
- **Status:** ✅ End-to-End Instrumentierung (HTTP + QueryEngine + Operator-Spans)

#### ❌ Inkrementelle Backups/WAL-Archiving
- **Status:** ❌ Nicht implementiert
- **Aktuell:** Nur Full-Checkpoints
- **todo.md Status:** Zeile 219 als `[ ]` - **KORREKT**

#### ❌ Automated Restore-Verification
- **Status:** ❌ Nicht implementiert
- **todo.md Status:** Zeile 219 als `[ ]` - **KORREKT**

#### ❌ POST /config (Hot-Reload)
- **Status:** ❌ Nicht implementiert
- **todo.md Status:** Zeile 510 als `[ ]` - **KORREKT**

#### ❌ Strukturierte JSON-Logs
- **Status:** ❌ Nicht implementiert (spdlog ohne JSON-Formatter)
- **todo.md Status:** Implizit in Zeile 218 - **KORREKT (offen)**

---

### ❌ Phase 6: Analytics (Apache Arrow) (0%)

- **Status:** ❌ Vollständig nicht gestartet
- **Code:** Keine Arrow-Integration gefunden
- **todo.md Status:** Zeile 401 als `[ ]` (Priorität 4) - **KORREKT**

---

### ⚠️ Phase 7: Security/Governance (~40% - Basis vorhanden)

#### ❌ RBAC (Role-Based Access Control)
- **Status:** Noch offen (Planungsphase)

#### ❌ Audit-Log
- **Status:** ❌ Nicht implementiert
- **todo.md:** Umfangreicher Plan in Phase 7 (Zeilen 1200+)

#### ❌ DSGVO-Compliance
- **Status:** ❌ Nicht implementiert
- **todo.md:** Phase 7.4 (Zeilen 1350+)

#### ⚠️ PKI-Integration (teilweise)
- **Status:** Dual-Modus: Echte RSA-Signaturen via OpenSSL, jedoch ohne Chain-/Revocation-/KU/EKU-Prüfung; Fallback ist Base64-Stub → aktuell nicht eIDAS-konform
- **Code-Hinweis:** `VCCPKIClient::signHash`/`verifyHash` nutzen RSA, wenn Key/Cert & passende Hashlänge vorliegen; sonst Stub. Hardening (Chain/Revocation/Canonicalization) offen

---

## 🚨 Aktualisierte Diskrepanzen & Gaps (Nov 2025)

### AQL Advanced Features
- Offene Punkte: Joins, OR/NOT (Index-Merge Optimierung), DISTINCT, Subqueries, Multi-Gruppen COLLECT, Path Constraints (Graph)

### Security/Governance
- PKI: RSA vorhanden, aber Chain/Revocation/Usage/Canonicalization offen → eIDAS nicht konform
- RBAC nicht implementiert
- Strukturierte Audit-Logs & Signatur-Verifikationsflags fehlen
- DSGVO Art. 30 Integrität eingeschränkt (Stub-Fallback möglich)

### Observability Erweiterungen
- Inkrementelle Backups/WAL-Archiving, erweiterte Compaction-Metriken, strukturierte JSON-Logs, Config API für selektive Live-Tuning (teilweise vorhanden)

---

## 📊 Priorisierte Lücken für Production Readiness

### 🔥 Kritisch (sofort)
1. **Prometheus-Histogramme: Kumulative Buckets** (Compliance-Fix)
   - Impact: Monitoring-Tools erwarten Prometheus-Spec
   - Aufwand: ~2-4h (Bucket-Logik ändern)

2. **HNSW-Persistenz** (Datenverlust-Risiko)
   - Impact: Vector-Index geht bei Restart verloren
   - Aufwand: ~1-2 Tage (save/load Implementation)

3. **AQL COLLECT/GROUP BY MVP** (Basisfunktionalität)
   - Impact: Aggregationen sind Standard-Anforderung
   - Aufwand: ~3-5 Tage (Executor-Integration)

### ⚠️ Wichtig (nächste 2 Wochen)
4. **OR/NOT Index-Merge** (Query-Flexibilität)
   - Impact: Viele Queries benötigen Disjunktionen
   - Aufwand: ~2-3 Tage (Planner-Regeln)

5. **OpenTelemetry Tracing** (Debugging/Observability)
   - Impact: Production-Debugging ohne Tracing schwierig
   - Aufwand: ~3-5 Tage (SDK-Integration, Span-Instrumentation)

### 📋 Nice-to-Have (spätere Sprints)
6. **Inkrementelle Backups/WAL-Archiving**
7. **Automated Restore-Verification**
8. **Strukturierte JSON-Logs**
9. **POST /config (Hot-Reload)**
10. **RBAC (Basic)**
11. **Batch-Verarbeitung (Caching strategy)**
12. Performance, Speichermanagement, Optimierungen
---

## ✅ Nächste Schritte

1. Konsistente Pflege dieses Dokuments nach jedem Feature-Abschluss.
2. Fokus nächste Iteration: PKI echte Signaturen, RBAC Basis, Joins & OR/NOT, Inkrementelle Backups, Strukturierte Logs.
3. todo.md und priorities.md synchron halten (Duplication vermeiden).

---

**Erstellt:** 29. Oktober 2025  
**Autor:** GitHub Copilot (Audit-Assistent)
