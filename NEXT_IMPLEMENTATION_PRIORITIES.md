# ThemisDB - Nächste Implementierungsprioritäten

**Datum:** 20. November 2025  
**Basierend auf:** ROADMAP.md v2.0  
**Status:** Aktualisiert nach Code-Audit (v2)

> **⚠️ WICHTIGE UPDATES:**
> - Column-Level Encryption ist bereits vollständig implementiert als "Field-Level Encryption" + "Schema-Based Encryption"
> - Window Functions sind bereits vollständig implementiert (885 Zeilen Code, 579 Zeilen Tests)
> - CI/CD Workflows existieren noch nicht (nur Badges im README)

---

## ⚡ Status Update: Bereits Implementierte Features

### Column-Level Encryption ✅ KOMPLETT

**Column-Level Encryption ist bereits implementiert!**

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

### Window Functions ✅ KOMPLETT

**Window Functions sind bereits vollständig implementiert!**

Trotz Status "0% - geplant" in DEVELOPMENT_AUDITLOG.md existiert eine vollständige Implementierung.

**Implementierte Features:**
- ✅ `WindowEvaluator` class - `include/query/window_evaluator.h` (342 Zeilen)
- ✅ Core Implementation - `src/query/window_evaluator.cpp` (543 Zeilen)
- ✅ Window Functions: ROW_NUMBER, RANK, DENSE_RANK, LAG, LEAD, FIRST_VALUE, LAST_VALUE
- ✅ PARTITION BY support
- ✅ ORDER BY support (ASC/DESC)
- ✅ Window Frames (ROWS, RANGE)
- ✅ Comprehensive Tests - `tests/test_window_functions.cpp` (579 Zeilen)

**Code-Locations:**
- Core: `src/query/window_evaluator.cpp`, `include/query/window_evaluator.h`
- Tests: `tests/test_window_functions.cpp`

---

## Zusammenfassung

Aus der konsolidierten Roadmap ergeben sich folgende **P0 (kritische Priorität)** Implementierungsnotwendigkeiten für Q1 2026. Diese Liste priorisiert die Arbeit nach Roadmap, ohne Ingestion-bezogene Features.

**Da Column-Level Encryption bereits implementiert ist, ist die neue Empfehlung:**

---

## 🎯 Empfohlener Nächster Branch: CI/CD Verbesserungen

### Begründung
- **Priorität:** P1 (Hoch)
- **Status:** Nicht implementiert (nur README Badges vorhanden)
- **Aufwand:** 1 Woche
- **Impact:** Code Quality, Automated Testing, Security Scanning
- **Abhängigkeiten:** Keine
- **Dringlichkeit:** HIGH - Workflows im README referenziert aber fehlen

### Implementierungsumfang (Branch: `feature/cicd-improvements`)

**GitHub Actions Workflows:**
- CI Workflow (.github/workflows/ci.yml)
  - Linux + Windows Matrix Builds
  - Automated Testing (alle 303 Tests)
  - Build Verification
  
- Code Quality Workflow (.github/workflows/code-quality.yml)
  - Trivy Security Scanning (fail on HIGH/CRITICAL)
  - CodeQL Analysis
  - Linting (clang-tidy)
  
- Coverage Workflow
  - Coverage Reporting (Codecov/Coveralls)
  - Coverage Badge Generation
  
- Release Workflow
  - Automated Release Process
  - Container Multi-Arch Builds (amd64, arm64)
  - GitHub Release Creation

**Deliverables:**
- [ ] `.github/workflows/ci.yml` (CI Build & Test)
- [ ] `.github/workflows/code-quality.yml` (Security & Linting)
- [ ] `.github/workflows/coverage.yml` (Coverage Reporting)
- [ ] `.github/workflows/release.yml` (Automated Releases)
- [ ] `docs/ci_cd_guide.md` (CI/CD Documentation)

**Akzeptanzkriterien:**
- [ ] Alle Workflows funktionieren
- [ ] Tests laufen auf Linux + Windows
- [ ] Security Scanning aktiv
- [ ] Coverage Reports generiert
- [ ] Badge-Links im README funktionieren

---

## 📋 Archiviert: Bereits Implementierte Features

### Column-Level Encryption ✅ KOMPLETT

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

### Window Functions ✅ KOMPLETT

**Status:** ✅ **Vollständig implementiert**

**Implementierte Features (alle ✅):**
- OVER clause
- PARTITION BY
- ROW_NUMBER, RANK, DENSE_RANK
- LAG, LEAD
- FIRST_VALUE, LAST_VALUE
- Window Frames (ROWS, RANGE)
- Comprehensive Tests (579 Zeilen in test_window_functions.cpp)

**Siehe:**
- Code: `include/query/window_evaluator.h`, `src/query/window_evaluator.cpp`
- Tests: `tests/test_window_functions.cpp`

---

## 📋 Alternative P0 Prioritäten

### JavaScript/Python SDK Finalisierung

**Status:** Alpha → Beta  
**Aufwand:** 2-3 Wochen  
**Priorität:** P0
**Branch:** `feature/sdk-beta-release`

**Warum nicht zuerst:**
- CI/CD ist kritischer (automatisierte Tests, security scanning)
- SDK kann parallel entwickelt werden nach CI/CD Setup
- CI/CD ermöglicht bessere SDK-Qualitätssicherung

**Kernaufgaben:**
- **JavaScript SDK:** TypeScript Definitions, Transaction Support, Error Handling, Tests, NPM Package
- **Python SDK:** Type Hints, Transaction Support, Async/Await, Tests, PyPI Package

**Deliverables:**
- `clients/javascript/themisdb-client/` (NPM-ready)
- `clients/python/themisdb-client/` (PyPI-ready)
- `docs/sdk_quickstart_js.md`, `docs/sdk_quickstart_python.md`
- `docs/sdk_api_reference.md`

---

## 📋 Post-v1.0.0 Enterprise Features

Diese Features werden **nach** v1.0.0 Release implementiert:

### 1. Docker Runtime Optimierung
**Typ:** Enterprise Feature  
**Aufwand:** 3-5 Tage  
**Branch:** `feature/docker-runtime-optimization` (Post-v1.0.0)

**Verbesserungen:**
- Multi-stage Build (kleiner Image Size)
- Distroless/Slim Base Image
- Target: <100MB Image Size (aktuell ~300MB)
- Security Hardening
- Non-root User (bereits implementiert)

### 2. Content Processors Erweiterung
**Typ:** ❌ **ENTFERNT** - Nicht Teil einer Datenbank  
**Begründung:** Ingestion/Content Processing ist nicht Aufgabe einer Datenbank

**Anmerkung:** Ursprünglich geplante Features (PDF, Office, Video/Audio Processing) werden nicht implementiert, da Content Ingestion außerhalb des DB-Scopes liegt.

---

## 📋 Mögliche zukünftige Prioritäten (Post-v1.0.0)

### Hardware GPU CUDA Support
**Status:** Design/Research  
**Aufwand:** 2-3 Monate  
**Priorität:** TBD

**Features:**
- Faiss GPU Integration für Vector Search
- CUDA Kernels für Distance Computation
- GPU Memory Management
- 10-50x Speedup für Batch Queries

### REST API Erweiterungen
**Status:** Planning  
**Aufwand:** TBD  
**Priorität:** TBD

**Features:**
- GraphQL API Support
- OpenAPI 3.0 Spec Completion
- API Versioning
- Rate Limiting Improvements

---

## 📊 Implementierungsreihenfolge (Q1 2026) - AKTUALISIERT v2

```
Woche 1:     CI/CD Verbesserungen              [P1] ← NÄCHSTER BRANCH
              └─ feature/cicd-improvements
              • GitHub Actions Workflows (ci.yml, code-quality.yml, coverage.yml, release.yml)
              • Trivy Security Scanning
              • Automated Testing Matrix (Linux + Windows)

Woche 2-4:   JavaScript/Python SDK Beta        [P0]
              └─ feature/sdk-beta-release
              • TypeScript Definitions + Transaction Support
              • Python Type Hints + Async/Await
              • NPM + PyPI Package Publishing

Woche 5-13:  TBD - Based on priorities post-v1.0.0
              • Mögliche Optionen: GPU CUDA Support, REST API Erweiterungen
              • Enterprise Features: Docker Optimization
```

**Entfernte Features:**
- ❌ Content Processors (Nicht DB-Aufgabe, Ingestion ist externe Verantwortung)
- ✅ Column-Level Encryption (Bereits implementiert als Field-Level Encryption)
- ✅ Window Functions (Bereits vollständig implementiert)

**Post-v1.0.0 Features:**
- Docker Runtime Optimization (Enterprise Feature)
- GPU CUDA Support (Research/Design Phase)

---

## ✅ Erfolgskriterien für nächsten Branch

### CI/CD Verbesserungen (Nächster Branch)

**GitHub Actions Workflows:**
- [ ] CI Workflow (.github/workflows/ci.yml) erstellt
  - [ ] Linux Build Matrix (Ubuntu 20.04, 22.04)
  - [ ] Windows Build Matrix (Windows 2019, 2022)
  - [ ] Alle 303 Tests laufen automatisch
  - [ ] Build Artifacts werden gespeichert
  
- [ ] Code Quality Workflow (.github/workflows/code-quality.yml) erstellt
  - [ ] Trivy Security Scanning (fail on HIGH/CRITICAL)
  - [ ] CodeQL Analysis aktiviert
  - [ ] clang-tidy Linting
  
- [ ] Coverage Workflow (.github/workflows/coverage.yml) erstellt
  - [ ] Coverage Reports generiert
  - [ ] Codecov/Coveralls Integration
  - [ ] Coverage Badge funktioniert
  
- [ ] Release Workflow (.github/workflows/release.yml) erstellt
  - [ ] Automated GitHub Releases
  - [ ] Container Multi-Arch Builds (amd64, arm64)
  - [ ] Semantic Versioning Support

**Dokumentation:**
- [ ] `docs/ci_cd_guide.md` erstellt
- [ ] README Badge-Links funktionieren
- [ ] Workflow-Dokumentation vollständig

**Qualität:**
- [ ] Alle Workflows laufen erfolgreich
- [ ] Keine Security Alerts
- [ ] Coverage Reports sichtbar

---

## 🚀 Quick Start für nächsten Development Branch

```bash
# 1. Neuen Branch erstellen
git checkout -b feature/cicd-improvements

# 2. GitHub Actions Workflows erstellen
mkdir -p .github/workflows

# 3. CI Workflow erstellen (.github/workflows/ci.yml)
# - Linux + Windows Build Matrix
# - vcpkg dependency management
# - Run all 303 tests
# - Upload build artifacts

# 4. Code Quality Workflow (.github/workflows/code-quality.yml)
# - Trivy security scanning
# - CodeQL analysis
# - clang-tidy linting

# 5. Coverage Workflow (.github/workflows/coverage.yml)
# - Generate coverage reports
# - Upload to Codecov/Coveralls
# - Update coverage badge

# 6. Release Workflow (.github/workflows/release.yml)
# - Build multi-arch Docker images (amd64, arm64)
# - Create GitHub releases
# - Tag versioning

# 7. Dokumentation
# - docs/ci_cd_guide.md erstellen
# - README Badge-Links verifizieren

# 8. Testing
# - Workflows lokal testen (act oder manual trigger)
# - Pull Request erstellen
```

---

## 📎 Referenzen

- **Roadmap:** [ROADMAP.md](ROADMAP.md)
- **Entwicklungsstand:** [DEVELOPMENT_AUDITLOG.md](DEVELOPMENT_AUDITLOG.md)
- **Changelog:** [CHANGELOG.md](CHANGELOG.md)
- **Column Encryption (Implementiert):** [docs/column_encryption.md](docs/column_encryption.md)
- **Encryption Code:** `include/security/encryption.h`, `src/security/field_encryption.cpp`
- **Window Functions (Implementiert):** `include/query/window_evaluator.h`, `src/query/window_evaluator.cpp`
- **SDK Alpha:** `clients/javascript/`, `clients/python/`

---

## 💡 Empfehlung

**Start mit `feature/cicd-improvements`:**
1. **KRITISCH:** Workflows im README referenziert aber fehlen
2. Hohe Priorität (P1) → Security & Code Quality
3. Kurze Implementierungszeit (1 Woche)
4. Keine externen Abhängigkeiten
5. Klare Akzeptanzkriterien
6. Ermöglicht automatisierte Testing & Security Scanning
7. Basis für alle weiteren Entwicklungen

**Warum CI/CD vor SDK:**
- README Badges verlinken auf nicht-existierende Workflows (ci.yml, code-quality.yml)
- Automatisierte Tests kritisch für Code Quality
- Security Scanning (Trivy) fehlt komplett
- SDK-Entwicklung profitiert von CI/CD Infrastructure

**Warum nicht mehr geplant:**
- ❌ **Content Processors:** Ingestion ist nicht DB-Aufgabe (externe Verantwortung)
- ✅ **Column-Level Encryption:** Bereits komplett (Field-Level Encryption)
- ✅ **Window Functions:** Bereits komplett (885 Zeilen Code, 579 Zeilen Tests)

**Nächster Meilenstein nach CI/CD:**
- JavaScript/Python SDK Beta Release (P0, 2-3 Wochen)
- Danach: Post-v1.0.0 Features (Docker Optimization, GPU CUDA Support)

---

**Letzte Aktualisierung:** 20. November 2025 (v2)  
**Nächstes Review:** Nach Abschluss CI/CD Improvements
