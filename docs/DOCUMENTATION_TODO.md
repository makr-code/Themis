# Documentation Consolidation and Update TODO
**Erstellt:** 17. November 2025  
**Zweck:** Zentrales Dokument zur Dokumentations-Konsolidierung und Aktualisierung basierend auf dem Abgleich zwischen Dokumentation und tatsächlicher Implementierung

---

## Übersicht

Dieses Dokument verfolgt die schrittweise Konsolidierung und Aktualisierung der ThemisDB-Dokumentation. Es identifiziert:
- Diskrepanzen zwischen Dokumentation und Implementierung
- Duplikate und überlappende Inhalte
- Fehlende oder veraltete Dokumentation
- Priorisierte Aufgaben für die Dokumentations-Updates

---

## 1. Kritische Diskrepanzen (Sofort beheben)

Diese Lücken betreffen bereits implementierte Features, die in der Dokumentation als "TODO" markiert oder nicht dokumentiert sind.

### 1.1 Vector Operations ✅ IMPLEMENTIERT, aber teilweise falsch dokumentiert

**Problem:** `docs/development/todo.md` und `docs/development/implementation_status.md` zeigen widersprüchliche Informationen

**Tatsächlicher Status (Code-Audit):**
- ✅ Cosine-Distanz: IMPLEMENTIERT (`src/index/vector_index.cpp` Zeilen 33-42, 77, 124, 163, 198)
- ✅ HNSW-Persistenz: IMPLEMENTIERT (save/load via hnswlib, automatisch bei Server start/shutdown)
- ✅ Vector Search HTTP Endpoint: IMPLEMENTIERT (`POST /vector/search`)
- ❌ Batch-Operationen: NICHT implementiert
- ❌ Konfigurierbare HNSW-Parameter (M, efConstruction): NICHT implementiert (hardcoded)

**Zu aktualisieren:**
- [x] `docs/development/todo.md` Zeile 574: `[ ] Cosine` → `[x] Cosine (inkl. Normalisierung)` ✅ ERLEDIGT (17.11.2025)
- [x] `docs/development/todo.md` Zeile 568: `[ ] HNSW-Persistenz` → `[x] HNSW-Persistenz (save/load, auto-save)` ✅ ERLEDIGT (17.11.2025)
- [x] `docs/development/implementation_status.md` Zeile 222-228: Status aktualisieren ✅ ERLEDIGT (17.11.2025)
- [x] `docs/vector_ops.md`: Sektion über Cosine-Similarity und Persistenz hinzufügen ✅ BEREITS VORHANDEN

### 1.2 Backup & Recovery ✅ IMPLEMENTIERT, aber nicht dokumentiert

**Problem:** Endpoints sind implementiert, aber in todo.md als offen markiert

**Tatsächlicher Status:**
- ✅ RocksDB Checkpoint-API: IMPLEMENTIERT
- ✅ HTTP Endpoints: `POST /admin/backup`, `POST /admin/restore`
- ✅ Code: `src/storage/rocksdb_wrapper.cpp`, `src/server/http_server.cpp`

**Zu aktualisieren:**
- [x] `docs/development/todo.md` Zeile 509: `[ ]` → `[x]` Backup/Restore Endpoints ✅ BEREITS KORREKT MARKIERT
- [x] `docs/deployment.md`: Sektion über Backup/Restore-Prozeduren hinzufügen ✅ BEREITS VORHANDEN (Zeile 773+)
- [x] `docs/operations_runbook.md`: Backup/Restore-Runbook erstellen ✅ BEREITS VORHANDEN (Zeile 112+)

### 1.3 Prometheus Metrics ✅ IMPLEMENTIERT mit kumulativen Buckets

**Problem:** Histogramme sind jetzt Prometheus-konform, aber Dokumentation fehlt

**Tatsächlicher Status:**
- ✅ Kumulative Buckets: IMPLEMENTIERT (29.10.2025)
- ✅ Tests validiert: 4/4 PASS (`test_metrics_api.cpp`)
- ✅ Latency-Buckets: 100us, 500us, 1ms, 5ms, 10ms, 50ms, 100ms, 500ms, 1s, 5s, +Inf

**Zu aktualisieren:**
- [x] `docs/development/todo.md`: Status auf `[x]` setzen für kumulative Buckets ✅ BEREITS IMPLEMENTIERT (29.10.2025)
- [x] `docs/operations_runbook.md`: Prometheus-Metriken-Sektion erweitern ✅ BEREITS VORHANDEN
- [x] Neue Datei `docs/observability/prometheus_metrics.md` erstellen mit vollständiger Metrik-Referenz ✅ ERSTELLT (17.11.2025)

### 1.4 AQL COLLECT/GROUP BY ✅ MVP IMPLEMENTIERT

**Problem:** Implementation ist vorhanden, aber Dokumentation unvollständig

**Tatsächlicher Status:**
- ✅ Parser: COLLECT + AGGREGATE Keywords implementiert
- ✅ Executor: Hash-Map Gruppierung in http_server.cpp
- ✅ Aggregationsfunktionen: COUNT, SUM, AVG, MIN, MAX
- ⚠️ Limitierungen: Nur 1 Gruppierungsfeld, keine Cursor-Paginierung

**Zu aktualisieren:**
- [ ] `docs/aql_syntax.md`: COLLECT/GROUP BY Beispiele erweitern (bereits vorhanden, könnte verbessert werden)
- [x] `docs/development/todo.md`: Status präzisieren (MVP abgeschlossen, Erweiterungen offen) ✅ KORREKT MARKIERT
- [ ] `docs/query_engine_aql.md`: Aggregations-Sektion hinzufügen

### 1.5 Time-Series Engine ✅ VOLLSTÄNDIG IMPLEMENTIERT

**Problem:** time_series.md ist veraltet und referenziert alten API-Stand

**Tatsächlicher Status (08.11.2025):**
- ✅ Gorilla-Compression: IMPLEMENTIERT (10-20x Ratio)
- ✅ Continuous Aggregates: IMPLEMENTIERT
- ✅ Retention Policies: IMPLEMENTIERT
- ✅ TSStore API: IMPLEMENTIERT
- ✅ Tests: test_tsstore.cpp, test_gorilla.cpp (alle PASS)

**Zu aktualisieren:**
- [ ] `docs/time_series.md`: Komplette Überarbeitung mit TSStore API, Aggregationen, Limitierungen
- [ ] Neue Datei `docs/apis/timeseries_api.md` erstellen
- [ ] `docs/development/todo.md`: Status auf `[x]` setzen

---

## 2. Duplikate und Überlappungen (Konsolidieren)

### 2.1 Change Data Capture: cdc.md vs. change_data_capture.md

**Problem:** Zwei Dateien mit überlappenden Inhalten

**Aktuelle Lage:**
- `docs/cdc.md`: Vorhanden in mkdocs.yml, Pfad unbekannt
- `docs/change_data_capture.md`: Vorhanden in Verzeichnis, CDC-Konzepte

**Lösung:**
- [ ] Inhalte vergleichen und zusammenführen
- [ ] `docs/change_data_capture.md` als primäre Datei behalten
- [ ] `docs/cdc.md` zu Alias/Redirect umwandeln oder löschen
- [ ] mkdocs.yml aktualisieren (Zeile 106)

### 2.2 Compliance/Governance: Mehrere überlappende Dateien

**Problem:** 5+ Dateien mit Compliance-Themen, teilweise redundant

**Dateien:**
- `docs/compliance.md`
- `docs/compliance_audit.md`
- `docs/compliance_governance_strategy.md`
- `docs/compliance_integration.md`
- `docs/governance_usage.md`
- `docs/EXTENDED_COMPLIANCE_FEATURES.md`

**Lösung:**
- [ ] Inhalte aller Dateien auflisten
- [ ] Gemeinsame Abschnitte identifizieren
- [ ] Hierarchie erstellen:
  - `docs/compliance.md` → Überblick
  - `docs/compliance/audit.md` → Audit-Details
  - `docs/compliance/governance.md` → Governance-Strategie
  - `docs/compliance/integration.md` → Integration-Guide
- [ ] Duplikate entfernen
- [ ] mkdocs.yml Struktur anpassen

### 2.3 Encryption: Mehrere strategische Dokumente

**Problem:** 3 Dateien mit Encryption-Strategie, Abgrenzung unklar

**Dateien:**
- `docs/encryption_strategy.md`
- `docs/encryption_deployment.md`
- `docs/column_encryption.md`

**Ist-Analyse:**
- encryption_strategy.md: Gesamtstrategie, Key-Management, Compliance
- encryption_deployment.md: Deployment-Aspekte, Konfiguration
- column_encryption.md: Feature-spezifisch (Field-Level Encryption)

**Lösung:**
- [ ] Klare Abgrenzung in jedem Dokument dokumentieren
- [ ] Cross-References zwischen den Dokumenten ergänzen
- [ ] Eventuell encryption/ Unterordner erstellen:
  - `docs/encryption/overview.md` (Strategy)
  - `docs/encryption/deployment.md`
  - `docs/encryption/column_level.md`

### 2.4 Security-Ordner: Leer vs. viele Security-Docs im Root

**Problem:** `docs/security/` existiert, ist aber leer. Viele Security-Docs sind in `docs/` root.

**Dateien im Root:**
- `docs/security_hardening_guide.md`
- `docs/security_audit_checklist.md`
- `docs/security_audit_report.md`
- `docs/security_encryption_gap_analysis.md`
- `docs/rbac_authorization.md`
- `docs/pii_detection_engines.md`
- `docs/pii_engine_signing.md`

**Lösung:**
- [ ] Alle Security-relevanten Docs nach `docs/security/` verschieben
- [ ] Unterstruktur erstellen:
  - `docs/security/overview.md`
  - `docs/security/hardening.md`
  - `docs/security/audit_checklist.md`
  - `docs/security/audit_report.md`
  - `docs/security/rbac.md`
  - `docs/security/pii_detection.md`
  - `docs/security/encryption_gap_analysis.md`
- [ ] mkdocs.yml entsprechend anpassen
- [ ] Redirects/Hinweise in alten Dateien platzieren

---

## 3. Fehlende Dokumentation (Neu erstellen)

### 3.1 HNSW Persistence Feature Guide

**Grund:** Feature ist implementiert, aber nicht dokumentiert

**Zu erstellen:**
- [ ] `docs/vector_ops.md`: Sektion "HNSW Persistence" hinzufügen
  - Auto-save beim Server-Shutdown
  - Auto-load beim Server-Start
  - Manuelle save/load via API
  - Konfiguration (save_path, auto_save)
  - Format (index.bin, labels.txt, meta.txt)

### 3.2 Cursor Pagination Guide

**Grund:** Implementierung ist vorhanden, Dokumentation unvollständig

**Zu erstellen:**
- [ ] `docs/cursor_pagination.md` erweitern:
  - HTTP-Ebene Cursor-Format (Base64 Token)
  - Response-Format (next_cursor, has_more)
  - Limitierungen (nur HTTP-Ebene, nicht Engine-integriert)
  - Best Practices

### 3.3 Prometheus Metrics Reference

**Grund:** Viele Metriken existieren, keine vollständige Referenz

**Zu erstellen:**
- [ ] `docs/observability/prometheus_metrics.md`:
  - Vollständige Metrik-Liste
  - Counter: requests_total, errors_total, etc.
  - Gauges: qps, uptime, rocksdb_*
  - Histograms: latency_bucket_*, page_fetch_time_ms_bucket_*
  - Bucket-Definitionen
  - Beispiel-Queries (PromQL)

### 3.4 MVCC Implementation Guide

**Grund:** MVCC ist vollständig implementiert, aber Doku könnte besser sein

**Zu erstellen/erweitern:**
- [ ] `docs/mvcc_design.md` erweitern:
  - Performance-Charakteristiken (Benchmarks)
  - Transaction-Isolation-Levels
  - Conflict-Handling
  - Best Practices
  - Migration-Guide (von non-transactional zu transactional)

### 3.5 Content Pipeline Architecture

**Grund:** Header existieren, aber keine Implementierungs-Doku

**Zu erstellen:**
- [ ] `docs/content_pipeline.md` neu schreiben:
  - Aktueller Status (Header-only)
  - Geplante Architektur
  - Roadmap für Implementierung
  - Hinweis auf fehlende Implementierung

---

## 4. Veraltete Dokumentation (Aktualisieren)

### 4.1 README.md

**Problem:** Enthält veraltete Informationen, fehlt kürzlich implementierte Features

**Zu aktualisieren:**
- [ ] MVCC/Transactions erwähnen
- [ ] HNSW Persistenz erwähnen
- [ ] Prometheus Metrics erwähnen
- [ ] AQL COLLECT/GROUP BY erwähnen
- [ ] Backup/Restore Endpoints dokumentieren

### 4.2 architecture.md

**Problem:** Könnte neuere Implementierungen reflektieren

**Zu prüfen und aktualisieren:**
- [ ] MVCC-Integration in Architecture-Diagramm
- [ ] Vector Index Persistenz
- [ ] Observability Stack
- [ ] Transaction-Flow-Diagramm

### 4.3 development/todo.md

**Problem:** Viele `[ ]` Items sind eigentlich `[x]` (siehe oben)

**Zu aktualisieren:**
- [ ] Alle falsch markierten Items korrigieren (Liste aus Sektion 1)
- [ ] Neue Sprint-Pläne hinzufügen
- [ ] Veraltete Aufgaben archivieren

### 4.4 development/implementation_status.md

**Problem:** Audit ist vom 29.10.2025, könnte Updates brauchen

**Zu aktualisieren:**
- [ ] Status-Tabelle aktualisieren (Phase 1-5)
- [ ] Neue Implementierungen eintragen (seit 29.10.)
- [ ] Diskrepanzen-Sektion aktualisieren

---

## 5. mkdocs.yml Validierung

### 5.1 Pfad-Validierung

**Aufgabe:** Sicherstellen, dass alle in mkdocs.yml referenzierten Dateien existieren

**Zu prüfen:**
- [ ] Alle Pfade in `nav:` durchgehen
- [ ] Nicht-existente Dateien identifizieren
- [ ] Datei erstellen oder aus nav entfernen

### 5.2 Fehlende Dateien in nav

**Aufgabe:** Neue Dokumentation in mkdocs.yml einbinden

**Zu ergänzen:**
- [ ] DOCUMENTATION_TODO.md (dieses Dokument)
- [ ] Neue docs/observability/ Dateien
- [ ] Neu-organisierte docs/security/ Struktur
- [ ] Neu-organisierte docs/compliance/ Struktur

---

## 6. Cross-Referenzen und Links

### 6.1 Interne Links prüfen

**Aufgabe:** Alle internen Markdown-Links validieren

**Zu tun:**
- [ ] Script erstellen zum Finden gebrochener Links
- [ ] Gebrochene Links reparieren
- [ ] Relative Pfade verwenden (nicht absolute)

### 6.2 Code-Referenzen aktualisieren

**Aufgabe:** Sicherstellen, dass Code-Beispiele aktuell sind

**Zu tun:**
- [ ] Code-Beispiele in Docs mit tatsächlichem Code abgleichen
- [ ] API-Signaturen prüfen
- [ ] HTTP-Endpoint-Beispiele validieren

---

## 7. Prioritäten

### Priorität 1: Kritische Diskrepanzen (Diese Woche)
- HNSW Persistenz dokumentieren
- Backup/Restore dokumentieren
- todo.md korrigieren (Cosine, HNSW, Backup)
- implementation_status.md aktualisieren

### Priorität 2: Konsolidierung (Nächste 2 Wochen)
- CDC Dateien zusammenführen
- Security-Docs reorganisieren
- Compliance-Docs konsolidieren
- Encryption-Docs strukturieren

### Priorität 3: Neue Dokumentation (Nächste 4 Wochen)
- Prometheus Metrics Reference
- MVCC Implementation Guide
- Content Pipeline Status-Update
- Cursor Pagination erweitern

### Priorität 4: Validierung (Laufend)
- mkdocs.yml Pfade prüfen
- Links validieren
- Code-Beispiele aktualisieren
- README.md pflegen

---

## 8. Tracking

### Erledigte Aufgaben
- [x] Dokumentations-Audit durchgeführt (17.11.2025)
- [x] DOCUMENTATION_TODO.md erstellt
- [x] Diskrepanzen identifiziert
- [x] Duplikate identifiziert

### In Bearbeitung
- [ ] (Keine)

### Blockiert
- [ ] (Keine)

---

## 9. Hinweise für Bearbeiter

### Allgemeine Richtlinien
1. **Vor Änderungen:** Immer prüfen, ob die Dokumentation die tatsächliche Implementierung widerspiegelt
2. **Nach Änderungen:** Tests durchführen (mkdocs build, Link-Validierung)
3. **Commit-Messages:** Klar beschreiben, welche Dokumentation aktualisiert wurde
4. **Reviews:** Dokumentations-Änderungen immer von einem zweiten Entwickler prüfen lassen

### Best Practices
- Verwende klare Überschriften und Struktur
- Füge Code-Beispiele hinzu
- Verlinke verwandte Dokumente
- Markiere veraltete Inhalte deutlich
- Verwende konsistente Terminologie (siehe docs/glossary.md)

---

**Letzte Aktualisierung:** 17. November 2025  
**Nächste Review:** Wöchentlich, immer freitags  
**Verantwortlich:** Development Team
