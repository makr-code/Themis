# ThemisDB - Nächste Schritte Analyse
**Datum:** 17. November 2025  
**Basis:** Code-Analyse + Todo-Liste + Implementation Summary  
**Status nach Critical/High-Priority Sprint:** 61% Gesamt-Implementierung

---

## Executive Summary

Nach Abschluss des Critical/High-Priority Sprints (61% Implementierung, Security 45%) sind die **nächsten logischen Schritte**:

**🎯 Priorität 1 (Sofort - Q4 2025):**
1. **AQL Advanced Features** (65% → 85%, 2-3 Wochen)
2. **Content Pipeline** (30% → 60%, 1-2 Wochen)
3. **Admin Tools MVP** (27% → 70%, 2-3 Wochen)

**🎯 Priorität 2 (Q1 2026):**
4. **Inkrementelle Backups** (0% → 90%, 1 Woche)
5. **HSM/eIDAS PKI** (Docs vorhanden → Production, 2 Wochen)

---

## Detaillierte Analyse

### 1. AQL Advanced Features (HÖCHSTE PRIORITÄT)

**Status:** 65% implementiert, AST-Nodes vorhanden, Executor fehlt  
**Impact:** BLOCKER für Production-Workloads (Joins, OR-Queries)  
**Aufwand:** 2-3 Wochen

#### 1.1 LET/Subqueries (CRITICAL)

**Code-Status:**
```cpp
// ✅ Parser vorhanden
// include/query/aql_parser.h:278
struct LetNode {
    std::string variable;
    std::shared_ptr<Expression> expression;
};

// ❌ Executor fehlt
// src/query/aql_translator.cpp:31
if (!ast->let_nodes.empty()) {
    jq.let_nodes = ast->let_nodes;  // Nur Weiterleitung, keine Execution
}
```

**TODO-Marker im Code:**
- `src/query/aql_translator.cpp:31` - LET-Node Handling nicht implementiert
- Docs erwähnen LET als "MVP seit 31.10.2025" → FALSCH, nur Parser existiert

**Implementierungs-Schritte:**
1. **LET Evaluator** (4-6h)
   ```cpp
   // src/query/let_evaluator.cpp
   class LetEvaluator {
       std::unordered_map<std::string, nlohmann::json> bindings_;
   public:
       void evaluateLet(const LetNode& node, const nlohmann::json& current_doc);
       nlohmann::json resolveVariable(const std::string& var_name);
   };
   ```

2. **Integration in Query Engine** (2-3h)
   - Add LET evaluator to query execution pipeline
   - Variable resolution in FILTER/RETURN expressions

3. **Tests** (3-4h)
   - Unit tests: LET mit Arithmetik, String-Ops, Nested Objects
   - Integration tests: LET + FILTER, LET in Joins
   - Edge cases: Undefined variables, circular dependencies

**DoD:**
- ✅ LET bindings funktionieren in FOR/FILTER/RETURN
- ✅ Mehrere LETs pro Query
- ✅ LETs können frühere LETs referenzieren
- ✅ 15+ Tests PASSING

**Files zu ändern:**
- `src/query/aql_translator.cpp` - LET evaluation logic
- `src/query/query_engine.cpp` - Variable resolution
- `tests/test_aql_let.cpp` - NEW

---

#### 1.2 OR/NOT mit Index-Merge (HIGH)

**Code-Status:**
```cpp
// ⚠️ OR teilweise implementiert (DNF-Konvertierung)
// ❌ Index-Merge fehlt
// src/query/aql_translator.cpp - Nur AND-Pfad nutzt Indexes
```

**TODO-Marker im Code:**
- `docs/query_engine_aql.md:657` - "OR-Support: Nur AND im Translator (OR in Arbeit)"
- `docs/aql_syntax.md:45` - "OR-Operator: Vollständig unterstützt über DNF-Konvertierung" → TEILWEISE FALSCH

**Implementierungs-Schritte:**
1. **Index-Merge für OR** (6-8h)
   ```cpp
   // Pseudo-Code
   if (condition is OR) {
       std::vector<std::string> result_set_1 = index_scan(left_predicate);
       std::vector<std::string> result_set_2 = index_scan(right_predicate);
       return union(result_set_1, result_set_2);  // Sorted merge
   }
   ```

2. **NOT Operator** (3-4h)
   - Inverted index scan
   - Full scan mit NOT filter als fallback

3. **Query Optimizer** (4-6h)
   - Cost estimation für OR (index merge vs. full scan)
   - Predicate reordering für OR expressions

4. **Tests** (4-5h)
   - OR mit 2 indexes
   - OR mit index + full scan
   - NOT mit index
   - Complex: (A AND B) OR (C AND D)

**DoD:**
- ✅ OR nutzt Index-Merge wenn beide Seiten indexable
- ✅ NOT nutzt inverted scans wo möglich
- ✅ Query Optimizer wählt optimale Strategy
- ✅ 20+ Tests PASSING

**Files zu ändern:**
- `src/query/aql_translator.cpp` - OR logic
- `src/query/query_optimizer.cpp` - Cost model
- `src/index/secondary_index.cpp` - Index merge utilities
- `tests/test_aql_or_not.cpp` - NEW

---

#### 1.3 Advanced Joins (MEDIUM)

**Code-Status:**
```cpp
// ✅ Nested-Loop Join implementiert
// ❌ Hash-Join fehlt
// ❌ Sort-Merge-Join fehlt
```

**Implementierungs-Schritte:**
1. **Hash-Join** (8-10h)
   - Build phase: Hash left side
   - Probe phase: Lookup right side
   - Memory management (spillover to disk if needed)

2. **Join Strategy Selection** (4-6h)
   - Small left + Large right → Hash-Join
   - Both sorted → Sort-Merge-Join
   - Small datasets → Nested-Loop (current)

3. **Tests** (6-8h)
   - Hash-Join vs Nested-Loop benchmarks
   - Large dataset tests (10k x 10k)
   - Memory spillover tests

**DoD:**
- ✅ Hash-Join für große Joins (>1000 rows)
- ✅ Optimizer wählt Join-Strategy
- ✅ 10x Speedup für große Joins
- ✅ Benchmarks validiert

**Files zu ändern:**
- `src/query/join_executor.cpp` - NEW
- `src/query/query_optimizer.cpp` - Join strategy
- `benchmarks/bench_joins.cpp` - NEW

---

### 2. Content Pipeline Vervollständigen (HIGH)

**Status:** 30% implementiert, Basis-Schema vorhanden  
**Impact:** RAG/Hybrid-Search Workloads blockiert  
**Aufwand:** 1-2 Wochen

#### 2.1 Advanced Extraction (PDF/DOCX/XLSX)

**Code-Status:**
```cpp
// ✅ Text Processor vorhanden (src/content/text_processor.cpp)
// ✅ Mock CLIP Processor (src/content/mock_clip_processor.cpp)
// ❌ Keine echten PDF/DOCX Parser
```

**TODO-Marker im Code:**
- `src/api/http_server.cpp:4` - "TODO: Implement in Phase 4, Task 11"
- Content-Pipeline nur Mockups

**Implementierungs-Schritte:**
1. **PDF Extraction** (6-8h)
   - Library: poppler-cpp oder pdfium
   - Text + Metadata (author, created, pages)
   - Image extraction für multi-modal

2. **DOCX Extraction** (4-6h)
   - Library: libxml2 (OpenXML parsing)
   - Text + Styles + Metadata

3. **XLSX Extraction** (4-6h)
   - Library: xlnt oder libxlsx
   - Tabellen → JSON/CSV

4. **Tests** (4-5h)
   - Real-world PDFs (100+ pages)
   - Complex DOCX (images, tables, formulas)
   - Large XLSX (10k rows)

**DoD:**
- ✅ PDF/DOCX/XLSX extraction funktioniert
- ✅ Metadata preservation
- ✅ Error handling für corrupted files
- ✅ Integration mit ContentManager

**Files zu ändern:**
- `src/content/pdf_processor.cpp` - NEW
- `src/content/docx_processor.cpp` - NEW
- `src/content/xlsx_processor.cpp` - NEW
- `CMakeLists.txt` - Add poppler/libxml2/xlnt
- `vcpkg.json` - Add dependencies

---

#### 2.2 Chunking Optimierung

**Code-Status:**
```cpp
// ⚠️ Basis-Chunking vorhanden
// ❌ Keine semantische Chunking-Strategies
```

**Implementierungs-Schritte:**
1. **Semantic Chunking** (6-8h)
   - Sentence-level chunking (NLTK/spaCy)
   - Paragraph-preserving chunking
   - Sliding window mit overlap

2. **Chunk Metadata** (3-4h)
   - Position tracking (start_offset, end_offset)
   - Parent-child relationships
   - Chunk embeddings

3. **Batch Upload Optimization** (4-6h)
   - Parallel chunk processing (Intel TBB)
   - RocksDB WriteBatch für bulk inserts

**DoD:**
- ✅ 3 Chunking-Strategies (fixed-size, sentence, paragraph)
- ✅ Chunk metadata vollständig
- ✅ 10x faster bulk upload
- ✅ Tests PASSING

**Files zu ändern:**
- `src/content/chunking_strategy.cpp` - NEW
- `src/content/content_manager.cpp` - Batch optimization
- `tests/test_chunking.cpp` - NEW

---

### 3. Admin Tools MVP (MEDIUM)

**Status:** 27% implementiert (nur AuditLogViewer produktiv)  
**Impact:** Operations, Compliance, DSGVO  
**Aufwand:** 2-3 Wochen

#### 3.1 Tool-Status Audit

**Aktuelle Tools (WPF .NET 8):**

| Tool | Code Status | Backend API | Tests | % |
|------|-------------|-------------|-------|---|
| AuditLogViewer | ✅ Implementiert | ✅ `/audit/logs` | ✅ | 90% |
| SAGAVerifier | ✅ Implementiert | ✅ `/saga/batches` | ⚠️ Minimal | 70% |
| PIIManager | ✅ Implementiert | ✅ `/pii/*` | ⚠️ Minimal | 60% |
| KeyRotationDashboard | ✅ MVP (Demo-Daten) | ✅ `/keys/*` | ❌ | 40% |
| RetentionManager | ✅ MVP (Demo-Daten) | ⚠️ Teilweise | ❌ | 30% |
| ClassificationDashboard | ✅ MVP (Demo-Daten) | ✅ `/classification/*` | ❌ | 40% |
| ComplianceReports | ✅ MVP (Demo-Daten) | ✅ `/reports/*` | ❌ | 40% |

**Durchschnitt:** 27% (stark durch fehlende Tests und echte Backend-Integration gezogen)

#### 3.2 Kritische Gaps

**Backend-APIs fehlen:**
- ✅ `/pii/*` - VORHANDEN (implementiert in Critical Sprint)
- ✅ `/keys/*` - VORHANDEN
- ✅ `/classification/*` - VORHANDEN
- ⚠️ `/retention/*` - TEILWEISE (ContinuousAggregateManager vorhanden, kein HTTP-Endpoint)
- ✅ `/reports/*` - VORHANDEN

**Action Items:**
1. **Retention API Endpoint** (4-6h)
   ```cpp
   // src/server/http_server.cpp
   CROW_ROUTE(app, "/api/retention/policies").methods("GET"_method)
   CROW_ROUTE(app, "/api/retention/policies").methods("POST"_method)
   CROW_ROUTE(app, "/api/retention/execute").methods("POST"_method)
   ```

2. **Integration Tests** (8-10h)
   - E2E tests für jedes Tool
   - Mock Backend → Real Backend migration

3. **Deployment Scripts** (3-4h)
   - MSI Installer (WiX Toolset)
   - Auto-Update mechanism

**DoD:**
- ✅ Alle 7 Tools mit Live-Backend verbunden
- ✅ Integration tests PASSING
- ✅ Deployment-ready MSI

**Files zu ändern:**
- `src/server/http_server.cpp` - Retention endpoints
- `tools/*/ViewModels/*.cs` - Remove mock data
- `tools/deployment/build.ps1` - NEW

---

### 4. Inkrementelle Backups (CRITICAL for Production)

**Status:** 0% implementiert (nur RocksDB Checkpoints)  
**Impact:** Data loss prevention, disaster recovery  
**Aufwand:** 1 Woche

#### 4.1 WAL-Archiving

**Code-Status:**
```cpp
// ✅ RocksDB Checkpoints implementiert
// ❌ Keine WAL-Archivierung
// ❌ Keine Point-in-Time Recovery
```

**TODO-Marker:**
- `docs/development/todo.md:60` - "Inkrementelle Backups / WAL-Archiving — TODO"

**Implementierungs-Schritte:**
1. **WAL Archive Manager** (8-10h)
   ```cpp
   class WALArchiveManager {
       void archiveWAL(const std::string& wal_file, const std::string& archive_path);
       void restoreFromWAL(const std::string& archive_path, uint64_t target_timestamp);
       std::vector<WALFile> listArchivedWALs();
   };
   ```

2. **Incremental Backup** (6-8h)
   - Copy only changed WAL files since last backup
   - Manifest file (backup_manifest.json) with timestamps

3. **Point-in-Time Recovery** (8-10h)
   - Restore checkpoint + replay WAL files until target timestamp
   - Verify data integrity after recovery

4. **Automated Backup Jobs** (4-6h)
   - Cron-style scheduler (every 6h, daily, weekly)
   - Retention policy (keep last 7 dailies, 4 weeklies, 12 monthlies)

5. **Cloud Storage Integration** (6-8h)
   - S3 upload via aws-sdk-cpp
   - Azure Blob Storage via azure-storage-cpp
   - Google Cloud Storage via google-cloud-cpp

**DoD:**
- ✅ Incremental backups funktionieren
- ✅ Point-in-Time Recovery tested
- ✅ S3/Azure/GCS upload
- ✅ Automated schedules
- ✅ Restore tests PASSING

**Files zu ändern:**
- `include/backup/wal_archive_manager.h` - NEW
- `src/backup/wal_archive_manager.cpp` - NEW
- `src/backup/backup_scheduler.cpp` - NEW
- `src/server/http_server.cpp` - Backup endpoints
- `tests/test_backup_restore.cpp` - NEW

---

### 5. HSM/eIDAS PKI Production-Ready (HIGH)

**Status:** Docs vorhanden (1,111 lines), keine HSM-Integration  
**Impact:** Qualified eIDAS signatures für Production  
**Aufwand:** 2 Wochen

#### 5.1 Vault HSM Integration

**Code-Status:**
```cpp
// ✅ VaultKeyProvider vorhanden (src/security/vault_key_provider.cpp)
// ✅ PKIClient vorhanden (src/security/vcc_pki_client.cpp)
// ❌ Keine HSM-Integration
```

**TODO-Marker:**
- `src/security/vcc_pki_client.cpp:348` - "TODO: Implement full X.509 chain validation"
- `docs/development/todo.md:60` - "eIDAS-konforme Signaturen / PKI Integration (Produktiv-Ready mit HSM) — TODO"

**Implementierungs-Schritte:**
1. **Vault Transit Engine** (6-8h)
   ```cpp
   class VaultHSMProvider : public PKIClient {
       std::string sign(const std::string& data) override {
           // POST /v1/transit/sign/my-key
           // HSM-backed signing
       }
   };
   ```

2. **X.509 Chain Validation** (4-6h)
   - OpenSSL X509_verify_cert()
   - CRL checking
   - OCSP validation

3. **Qualified Timestamp Authority** (6-8h)
   - RFC 3161 timestamp requests
   - Timestamp verification
   - Integration mit SAGA events

4. **eIDAS Compliance Tests** (8-10h)
   - Qualified signature validation
   - Timestamp validation
   - Full audit trail test

**DoD:**
- ✅ Vault Transit Engine integration
- ✅ X.509 chain validation
- ✅ Qualified TSA integration
- ✅ eIDAS compliance validated
- ✅ Production deployment guide

**Files zu ändern:**
- `src/security/vault_hsm_provider.cpp` - NEW
- `src/security/vcc_pki_client.cpp` - X.509 validation
- `src/utils/timestamp_authority.cpp` - NEW
- `tests/test_eid as_compliance.cpp` - NEW

---

## Prioritäten-Matrix

| Task | Business Value | Technical Complexity | Effort | Priority |
|------|----------------|---------------------|--------|----------|
| **LET/Subqueries** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | 2-3 days | **P0** |
| **OR/NOT Index-Merge** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ | 3-4 days | **P0** |
| **PDF/DOCX Extraction** | ⭐⭐⭐⭐ | ⭐⭐⭐ | 2-3 days | **P1** |
| **Incremental Backups** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ | 5-7 days | **P1** |
| **Admin Tools Integration** | ⭐⭐⭐ | ⭐⭐ | 3-4 days | **P2** |
| **Hash-Join** | ⭐⭐⭐ | ⭐⭐⭐⭐ | 4-5 days | **P2** |
| **HSM/eIDAS** | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | 10-12 days | **P2** |
| **Chunking Optimization** | ⭐⭐⭐ | ⭐⭐ | 2-3 days | **P3** |

---

## Empfohlene Roadmap

### Sprint 1 (Week 1-2): AQL Advanced Features
**Ziel:** AQL von 65% auf 85%

- Day 1-3: LET/Subqueries implementieren + tests
- Day 4-7: OR/NOT mit Index-Merge
- Day 8-10: Advanced Joins (Hash-Join Basis)

**Deliverable:** AQL Production-Ready für komplexe Queries

---

### Sprint 2 (Week 3-4): Content Pipeline + Backups
**Ziel:** Content 30% → 60%, Backups 0% → 90%

- Day 1-4: PDF/DOCX/XLSX Extraction
- Day 5-6: Chunking Optimization
- Day 7-10: WAL-Archiving + Point-in-Time Recovery

**Deliverable:** RAG-Ready Content Pipeline, Production Backups

---

### Sprint 3 (Week 5-6): Admin Tools + HSM
**Ziel:** Admin Tools 27% → 70%, HSM Integration

- Day 1-4: Admin Tools Backend-Integration + Tests
- Day 5-10: Vault HSM + eIDAS Compliance

**Deliverable:** Operations-Ready Admin Suite, Qualified Signatures

---

## Code-TODOs Priorisiert

### CRITICAL (Sprint 1)
1. ✅ `src/query/aql_translator.cpp:31` - LET execution
2. ✅ `src/query/query_optimizer.cpp` - OR cost model
3. ✅ `src/index/secondary_index.cpp` - Index merge utilities

### HIGH (Sprint 2)
4. ✅ `src/content/pdf_processor.cpp` - NEW (PDF extraction)
5. ✅ `src/backup/wal_archive_manager.cpp` - NEW (WAL archiving)
6. ✅ `src/server/http_server.cpp` - Retention endpoints

### MEDIUM (Sprint 3)
7. ✅ `src/security/vault_hsm_provider.cpp` - NEW (HSM integration)
8. ✅ `src/security/vcc_pki_client.cpp:348` - X.509 validation
9. ✅ `tools/*/ViewModels/*.cs` - Remove mock data

---

## Success Metrics

### Sprint 1 Goals:
- ✅ AQL: 85% implementation (up from 65%)
- ✅ LET: 15+ tests PASSING
- ✅ OR: 20+ tests PASSING
- ✅ Hash-Join: 10x speedup on large joins

### Sprint 2 Goals:
- ✅ Content: 60% implementation (up from 30%)
- ✅ PDF/DOCX: Real-world extraction works
- ✅ Backups: Point-in-Time Recovery validated
- ✅ Automated backup jobs running

### Sprint 3 Goals:
- ✅ Admin Tools: 70% implementation (up from 27%)
- ✅ All 7 tools with live backends
- ✅ HSM: Vault Transit Engine integrated
- ✅ eIDAS: Qualified signatures validated

**Overall Target:** 70% Gesamt-Implementierung (up from 61%)

---

## Abhängigkeiten

### External Libraries zu installieren:
- poppler-cpp (PDF extraction)
- libxml2 (DOCX extraction)
- xlnt (XLSX extraction)
- aws-sdk-cpp (S3 backups)
- azure-storage-cpp (Azure backups)
- google-cloud-cpp (GCS backups)

### vcpkg.json Updates:
```json
{
  "dependencies": [
    "poppler",
    "libxml2",
    "xlnt",
    "aws-sdk-cpp[s3]",
    "azure-storage-cpp",
    "google-cloud-cpp[storage]"
  ]
}
```

---

## Risiken & Mitigations

| Risiko | Impact | Wahrscheinlichkeit | Mitigation |
|--------|--------|-------------------|-----------|
| **LET-Implementierung komplex** | HIGH | MEDIUM | Start mit einfachen Expressions, schrittweise erweitern |
| **Index-Merge Performance** | MEDIUM | LOW | Benchmarks parallel zur Entwicklung |
| **PDF-Library Integration** | MEDIUM | MEDIUM | POC mit poppler vor vollständiger Integration |
| **HSM-Kosten** | HIGH | LOW | Dev-Umgebung mit Mock HSM, Production-Tests separat |
| **Backup-Storage-Kosten** | MEDIUM | MEDIUM | Retention policies implementieren (auto-delete old backups) |

---

## Fazit

**Empfohlene Next Steps (Reihenfolge):**

1. **JETZT:** LET/Subqueries (3 Tage) - BLOCKER für Production
2. **DANN:** OR/NOT Index-Merge (4 Tage) - BLOCKER für komplexe Queries
3. **PARALLEL:** Incremental Backups (5 Tage) - CRITICAL für Production
4. **DANACH:** Content Pipeline (3 Tage) - Enables RAG
5. **SPÄTER:** Admin Tools + HSM (2 Wochen) - Operations Excellence

**Total Aufwand:** ~6 Wochen für alle P0/P1 Tasks  
**Expected Outcome:** 70% Gesamt-Implementierung, Production-Ready AQL, Operations Excellence
