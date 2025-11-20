# ThemisDB - Nächste Implementierungsprioritäten

**Datum:** 20. November 2025  
**Basierend auf:** ROADMAP.md v2.0  
**Status:** Aktualisiert nach Code-Audit

> **⚠️ WICHTIG:** Column-Level Encryption ist bereits vollständig implementiert als "Field-Level Encryption" + "Schema-Based Encryption". Siehe Details unten.

---

## ⚡ Status Update: Column-Level Encryption

**Column-Level Encryption ist bereits implementiert! ✅**

Die in der Roadmap geplante "Column-Level Encryption" wurde als "Field-Level Encryption" implementiert, was in document databases funktional äquivalent ist.

**Implementierte Features:**
- ✅ `FieldEncryption` class (AES-256-GCM) - `include/security/encryption.h`
- ✅ Schema-basierte Verschlüsselung - HTTP API `/config/encryption-schema`
- ✅ Key Rotation Support - `decryptAndReEncrypt()`, `needsReEncryption()`
- ✅ Encryption Metrics - 42 atomic counters für Prometheus
- ✅ Comprehensive Tests - `tests/test_schema_encryption.cpp` (809 Zeilen, 19 Test-Cases)
- ✅ Dokumentation - `docs/column_encryption.md` (25K Zeilen Design-Doc)

**Code-Locations:**
- Core: `src/security/field_encryption.cpp`, `include/security/encryption.h`
- Schema Integration: `src/server/http_server.cpp` (lines 8862-8967)
- Tests: `tests/test_schema_encryption.cpp`, `tests/test_lazy_reencryption.cpp`

---

## Zusammenfassung

Aus der konsolidierten Roadmap ergeben sich folgende **P0 (kritische Priorität)** Implementierungsnotwendigkeiten für Q1 2026. Diese Liste priorisiert die Arbeit nach Roadmap, ohne Ingestion-bezogene Features.

**Da Column-Level Encryption bereits implementiert ist, ist die neue Empfehlung:**

---

## 🎯 Empfohlener Nächster Branch: JavaScript/Python SDK Finalisierung

### Begründung
- **Priorität:** P0 (Kritisch)
- **Status:** Alpha → Beta
- **Aufwand:** 2-3 Wochen
- **Impact:** Developer Experience, Ecosystem Growth
- **Abhängigkeiten:** Keine
- **Dokumentation:** Teilweise vorhanden (Alpha-Versionen in `clients/`)

### Implementierungsumfang (Branch: `feature/sdk-beta-release`)

**JavaScript SDK:**
- TypeScript Definitions (.d.ts)
- Transaction Support (BEGIN/COMMIT/ROLLBACK)
- Error Handling (Custom Error Types)
- Comprehensive Tests (Unit + Integration)
- NPM Package Preparation

**Python SDK:**
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

**Akzeptanzkriterien:**
- [ ] Alle Tests bestehen (Unit + Integration)
- [ ] TypeScript definitions vollständig
- [ ] Transaction support funktional
- [ ] Dokumentation vollständig
- [ ] NPM + PyPI Package veröffentlicht (Beta)

---

## 📋 Archiviert: Column-Level Encryption (Bereits Implementiert)

**Status:** ✅ **Vollständig implementiert als Field-Level Encryption + Schema-Based Encryption**

Die ursprünglich geplante "Column-Level Encryption" wurde bereits implementiert. In document databases sind Field-Level und Column-Level Encryption funktional äquivalent.

**Implementierte Features (alle ✅):**
- Transparent Encryption/Decryption (AES-256-GCM)
- Schema-basierte Konfiguration via `/config/encryption-schema` API
- Key Rotation Support (Lazy Re-Encryption)
- Pluggable Key Management (MockKeyProvider, HSMKeyProvider, VaultKeyProvider)
- Encryption Metrics (42 atomic counters)
- Comprehensive Tests (809 Zeilen in test_schema_encryption.cpp)

**Siehe:**
- Code: `include/security/encryption.h`, `src/security/field_encryption.cpp`
- Tests: `tests/test_schema_encryption.cpp`, `tests/test_lazy_reencryption.cpp`
- Docs: `docs/column_encryption.md`

---

## 📋 P1 Prioritäten (Nach SDK Finalisierung)

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

## 📊 Implementierungsreihenfolge (Q1 2026) - AKTUALISIERT

```
Woche 1-3:   JavaScript/Python SDK Beta       [P0] ← NÄCHSTER BRANCH
              └─ feature/sdk-beta-release

Woche 4-6:   Content Processors Erweiterung   [P1]
              └─ feature/content-processors-extended

Woche 7:     CI/CD Verbesserungen             [P1]
              └─ feature/cicd-improvements

Woche 8-10:  Window Functions                 [P1]
              └─ feature/window-functions

Woche 11:    Docker Runtime Optimierung       [P1]
              └─ feature/docker-runtime-optimization

Woche 12-13: Puffer für Bugfixes & Documentation
```

**Hinweis:** Column-Level Encryption (ursprünglich Woche 1-2) ist bereits implementiert und wurde aus dem Plan entfernt.

---

## ✅ Erfolgskriterien für nächsten Branch

### JavaScript/Python SDK Finalisierung (Nächster Branch)

**JavaScript SDK:**
- [ ] TypeScript definitions vollständig (.d.ts files)
- [ ] Transaction support (BEGIN/COMMIT/ROLLBACK)
- [ ] Error handling (Custom error types)
- [ ] Unit tests (>80% coverage)
- [ ] Integration tests (E2E scenarios)
- [ ] NPM package published (Beta)

**Python SDK:**
- [ ] Type hints vollständig (PEP 484)
- [ ] Transaction support
- [ ] Async/await support (asyncio)
- [ ] Unit tests (pytest, >80% coverage)
- [ ] Integration tests (E2E scenarios)
- [ ] PyPI package published (Beta)

**Dokumentation:**
- [ ] Quick Start Guide (JS + Python)
- [ ] API Reference (vollständig)
- [ ] Code Examples (mindestens 10 pro SDK)
- [ ] Migration Guide (Alpha → Beta)

**Qualität:**
- [ ] Code Review abgeschlossen (0 CRITICAL/HIGH Issues)
- [ ] Alle Tests bestehen (Unit + Integration)
- [ ] Dokumentation vollständig
- [ ] NPM + PyPI Packages veröffentlicht

---

## 🚀 Quick Start für nächsten Development Branch

```bash
# 1. Neuen Branch erstellen
git checkout -b feature/sdk-beta-release

# 2. JavaScript SDK finalisieren
cd clients/javascript/themisdb-client
# - TypeScript definitions (.d.ts)
# - Transaction support
# - Error handling
# - Tests (jest/mocha)
npm test
npm run build

# 3. Python SDK finalisieren
cd ../../python/themisdb-client
# - Type hints
# - Async support
# - Transaction support
# - Tests (pytest)
pytest
python setup.py sdist bdist_wheel

# 4. Dokumentation
# - docs/sdk_quickstart_js.md
# - docs/sdk_quickstart_python.md
# - docs/sdk_api_reference.md

# 5. Publishing (Beta)
# JavaScript: npm publish --tag beta
# Python: twine upload --repository pypi dist/*

# 6. Code Review & Testing
# - Pull Request erstellen
# - Team Review
```

---

## 📎 Referenzen

- **Roadmap:** [ROADMAP.md](ROADMAP.md)
- **Entwicklungsstand:** [DEVELOPMENT_AUDITLOG.md](DEVELOPMENT_AUDITLOG.md)
- **Changelog:** [CHANGELOG.md](CHANGELOG.md)
- **Column Encryption (Implementiert):** [docs/column_encryption.md](docs/column_encryption.md)
- **Encryption Code:** `include/security/encryption.h`, `src/security/field_encryption.cpp`
- **SDK Alpha:** `clients/javascript/`, `clients/python/`

---

## 💡 Empfehlung

**Start mit `feature/sdk-beta-release`:**
1. Hohe Priorität (P0)
2. Developer Experience & Ecosystem Growth
3. Überschaubare Implementierungszeit (2-3 Wochen)
4. Alpha-Versionen bereits vorhanden
5. Keine externen Abhängigkeiten
6. Klare Akzeptanzkriterien
7. Hoher Business Value (Adoption fördern)

**Warum SDK vor anderen P1 Features:**
- Column-Level Encryption bereits implementiert (nicht mehr notwendig)
- SDK Beta ermöglicht frühe Adoption durch Entwickler
- Community Feedback für weitere Features

**Nächster Meilenstein nach SDK Beta:**
- Content Processors Erweiterung (P1)
- Danach: CI/CD, Window Functions, Docker Optimization

---

**Letzte Aktualisierung:** 20. November 2025  
**Nächstes Review:** Nach Abschluss Column-Level Encryption
