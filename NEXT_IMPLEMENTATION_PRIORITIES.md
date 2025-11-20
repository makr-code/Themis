# ThemisDB - Nächste Implementierungsprioritäten

**Datum:** 20. November 2025  
**Basierend auf:** ROADMAP.md v2.0  
**Status:** Planung für nächsten Development Branch

---

## Zusammenfassung

Aus der konsolidierten Roadmap ergeben sich folgende **P0 (kritische Priorität)** Implementierungsnotwendigkeiten für Q1 2026. Diese Liste priorisiert die Arbeit nach Roadmap, ohne Ingestion-bezogene Features.

---

## 🎯 Empfohlener Nächster Branch: Column-Level Encryption

### Begründung
- **Priorität:** P0 (Kritisch)
- **Status:** Design-Phase abgeschlossen
- **Aufwand:** 1-2 Wochen
- **Impact:** Sicherheit & Compliance (DSGVO, eIDAS)
- **Abhängigkeiten:** Keine - kann sofort gestartet werden
- **Dokumentation:** Bereits vorhanden (`docs/column_encryption.md`)

### Implementierungsumfang (Branch: `feature/column-level-encryption`)

**Core Features:**
1. **Transparent Encryption/Decryption**
   - Automatische Ver-/Entschlüsselung auf Spaltenebene
   - Integration in Storage Layer (RocksDB)
   - Schema-basierte Konfiguration (welche Spalten verschlüsselt werden)

2. **Key Rotation Support**
   - Lazy Re-Encryption (bereits implementiert in FieldEncryption)
   - Versionierung der Encryption Keys
   - Zero-downtime Key-Wechsel

3. **Pluggable Key Management**
   - Interface für externe Key Management Systeme
   - Vault/HSM Integration (HSMKeyProvider bereits vorhanden)
   - Fallback auf lokale Keys (LEKManager)

4. **Index Compatibility**
   - Encrypted Columns können indiziert werden
   - Deterministic Encryption für Equality Searches
   - Non-deterministic für Sensitive Data

**Deliverables:**
- ✅ `src/utils/column_encryption.h` + `.cpp`
- ✅ `tests/test_column_encryption_e2e.cpp` (E2E Tests)
- ✅ `tests/test_column_encryption_key_rotation.cpp` (Key Rotation Tests)
- ✅ `benchmarks/bench_column_encryption.cpp` (Performance Benchmarks)
- ✅ `docs/column_encryption_implementation.md` (Implementation Guide)
- ✅ `docs/column_encryption_migration.md` (Migration Guide)

**Akzeptanzkriterien:**
- [ ] Alle Tests bestehen (E2E, Key Rotation, Performance)
- [ ] <10% Performance Overhead bei verschlüsselten Queries
- [ ] Key Rotation ohne Downtime
- [ ] Dokumentation vollständig (Implementation + Migration)
- [ ] Code Review mit 0 CRITICAL/HIGH Security Issues

---

## 📋 Alternative P0 Prioritäten (Falls Column Encryption blockiert)

### Alternative 1: JavaScript/Python SDK Finalisierung

**Status:** Alpha → Beta  
**Aufwand:** 2-3 Wochen  
**Priorität:** P0

**Warum später:**
- Längere Entwicklungszeit (2-3 Wochen vs. 1-2 Wochen)
- Komplexere Testing-Requirements (NPM/PyPI Publishing)
- Mehrere Deliverables (JS + Python parallel)

**Branch-Vorschlag:** `feature/sdk-beta-release`

**Kernaufgaben:**
- **JavaScript SDK:**
  - TypeScript Definitions (.d.ts)
  - Transaction Support (BEGIN/COMMIT/ROLLBACK)
  - Error Handling (Custom Error Types)
  - Comprehensive Tests (Unit + Integration)
  - NPM Package Preparation

- **Python SDK:**
  - Type Hints (PEP 484)
  - Transaction Support
  - Async/Await Support (asyncio)
  - Comprehensive Tests (pytest)
  - PyPI Package Preparation

**Deliverables:**
- ✅ `clients/javascript/themisdb-client/` (NPM-ready)
- ✅ `clients/python/themisdb-client/` (PyPI-ready)
- ✅ `docs/sdk_quickstart_js.md`
- ✅ `docs/sdk_quickstart_python.md`
- ✅ `docs/sdk_api_reference.md`

---

## 🔧 P1 Prioritäten (Nach P0 Abschluss)

### 1. Content Processors Erweiterung
**Aufwand:** 2-3 Wochen  
**Branch:** `feature/content-processors-extended`

**Neue Prozessoren:**
- PDF Processor (Text extraction, metadata)
- Office Processor (DOCX, XLSX, PPTX)
- Video/Audio Metadata Extractor

**Integration:**
- Unified Ingestion Pipeline
- Batch Processing Support

### 2. CI/CD Verbesserungen
**Aufwand:** 1 Woche  
**Branch:** `feature/cicd-improvements`

**Implementierung:**
- GitHub Actions Matrix (Linux + Windows)
- Trivy Security Scanning (fail on HIGH/CRITICAL)
- Coverage Reporting (Codecov/Coveralls)
- Automated Release Process
- Container Multi-Arch Builds (amd64, arm64)

### 3. Window Functions (SQL Analytics)
**Aufwand:** 2-3 Wochen  
**Branch:** `feature/window-functions`

**Features:**
- OVER clause
- PARTITION BY
- ROW_NUMBER, RANK, DENSE_RANK
- LAG, LEAD
- Running Totals

**Implementierung:**
- AQL Syntax Extension
- Query Executor Updates
- Query Optimizer Integration

### 4. Docker Runtime Optimierung
**Aufwand:** 3-5 Tage  
**Branch:** `feature/docker-runtime-optimization`

**Verbesserungen:**
- Multi-stage Build (kleiner Image Size)
- Distroless/Slim Base Image
- Target: <100MB Image Size (aktuell ~300MB)
- Security Hardening
- Non-root User (bereits implementiert)

---

## 📊 Implementierungsreihenfolge (Q1 2026)

```
Woche 1-2:   Column-Level Encryption          [P0] ← NÄCHSTER BRANCH
              └─ feature/column-level-encryption

Woche 3-5:   JavaScript/Python SDK Beta       [P0]
              └─ feature/sdk-beta-release

Woche 6-8:   Content Processors Erweiterung   [P1]
              └─ feature/content-processors-extended

Woche 9:     CI/CD Verbesserungen             [P1]
              └─ feature/cicd-improvements

Woche 10-12: Window Functions                 [P1]
              └─ feature/window-functions

Woche 13:    Docker Runtime Optimierung       [P1]
              └─ feature/docker-runtime-optimization
```

---

## ✅ Erfolgskriterien für nächsten Branch

### Column-Level Encryption (Nächster Branch)

**Technisch:**
- [ ] 100% Test Coverage für neue Column Encryption Klassen
- [ ] Performance Overhead <10% bei verschlüsselten Queries
- [ ] Key Rotation funktioniert ohne Downtime
- [ ] Integration mit bestehenden Indexes (Graph, Vector, Full-Text)

**Qualität:**
- [ ] Code Review abgeschlossen (0 CRITICAL/HIGH Issues)
- [ ] CodeQL Security Scan bestanden (0 CRITICAL/HIGH Alerts)
- [ ] Dokumentation vollständig (Implementation + Migration Guide)
- [ ] Alle bestehenden Tests bestehen (keine Regression)

**Compliance:**
- [ ] DSGVO Artikel 32 konform (technische Maßnahmen)
- [ ] eIDAS-kompatibel (falls erforderlich)
- [ ] Audit-Log für Key Rotation Events

---

## 🚀 Quick Start für nächsten Development Branch

```bash
# 1. Neuen Branch erstellen
git checkout -b feature/column-level-encryption

# 2. Implementierung starten
# - src/utils/column_encryption.h
# - src/utils/column_encryption.cpp
# - Integration in Storage Layer

# 3. Tests schreiben
# - tests/test_column_encryption_e2e.cpp
# - tests/test_column_encryption_key_rotation.cpp

# 4. Performance Benchmarks
# - benchmarks/bench_column_encryption.cpp

# 5. Dokumentation
# - docs/column_encryption_implementation.md
# - docs/column_encryption_migration.md

# 6. Code Review & Security Scan
# - Pull Request erstellen
# - CodeQL Scan abwarten
# - Team Review
```

---

## 📎 Referenzen

- **Roadmap:** [ROADMAP.md](ROADMAP.md)
- **Entwicklungsstand:** [DEVELOPMENT_AUDITLOG.md](DEVELOPMENT_AUDITLOG.md)
- **Changelog:** [CHANGELOG.md](CHANGELOG.md)
- **Bestehende Encryption Docs:** [docs/column_encryption.md](docs/column_encryption.md)

---

## 💡 Empfehlung

**Start mit `feature/column-level-encryption`:**
1. Hohe Priorität (P0)
2. Überschaubare Implementierungszeit (1-2 Wochen)
3. Design bereits abgeschlossen
4. Keine externen Abhängigkeiten
5. Hoher Business Value (Security & Compliance)
6. Klare Akzeptanzkriterien
7. Bestehende Dokumentation als Basis

**Nächster Meilenstein nach Column Encryption:**
- SDK Beta Release (JavaScript + Python)
- Danach: P1 Features in Reihenfolge abarbeiten

---

**Letzte Aktualisierung:** 20. November 2025  
**Nächstes Review:** Nach Abschluss Column-Level Encryption
