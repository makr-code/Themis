# Themis - Priorisierte Roadmap für Production Readiness
**Stand:** 29. Oktober 2025, 22:15  
**Basis:** IMPLEMENTATION_STATUS.md Audit-Ergebnisse

---

## 🎯 Entscheidungsmatrix: Nächste Schritte

| Feature | Impact | Aufwand | Risiko | Prio | Empfehlung |
|---------|--------|---------|--------|------|------------|
| ~~**Prometheus Histogramme (kumulative Buckets)**~~ | Mittel | 2-4h | Niedrig | ✅ **ERLEDIGT** | **Quick Win** - Abgeschlossen |
| ~~**HNSW Persistenz**~~ | Hoch | 1-2 Tage | Mittel | ✅ **ERLEDIGT** | **Datenverlust-Risiko eliminiert** |
| ~~**COLLECT/GROUP BY MVP**~~ | Hoch | 3-5 Tage | Mittel | ✅ **ERLEDIGT** | **Basisfunktionalität implementiert** |
| ~~**Vector Search HTTP Endpoint**~~ | Hoch | 1-2 Tage | Niedrig | ✅ **ERLEDIGT** | **API-Integration vollständig** |
| ~~**OR Query Index-Merge**~~ | Mittel | 2-3 Tage | Mittel | ✅ **ERLEDIGT** | **DisjunctiveQuery implementiert** |
| ~~**OpenTelemetry Tracing**~~ | Mittel | 3-5 Tage | Niedrig | ✅ **ERLEDIGT** | **Production-Debugging enabled** |
| **Inkrementelle Backups** | Niedrig | 5-7 Tage | Hoch | 📋 P2 | Nice-to-Have |
| **RBAC (Basic)** | Hoch | 7-10 Tage | Hoch | 📋 P2 | Security (später) |
| **Apache Arrow Integration** | Niedrig | 10-15 Tage | Mittel | 📋 P3 | Analytics (später) |

**Status Update (30. Oktober 2025, 13:50):**
- ✅ **Alle P0-Features abgeschlossen!**
- ✅ **P1 OpenTelemetry Tracing: VOLLSTÄNDIG IMPLEMENTIERT**
  - ✅ Infrastruktur: Tracer-Wrapper, OTLP HTTP Exporter, CMake integration
  - ✅ HTTP-Handler instrumentiert (7 Endpoints)
  - ✅ QueryEngine instrumentiert (11 Methoden + Child-Spans)
  - ✅ AQL-Operator-Pipeline instrumentiert (parse, translate, for, filter, limit, collect, return, traversal+bfs)
  - ✅ Dokumentation aktualisiert (docs/tracing.md)
  - Build erfolgreich, Server-Test bestanden
  - **ALLE P1-TASKS ABGESCHLOSSEN!**

**Abgeschlossene Features:**
- ✅ HNSW-Persistenz: Automatisches Save/Load implementiert
- ✅ COLLECT/GROUP BY MVP: Parser + In-Memory Aggregation (COUNT, SUM, AVG, MIN, MAX)
- ✅ Prometheus-Histogramme: Kumulative Buckets implementiert + validiert
- ✅ Vector Search HTTP Endpoint: POST /vector/search mit k-NN Suche
- ✅ OR Query Index-Merge: DisjunctiveQuery mit Index-Union
- ✅ OpenTelemetry Distributed Tracing: End-to-End Instrumentierung (HTTP → QueryEngine → AQL Operators)

**Legende:**
- 🔥 P0 = Kritisch (sofort/diese Woche) - ✅ **ALLE ERLEDIGT**
- ⚠️ P1 = Wichtig (nächste 2 Wochen) - ✅ **ALLE ERLEDIGT**
- 📋 P2 = Nice-to-Have (nächster Sprint) - **NÄCHSTE PHASE**
- 📋 P3 = Backlog (zukünftig)

---

## 🚀 Empfohlene Reihenfolge (Batch 1: Diese Woche)

### Option A: Quick Wins zuerst (Momentum aufbauen) ✅ ABGESCHLOSSEN
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

## 🎯 Entscheidung erforderlich

**Bitte wählen:**
1. **Option A:** Quick Wins zuerst (Prometheus → OR/NOT → HNSW)
2. **Option B:** Strategisch (COLLECT → HNSW → Prometheus)
3. **Option C:** Risiko-Minimierung (HNSW → COLLECT → Prometheus)
4. **Empfehlung:** Hybrid (Prometheus [Quick Win] → HNSW [Risiko] → COLLECT [Strategisch])

**Oder eigene Priorisierung nennen.**

---

**Erstellt:** 29. Oktober 2025  
**Nächster Review:** Nach Abschluss von 3 P0-Features
