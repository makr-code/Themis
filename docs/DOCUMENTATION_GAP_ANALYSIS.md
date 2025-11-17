# Documentation Gap Analysis
**Erstellt:** 17. November 2025  
**Zweck:** Systematischer Abgleich zwischen Implementierung und Dokumentation

---

## Executive Summary

Diese Analyse vergleicht den tatsächlichen Implementierungsstand (basierend auf Code-Audit) mit der vorhandenen Dokumentation. Sie identifiziert:

1. **Implementierte Features ohne Dokumentation** (Gap Type A)
2. **Dokumentierte Features ohne Implementierung** (Gap Type B)
3. **Inkonsistente Dokumentation** (Gap Type C)
4. **Veraltete Dokumentation** (Gap Type D)

### Kennzahlen

| Kategorie | Anzahl | Priorität |
|-----------|--------|-----------|
| **Type A (Impl. > Docs)** | 8 | Kritisch |
| **Type B (Docs > Impl.)** | 12 | Niedrig |
| **Type C (Inkonsistent)** | 6 | Hoch |
| **Type D (Veraltet)** | 4 | Mittel |
| **GESAMT** | **30** | - |

---

## Gap Type A: Implementiert, aber nicht dokumentiert

Diese Features sind im Code vorhanden, aber die Dokumentation fehlt oder ist unvollständig.

### A1: HNSW Persistence ⚠️ KRITISCH

**Status:** ✅ Vollständig implementiert  
**Code-Evidenz:**
- `src/index/vector_index.cpp`: saveIndex(), loadIndex() (Zeilen 560-650)
- `src/server/main_server.cpp`: Auto-save beim Shutdown
- `src/index/vector_index.cpp`: Auto-load beim init() (Zeile 111-140)

**Dokumentations-Lücken:**
- `docs/vector_ops.md`: Keine Erwähnung von save/load
- `docs/development/todo.md` Zeile 568: Falsch als `[ ]` markiert
- README.md: Keine Erwähnung der Persistenz

**Impact:** Benutzer wissen nicht, dass Vector-Index persistiert wird  
**Aufwand:** 2-3 Stunden (Dokumentation schreiben)

**Empfohlene Aktionen:**
1. `docs/vector_ops.md` erweitern mit Sektion "HNSW Persistence"
2. API-Referenz für `/vector/index/save` und `/vector/index/load` hinzufügen
3. Konfigurationsoptionen dokumentieren (`vector_index.save_path`, `vector_index.auto_save`)
4. todo.md korrigieren: Zeile 568 auf `[x]` setzen

---

### A2: Cosine Similarity Support ⚠️ KRITISCH

**Status:** ✅ Vollständig implementiert  
**Code-Evidenz:**
- `src/index/vector_index.cpp` Zeile 33-42: cosineOneMinus() Funktion
- `src/index/vector_index.cpp` Zeile 77: hnswlib::InnerProductSpace
- `src/index/vector_index.cpp` Zeile 124: Metric::COSINE Support
- `src/server/http_server.cpp` Zeile 2271: Metric-String-Mapping

**Dokumentations-Lücken:**
- `docs/vector_ops.md`: Erwähnt nur L2, nicht Cosine
- `docs/development/todo.md` Zeile 574: Falsch als `[ ]` markiert
- `docs/development/implementation_status.md` Zeile 222: Status unklar

**Impact:** Benutzer wissen nicht, dass Cosine-Distanz verfügbar ist  
**Aufwand:** 1-2 Stunden

**Empfohlene Aktionen:**
1. `docs/vector_ops.md` erweitern:
   - Metriken-Sektion (L2 vs. Cosine)
   - Normalisierung erklären
   - Use-Cases für jede Metrik
2. todo.md korrigieren
3. implementation_status.md aktualisieren

---

### A3: Backup & Restore HTTP Endpoints ⚠️ KRITISCH

**Status:** ✅ Vollständig implementiert  
**Code-Evidenz:**
- `src/storage/rocksdb_wrapper.cpp`: createCheckpoint(), restoreFromCheckpoint()
- `src/server/http_server.cpp`: handleBackup(), handleRestore()
- HTTP Endpoints: `POST /admin/backup`, `POST /admin/restore`

**Dokumentations-Lücken:**
- `docs/deployment.md`: Keine Erwähnung von Backup/Restore-Prozeduren
- `docs/operations_runbook.md`: Kein Backup/Restore-Runbook
- `docs/development/todo.md` Zeile 509: Falsch als `[ ]` markiert
- OpenAPI: Endpoints nicht dokumentiert

**Impact:** Admins wissen nicht, wie Backups durchgeführt werden  
**Aufwand:** 3-4 Stunden

**Empfohlene Aktionen:**
1. `docs/deployment.md` erweitern:
   - Backup-Strategie-Sektion
   - Schritt-für-Schritt-Anleitung
   - Restore-Prozedur
   - Best Practices (Backup-Häufigkeit, Retention)
2. `docs/operations_runbook.md` erweitern:
   - Disaster-Recovery-Plan
   - Backup-Verification
   - Restore-Testing
3. OpenAPI erweitern (docs/openapi.yaml)
4. todo.md korrigieren

---

### A4: Prometheus Metrics mit kumulativen Buckets ⚠️ HOCH

**Status:** ✅ Implementiert (29.10.2025)  
**Code-Evidenz:**
- `src/server/http_server.cpp`: recordLatency() mit kumulativen Buckets
- `tests/test_metrics_api.cpp`: 4/4 Tests PASSED inkl. Bucket-Validierung
- Buckets: 100us, 500us, 1ms, 5ms, 10ms, 50ms, 100ms, 500ms, 1s, 5s, +Inf

**Dokumentations-Lücken:**
- Keine vollständige Metrik-Referenz
- Bucket-Definitionen nicht dokumentiert
- PromQL-Beispiele fehlen

**Impact:** Monitoring-Setup ohne Dokumentation schwierig  
**Aufwand:** 2-3 Stunden

**Empfohlene Aktionen:**
1. Neue Datei `docs/observability/prometheus_metrics.md` erstellen
2. Alle Metriken auflisten (Counter, Gauges, Histograms)
3. Bucket-Definitionen dokumentieren
4. PromQL-Beispiele hinzufügen
5. Grafana-Dashboard-Beispiele verlinken

---

### A5: AQL COLLECT/GROUP BY MVP ⚠️ HOCH

**Status:** ✅ MVP implementiert  
**Code-Evidenz:**
- `src/query/aql_parser.cpp`: COLLECT Keyword-Parsing
- `src/server/http_server.cpp`: Hash-basierte Gruppierung
- Funktionen: COUNT, SUM, AVG, MIN, MAX
- Tests: 2/2 PASSED (`test_http_aql_collect.cpp`)

**Dokumentations-Lücken:**
- `docs/aql_syntax.md` Zeile 425-445: Nur kurze Beispiele
- Limitierungen nicht klar dokumentiert (nur 1 Gruppierungsfeld)
- Keine Performance-Charakteristik
- Keine Hinweise auf fehlende Features (HAVING, multi-column GROUP BY)

**Impact:** Benutzer wissen nicht, was funktioniert und was nicht  
**Aufwand:** 2 Stunden

**Empfohlene Aktionen:**
1. `docs/aql_syntax.md` erweitern:
   - COLLECT-Sektion mit vollständigen Beispielen
   - Limitierungen deutlich machen
   - Performance-Hinweise (In-Memory)
2. `docs/query_engine_aql.md`: Aggregations-Sektion hinzufügen
3. Roadmap für erweiterte Features (HAVING, multi-column)

---

### A6: Time-Series Engine (Gorilla, Retention, Aggregates) ⚠️ MITTEL

**Status:** ✅ Vollständig implementiert (08.11.2025)  
**Code-Evidenz:**
- `include/timeseries/tsstore.h`, `src/timeseries/tsstore.cpp`
- Gorilla-Compression: 10-20x Ratio
- Tests: test_tsstore.cpp, test_gorilla.cpp (alle PASS)

**Dokumentations-Lücken:**
- `docs/time_series.md`: Veraltet, referenziert alten API-Stand
- Gorilla-Compression nicht dokumentiert
- Continuous Aggregates nicht dokumentiert
- Retention Policies nicht dokumentiert

**Impact:** Feature ist nicht nutzbar ohne aktuelle Doku  
**Aufwand:** 4-5 Stunden

**Empfohlene Aktionen:**
1. `docs/time_series.md` komplett überarbeiten:
   - TSStore API-Referenz
   - Gorilla-Compression (Impact, Trade-offs)
   - Continuous Aggregates
   - Retention Policies
   - Performance-Charakteristik
2. Neue Datei `docs/apis/timeseries_api.md` erstellen
3. Beispiele und Use-Cases hinzufügen

---

### A7: MVCC Transaction Performance ⚠️ MITTEL

**Status:** ✅ Implementiert und getestet  
**Code-Evidenz:**
- `benchmarks/bench_mvcc.cpp`: Performance-Benchmarks
- Ergebnisse: MVCC ~3.4k/s ≈ WriteBatch ~3.1k/s

**Dokumentations-Lücken:**
- `docs/mvcc_design.md`: Enthält keine Benchmark-Daten
- Performance-Charakteristik nicht dokumentiert
- Trade-offs nicht klar

**Impact:** Benutzer wissen nicht, ob MVCC für ihren Use-Case geeignet ist  
**Aufwand:** 1-2 Stunden

**Empfohlene Aktionen:**
1. `docs/mvcc_design.md` erweitern:
   - Performance-Sektion mit Benchmark-Daten
   - Overhead-Analyse
   - When to use MVCC vs. WriteBatch
2. `docs/performance_benchmarks.md` aktualisieren

---

### A8: Cursor Pagination (HTTP-Ebene) ⚠️ NIEDRIG

**Status:** ✅ Implementiert  
**Code-Evidenz:**
- Base64-Token-Format
- Response: `{items, has_more, next_cursor, batch_size}`
- `docs/cursor_pagination.md` existiert

**Dokumentations-Lücken:**
- Dokumentation ist vorhanden, aber könnte besser sein
- Limitierungen nicht klar (nur HTTP-Ebene, nicht Engine)

**Impact:** Gering, Basis-Doku vorhanden  
**Aufwand:** 1 Stunde

**Empfohlene Aktionen:**
1. `docs/cursor_pagination.md` verbessern:
   - Limitierungen deutlicher machen
   - Best Practices hinzufügen

---

## Gap Type B: Dokumentiert, aber nicht implementiert

Diese Features sind in der Dokumentation oder todo.md erwähnt, aber nicht (oder nur teilweise) implementiert.

### B1: Apache Arrow Integration ❌

**Status:** ❌ Nicht implementiert  
**Dokumentations-Erwähnungen:**
- `docs/development/todo.md` Zeile 401: Priorität 4
- README.md: Arrow als Dependency gelistet

**Code-Status:**
- CMake findet Arrow (find_package)
- vcpkg manifest enthält `arrow`
- ❌ Keine Arrow-API-Nutzung im src/

**Impact:** Verwirrend für Benutzer, die Arrow-Features erwarten  
**Aufwand:** N/A (Feature nicht geplant für Core-Release)

**Empfohlene Aktionen:**
1. `docs/development/todo.md`: Klarstellen, dass Arrow post-release ist
2. README.md: Arrow als "geplant" markieren
3. Neue Datei `docs/roadmap/arrow_integration.md` für zukünftige Pläne

---

### B2: LET/Subqueries in AQL ❌

**Status:** ❌ Nicht implementiert  
**Dokumentations-Erwähnungen:**
- `docs/development/todo.md` Zeile 463, 495: Als TODO markiert
- `docs/aql_syntax.md`: Könnte LET erwähnen

**Code-Status:**
- ✅ AST-Node `LetNode` existiert (aql_parser.h Zeile 28)
- ❌ Keine Executor-Implementierung

**Impact:** Gering, korrekt als TODO markiert  
**Aufwand:** N/A

**Empfohlene Aktionen:**
1. Keine Änderungen nötig, Status ist korrekt

---

### B3: OR/NOT Optimierung in AQL ❌

**Status:** ❌ Nicht implementiert  
**Dokumentations-Erwähnungen:**
- `docs/development/todo.md` Zeile 465, 488, 597

**Code-Status:**
- Nur AND-Konjunktionen unterstützt

**Impact:** Gering, korrekt als TODO markiert  
**Aufwand:** N/A

**Empfohlene Aktionen:**
1. Keine Änderungen nötig, Status ist korrekt

---

### B4: Pfad-Constraints (PATH.ALL/NONE/ANY) ❌

**Status:** ❌ Nicht implementiert  
**Dokumentations-Erwähnungen:**
- `docs/path_constraints.md`: Design-Dokument existiert
- `docs/development/todo.md` Zeile 37

**Code-Status:**
- ✅ Design dokumentiert
- ❌ Keine Implementierung

**Impact:** Gering, Design-Doku korrekt  
**Aufwand:** N/A

**Empfohlene Aktionen:**
1. `docs/path_constraints.md`: Hinweis hinzufügen, dass es sich um Design handelt, nicht Implementierung

---

### B5: Filesystem/Content Pipeline ❌

**Status:** ❌ Nur Header, keine Implementierung  
**Dokumentations-Erwähnungen:**
- `docs/content_pipeline.md`, `docs/content_architecture.md`
- `docs/development/todo.md` Phase 4

**Code-Status:**
- ✅ Header: `include/content/content_manager.h`
- ❌ Keine .cpp-Implementierungen

**Impact:** Hoch, Dokumentation suggeriert Feature-Existenz  
**Aufwand:** N/A (Post-release)

**Empfohlene Aktionen:**
1. `docs/content_pipeline.md` aktualisieren:
   - Deutlicher Status-Hinweis (PLANNED, NOT IMPLEMENTED)
   - Roadmap hinzufügen
2. `docs/content_architecture.md`: Ähnlicher Hinweis

---

### B6: RBAC & Security Features ❌

**Status:** ❌ Nicht implementiert  
**Dokumentations-Erwähnungen:**
- `docs/rbac_authorization.md`
- Viele Security-Docs (siehe DOCUMENTATION_TODO.md)

**Code-Status:**
- ❌ Keine RBAC-Implementierung

**Impact:** Hoch, Security-Doku suggeriert Features  
**Aufwand:** N/A (Post-release, Phase 7)

**Empfohlene Aktionen:**
1. Alle Security-Docs mit Status-Hinweis versehen: "PLANNED - NOT YET IMPLEMENTED"
2. Roadmap-Sektion hinzufügen

---

### B7-B12: Weitere Post-Release Features

Diese Features sind korrekt als TODO/geplant dokumentiert:
- Joins (Multi-FOR + FILTER)
- Cluster/Replication
- Advanced Search (ArangoSearch-ähnlich)
- Serverless Scaling
- ML/AI Features
- Enterprise Compliance

**Empfohlene Aktionen:**
1. Alle entsprechenden Docs mit Roadmap-Status versehen
2. Keine Implementierung implizieren

---

## Gap Type C: Inkonsistente Dokumentation

Widersprüchliche Informationen in verschiedenen Dokumenten.

### C1: Vector Operations Status

**Problem:** todo.md vs. implementation_status.md vs. tatsächlicher Code

**todo.md sagt:**
- `[ ]` Cosine (Zeile 574)
- `[ ]` HNSW-Persistenz (Zeile 568)

**implementation_status.md sagt:**
- Zeile 222: "❌ Nicht separat implementiert" (Cosine)
- Zeile 236: "⚠️ Teilweise implementiert" (HNSW)

**Tatsächlicher Code:**
- ✅ Cosine: VOLLSTÄNDIG implementiert
- ✅ HNSW Persistenz: VOLLSTÄNDIG implementiert

**Empfohlene Aktionen:**
1. Beide Dokumente auf ✅ aktualisieren
2. Code-Evidenz hinzufügen

---

### C2: Backup/Restore Status

**Problem:** Verschiedene Dokumente widersprechen sich

**todo.md Zeile 509:** `[ ]` Backup/Restore Endpoints  
**todo.md Zeile 40:** `[x]` Ops & Recovery (mit Kommentar "Backup/Restore implementiert")  
**implementation_status.md Zeile 297:** ✅ IMPLEMENTIERT

**Empfohlene Aktionen:**
1. todo.md Zeile 509 auf `[x]` setzen
2. Konsistenz sicherstellen

---

### C3: AQL COLLECT Status

**Problem:** Verschiedene Statusangaben

**todo.md:** Verschiedene Zeilen mit unterschiedlichen Status  
**implementation_status.md Zeile 99-119:** ✅ MVP implementiert, aber Limitierungen

**Empfohlene Aktionen:**
1. Status in allen Dokumenten auf "✅ MVP, erweiterte Features offen" setzen
2. Klare Abgrenzung zwischen MVP und Full-Features

---

### C4-C6: Weitere Inkonsistenzen

Kleinere Widersprüche in:
- Graph Traversal Features
- Observability Status
- Time-Series Status

**Empfohlene Aktionen:**
1. Systematischer Review aller Status-Angaben
2. Code als Single Source of Truth verwenden
3. Dokumentation aktualisieren

---

## Gap Type D: Veraltete Dokumentation

Dokumentation, die nicht mehr den aktuellen Stand widerspiegelt.

### D1: time_series.md

**Problem:** Referenziert alten API-Stand vor TSStore-Implementation

**Veraltet:**
- API-Beschreibung
- Keine Gorilla-Compression
- Keine Continuous Aggregates

**Empfohlene Aktionen:**
1. Komplette Überarbeitung (siehe A6)

---

### D2: README.md

**Problem:** Fehlt kürzlich implementierte Features

**Fehlt:**
- MVCC/Transactions
- HNSW Persistenz
- Prometheus Metrics (kumulative Buckets)
- AQL COLLECT/GROUP BY
- Backup/Restore

**Empfohlene Aktionen:**
1. README.md aktualisieren mit neuesten Features
2. Link zu vollständiger Feature-Liste

---

### D3: architecture.md

**Problem:** Könnte neuere Implementierungen reflektieren

**Fehlt:**
- MVCC-Integration
- Vector Index Persistenz
- Transaction Flow

**Empfohlene Aktionen:**
1. Architecture-Diagramme aktualisieren
2. MVCC-Flow hinzufügen

---

### D4: OpenAPI Specification

**Problem:** Fehlende Endpoints

**Fehlt:**
- /admin/backup
- /admin/restore
- /vector/index/save
- /vector/index/load

**Empfohlene Aktionen:**
1. OpenAPI erweitern mit fehlenden Endpoints

---

## Priorisierung

### Sofort (Diese Woche)
1. A1: HNSW Persistence dokumentieren
2. A2: Cosine Similarity dokumentieren
3. A3: Backup/Restore dokumentieren
4. C1-C3: Inkonsistenzen beheben

### Kurzfristig (Nächste 2 Wochen)
5. A4: Prometheus Metrics Reference erstellen
6. A5: AQL COLLECT erweitern
7. A6: Time-Series Doku überarbeiten
8. D2: README.md aktualisieren

### Mittelfristig (Nächste 4 Wochen)
9. A7: MVCC Performance dokumentieren
10. B5: Content Pipeline Status klären
11. B6: Security Docs mit Status versehen
12. D3-D4: Architecture und OpenAPI aktualisieren

---

## Tracking

**Gaps identifiziert:** 30  
**Kritisch:** 3  
**Hoch:** 2  
**Mittel:** 3  
**Niedrig:** 22

**Geschätzter Gesamtaufwand:** 25-35 Stunden

---

**Erstellt:** 17. November 2025  
**Autor:** Documentation Audit Bot  
**Nächstes Update:** Wöchentlich
