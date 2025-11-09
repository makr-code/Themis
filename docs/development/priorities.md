# Themis - Priorisierte Roadmap für Production Readiness
**Stand:** 09. November 2025  
**Basis:** Aktualisiertes IMPLEMENTATION_STATUS.md & Code-Audit (PKI/Ranger)

---

## 🎯 Entscheidungsmatrix: Nächste Schritte

| Feature | Impact | Aufwand | Risiko | Prio | Empfehlung |
|---------|--------|---------|--------|------|------------|
| ~~Prometheus Histogramme (kumulative Buckets)~~ | Mittel | 2-4h | Niedrig | ✅ | Abgeschlossen |
| ~~HNSW Persistenz~~ | Hoch | 1-2 Tage | Mittel | ✅ | Abgeschlossen |
| ~~COLLECT/GROUP BY MVP~~ | Hoch | 3-5 Tage | Mittel | ✅ | Abgeschlossen |
| ~~Vector Search HTTP Endpoint~~ | Hoch | 1-2 Tage | Niedrig | ✅ | Abgeschlossen |
| ~~OpenTelemetry Tracing~~ | Mittel | 3-5 Tage | Niedrig | ✅ | Abgeschlossen |
| PKI: Echte RSA/X.509 Signaturen | Hoch (Compliance) | 5-7 Tage | Hoch | 🔥 P0 | Start sofort |
| RBAC (Basic Rollen + Scopes) | Hoch (Security) | 7-10 Tage | Hoch | 🔥 P0 | Planung + Implementierung |
| Ranger Adapter Hardening (Pooling, Backoff, Timeouts Erweiterung) | Mittel | 2-3 Tage | Mittel | ⚠️ P1 | Nach PKI/RBAC |
| Inkrementelle Backups + WAL-Archiving | Mittel | 5-7 Tage | Mittel | ⚠️ P1 | Daten-Schutz |
| Strukturierte JSON-Logs (Audit & Request) | Mittel | 2-3 Tage | Niedrig | ⚠️ P1 | Sichtbarkeit |
| AQL Joins & OR/NOT & DISTINCT | Hoch (Abfrageflexibilität) | 6-9 Tage | Mittel | ⚠️ P1 | Parallel zu Ops |
| Multi-Gruppen COLLECT & Subqueries | Mittel | 5-8 Tage | Mittel | 📋 P2 | Nach Basis-Flex Features |
| Graph Path Constraints (PATH.ALL/NONE/ANY) | Mittel | 3-5 Tage | Niedrig | 📋 P2 | Graph Erweiterung |
| Vector Quantization (SQ/PQ) | Mittel | 7-12 Tage | Mittel | 📋 P2 | Performance bei >1M Vektoren |
| Apache Arrow Integration | Niedrig | 10-15 Tage | Mittel | 📋 P3 | Analytics langfristig |

**Status Update (09. November 2025):**
- ✅ Alle bisherigen Infrastruktur-P0 abgeschlossen (Persistenz, Tracing, Metrics, Aggregationen)
- ✅ LET Runtime für AQL hinzugefügt (FILTER + SORT + RETURN)
- ✅ BM25(doc) AQL Scoring integriert
- 🔥 Neue P0-Fokusbereiche: PKI echte Signaturen & RBAC Basis
  - ✅ Infrastruktur: Tracer-Wrapper, OTLP HTTP Exporter, CMake integration
  - ✅ HTTP-Handler instrumentiert (7 Endpoints)
  - ✅ QueryEngine instrumentiert (11 Methoden + Child-Spans)
  - ✅ AQL-Operator-Pipeline instrumentiert (parse, translate, for, filter, limit, collect, return, traversal+bfs)
  - ✅ Dokumentation aktualisiert (docs/tracing.md)
  - Build erfolgreich, Server-Test bestanden
  - **ALLE P1-TASKS ABGESCHLOSSEN!**

**Abgeschlossene Kern-Features:**
- HNSW Persistenz & Dynamic Config
- COLLECT/GROUP BY MVP
- Prometheus (kumulative Buckets)
- OpenTelemetry Tracing
- Vector Search Endpoints & Hybrid Fusion
- LET Runtime & BM25(doc) Scoring

**Legende:**
- 🔥 P0 = Kritisch (sofort/diese Woche) - ✅ **ALLE ERLEDIGT**
- ⚠️ P1 = Wichtig (nächste 2 Wochen) - ✅ **ALLE ERLEDIGT**
- 📋 P2 = Nice-to-Have (nächster Sprint) - **NÄCHSTE PHASE**
- 📋 P3 = Backlog (zukünftig)

---

## 🚀 Empfohlene Reihenfolge (Nächste Iteration: KW 46)

### Phase 1 (Security & Compliance)
1. PKI echte RSA/X.509 Signaturen (Sign/Verify, Zertifikatskette prüfen)
2. RBAC Basis (Rollen, Ressourcen-Typen: collection, graph, vector; Operationen: read/write/admin)

### Phase 2 (Ops & Observability)
3. Ranger Adapter Hardening (Pooling via CURLSH_SHARE, Retry Exponential Backoff mit Jitter, konfigurierbare Timeouts)
4. Strukturierte JSON-Logs (Request + Audit + Error Payloads)
5. Inkrementelle Backups (Differential + WAL-Archiving)

### Phase 3 (Query Flex & Performance)
6. AQL: Joins (Nested LOOP, Early Filter Pushdown)
7. AQL: OR/NOT (Index-Merge & Predicate Tree Normalisierung)
8. AQL: DISTINCT (Hash-Set Pipeline vor LIMIT)
9. Multi-Gruppen COLLECT (Composite Key Hash)

### Phase 4 (Graph & Vector Erweiterung)
10. Graph Path Constraints (Pruning-Regeln implementieren)
11. Vector Quantization (SQ8 erstmal, speicherorientiert)

### Phase 5 (Longer-Term)
12. Subqueries (Execution Context Stack)
13. Apache Arrow Integration (Columnar Export + SIMD Aggregationen)
```
Tag 1-2:  Prometheus Histogramme (kumulative Buckets) ✅
Tag 2-4:  OR/NOT Index-Merge (Query-Flexibilität) ✅
Tag 5-7:  HNSW Persistenz (Datenverlust-Risiko eliminieren) ✅
```
**Ergebnis:** Alle P0-Features implementiert und getestet!

### Batch 2: P1 Features (Diese/Nächste Woche)

```
Tag 1-2:  OpenTelemetry Tracing - Infrastruktur ✅
Tag 2-3:  OpenTelemetry Tracing - Instrumentierung (HTTP, Query)
Tag 4-5:  Jaeger Integration testen + Dokumentation
```

### Option B: Strategische Features zuerst (Fundamentals)
```
Tag 1-5:  COLLECT/GROUP BY MVP (Basisfunktionalität)
Tag 6-7:  HNSW Persistenz (Datenverlust-Risiko)
Tag 8:    Prometheus Histogramme (Quick Win zum Abschluss)
```
**Vorteil:** Kernfunktionalität (Aggregationen) schnell verfügbar  
**Nachteil:** Längerer initialer Entwicklungszyklus

### Option C: Risiko-Minimierung zuerst (Defensive)
```
Tag 1-2:  HNSW Persistenz (Datenverlust-Risiko eliminieren)
Tag 3-7:  COLLECT/GROUP BY MVP (Basisfunktionalität)
Tag 8:    Prometheus Histogramme (Quick Win)
```
**Vorteil:** Kritische Risiken (Datenverlust) sofort adressiert  
**Nachteil:** Komplexes Feature am Anfang (HNSW save/load)

---

## 🔍 Detaillierte Analyse: Top 3 Features

### 1️⃣ Prometheus Histogramme (kumulative Buckets)

**Problem:**
- Aktuelle Implementation: Non-kumulative Buckets (jeder Bucket zählt nur seinen Range)
- Prometheus-Spec: Buckets müssen kumulativ sein (`le="100"` = alle Werte ≤ 100)
- Impact: Grafana/Prometheus-Tools zeigen falsche Percentiles

**Lösung:**
```cpp
// Aktuell (FALSCH):
if (ms <= 1) page_bucket_1ms_++;
else if (ms <= 5) page_bucket_5ms_++;
// ...

// Korrekt (KUMULATIV):
if (ms <= 1) page_bucket_1ms_++;
if (ms <= 5) page_bucket_5ms_++;
if (ms <= 10) page_bucket_10ms_++;
// ... (jeder Wert inkrementiert ALLE passenden Buckets)
```

**Aufwand:**
- Änderungen: `http_server.cpp` (recordLatency, recordPageFetch)
- Tests: Smoke-Test erweitern (Bucket-Prüfung)
- Doku: README.md aktualisieren
- **Geschätzt: 2-4 Stunden**

**DoD (Definition of Done):**
- [ ] recordLatency() verwendet kumulative Bucket-Logik
- [ ] recordPageFetch() verwendet kumulative Bucket-Logik
- [ ] Smoke-Test validiert: Wert 150ms → buckets 1,5,10,25,50,100,250,500,1000,5000,Inf alle ≥ 1
- [ ] README.md Histogram-Beschreibung korrigiert

---

### 2️⃣ HNSW Persistenz (save/load)

**Problem:**
- Vector-Index ist nur In-Memory
- Server-Restart → alle Vektoren weg
- Manuelles Rebuild nötig (Performance-Impact)

**Lösung:**
```cpp
// HNSWlib API:
index_->saveIndex("data/vector_index_<collection>.bin");
index_->loadIndex("data/vector_index_<collection>.bin", space_, max_elements_);
```

**Implementierung:**
1. **Startup:** `VectorIndexManager::init()` prüft auf existierende `.bin`, lädt wenn vorhanden
2. **Shutdown:** `VectorIndexManager::shutdown()` speichert Index
3. **Background Save:** Optional: Periodisches Save alle N Minuten
4. **Versioning:** Filename-Schema: `<collection>_v<version>.bin`

**Aufwand:**
- Code: `vector_index.cpp` (init/shutdown/save/load)
- Tests: `test_vector_index.cpp` (save → restart → load → verify results)
- Config: `config.json` (vector_save_interval_minutes)
- **Geschätzt: 1-2 Tage**

**DoD:**
- [ ] `saveIndex()` speichert bei Shutdown
- [ ] `loadIndex()` lädt bei Startup (wenn vorhanden)
- [ ] Test: Add 100 Vektoren → Restart → Search findet alle
- [ ] Config-Option: `vector_auto_save: true/false`

---

### 3️⃣ COLLECT/GROUP BY MVP

**Problem:**
- Aggregationen sind SQL/AQL-Standard-Feature
- AST-Node (`CollectNode`) existiert, aber keine Executor-Integration
- Queries wie `SELECT city, COUNT(*) FROM users GROUP BY city` unmöglich

**Lösung (MVP-Scope):**
```aql
// Beispiel:
FOR doc IN orders
  FILTER doc.created_at >= "2025-01-01"
  COLLECT city = doc.city
  AGGREGATE 
    total = SUM(doc.amount),
    count = COUNT()
  RETURN {city, total, count}
```

**Implementierung:**
1. **Parser:** `CollectNode` parsing (bereits vorhanden in AST)
2. **Translator:** `handleCollect()` in `aql_translator.cpp`
3. **Executor:** 
   - Hash-Map für Gruppierung: `std::unordered_map<string, AggregateState>`
   - Aggregat-Funktionen: COUNT, SUM, AVG, MIN, MAX
   - Streaming-Execution (keine Full-Scan-Materialisierung)
4. **Tests:** Unit-Tests + HTTP-Integration-Tests

**Aufwand:**
- Code: `aql_translator.cpp`, `query_engine.cpp`
- Tests: `test_aql_translator.cpp` (mindestens 10 neue Tests)
- Doku: `docs/aql_syntax.md` aktualisieren
- **Geschätzt: 3-5 Tage**

**MVP-Scope (Reduktion):**
- ✅ Einspaltige Gruppierung (`COLLECT city = doc.city`)
- ✅ Basis-Aggregat-Funktionen (COUNT, SUM, AVG, MIN, MAX)
- ❌ Mehrspaltige Gruppierung (später)
- ❌ HAVING-Filter (später)
- ❌ KEEP/WITH COUNT (später)

**DoD:**
- [ ] `COLLECT field = expr` funktioniert
- [ ] `AGGREGATE count = COUNT()` funktioniert
- [ ] `AGGREGATE sum = SUM(field)` funktioniert
- [ ] Unit-Tests: 10+ Test-Cases PASS
- [ ] HTTP-Test: End-to-End GROUP BY Query
- [ ] Doku: Beispiel in `docs/aql_syntax.md`

---

## 📊 Impact-Analyse

### Business Value
1. **COLLECT/GROUP BY:** ⭐⭐⭐⭐⭐ (Kernfunktionalität, Kundenerwartung)
2. **HNSW Persistenz:** ⭐⭐⭐⭐ (Datenverlust-Risiko, Produktionsfähigkeit)
3. **Prometheus Histogramme:** ⭐⭐⭐ (Observability, Ops-Qualität)

### Technical Debt Reduction
1. **Prometheus Histogramme:** ⭐⭐⭐⭐⭐ (Compliance-Fix, behebt Spec-Verletzung)
2. **HNSW Persistenz:** ⭐⭐⭐⭐ (Architektur-Lücke schließen)
3. **COLLECT/GROUP BY:** ⭐⭐⭐ (AST-Code-Completion)

### Risk Mitigation
1. **HNSW Persistenz:** ⭐⭐⭐⭐⭐ (Datenverlust-Risiko eliminieren)
2. **COLLECT/GROUP BY:** ⭐⭐ (Feature-Lücke, kein direktes Risiko)
3. **Prometheus Histogramme:** ⭐⭐ (Monitoring-Fehler, aber nicht kritisch)

---

## ✅ Empfehlung: Hybrid-Ansatz

**Woche 1 (Tag 1-7):**
```
🔥 Tag 1 (2-4h):    Prometheus Histogramme (Quick Win, Motivation)
🔥 Tag 1-3 (2 Tage): HNSW Persistenz (Risiko-Minimierung)
🔥 Tag 4-7 (4 Tage): COLLECT/GROUP BY MVP (Strategisch wichtig)
```

**Rationale:**
1. **Quick Win am Anfang:** Momentum, sichtbarer Fortschritt nach 4h
2. **Risiko-Minimierung:** HNSW-Persistenz vor Wochenende fertig
3. **Strategisches Feature:** COLLECT/GROUP BY nutzt volle Woche

**Erwartete Ergebnisse (Ende Woche 1):**
- ✅ Prometheus-konforme Histogramme
- ✅ Vector-Index überleben Server-Restart
- ✅ Basis-Aggregationen (COLLECT/COUNT/SUM) funktional
- 📈 Production-Readiness-Score: ~35% → ~55%

---

## 📅 Backlog (Woche 2+)

**Woche 2:**
- OR/NOT Index-Merge (2-3 Tage)
- OpenTelemetry Tracing (3-5 Tage)

**Sprint 2:**
- Inkrementelle Backups/WAL-Archiving
- Automated Restore-Verification
- Strukturierte JSON-Logs

**Sprint 3:**
- RBAC (Basic)
- Query/Plan-Cache
- POST /config (Hot-Reload)

**Langfristig:**
- Apache Arrow Integration
- Phase 4 (Filesystem/Content) Start
- Phase 7 (Security/Governance) Vollausbau

---

## 🔥 Top Offene Gaps (Snapshot 09.11.2025)
- PKI echte Signaturen (Compliance, Audit Integrität)
- RBAC Basis (Rollen/Scopes, Autorisierungslayer)
- Inkrementelle Backups/WAL-Archiving (RPO Verbesserung)
- Strukturierte JSON-Logs (bessere Analyse & Korrelation)
- AQL Joins & OR/NOT & DISTINCT (Query-Flexibilität)
- Ranger Adapter Hardening (Resilienz & Performance)

## ✅ Definition of Done P0 (Security & RBAC)
- Signaturen: OpenSSL RSA Sign/Verify mit SHA-256; Zertifikatskette validiert
- PKIClient ersetzt Stub (kein Base64-Fake)
- RBAC: Policy-Store (RocksDB CF rbac_policies), Enforcement im HTTP-Dispatcher
- Tests: 90%+ Coverage für Sign/Verify und Policy Checks

## 📌 Messgrößen nach Umsetzung
- Audit-Log Einträge mit echter Signatur (FLAG: signature_verified=true)
- RBAC Denied Counter (metrics: rbac_denied_total)
- Backup-Differential Größe pro Intervall
- Log Parsing Latenz (structured_logs_parse_ms_bucket)

---

**Erstellt:** 29. Oktober 2025  
**Nächster Review:** Nach Abschluss von 3 P0-Features
