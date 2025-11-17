# Dokumentations-Konsolidierungsplan
**Erstellt:** 17. November 2025  
**Zweck:** Detaillierter Plan zur Reorganisation und Konsolidierung der ThemisDB-Dokumentation

---

## Übersicht

Dieses Dokument beschreibt die schrittweise Reorganisation der Dokumentation, um:
1. Duplikate zu eliminieren
2. Eine klare Hierarchie zu schaffen
3. Die Navigation zu verbessern
4. Wartbarkeit zu erhöhen

---

## 1. CDC (Change Data Capture) ✅ BEREITS KONSOLIDIERT

**Aktueller Stand:**
- `docs/cdc.md` - 403 bytes - Redirect zu change_data_capture.md
- `docs/change_data_capture.md` - 14K - Vollständige Dokumentation
- `mkdocs.yml` - Verweist korrekt auf change_data_capture.md

**Status:** ✅ Keine Änderungen erforderlich

**Begründung:** Die Struktur ist bereits optimal. cdc.md dient als kurzer Redirect für Nutzer, die den alten Link verwenden.

---

## 2. Compliance & Governance - KONSOLIDIERUNG ERFORDERLICH

**Aktuelle Dateien:**
1. `docs/compliance.md` - 7.7K - Überblick/Quickstart
2. `docs/compliance_audit.md` - 11K - PKI & Audit Logger
3. `docs/compliance_governance_strategy.md` - 46K - Umfassende Strategie
4. `docs/compliance_integration.md` - 13K - Integration Guide
5. `docs/governance_usage.md` - 8.7K - Usage Examples
6. `docs/EXTENDED_COMPLIANCE_FEATURES.md` - Größe unbekannt

**Probleme:**
- Überlappende Inhalte (z.B. Klassifizierungsstufen in mehreren Dateien)
- Unklare Hierarchie
- Schwer zu navigieren

**Empfohlene Neue Struktur:**

```
docs/compliance/
├── index.md (Überblick, von compliance.md)
├── governance.md (Governance-Strategie, konsolidiert aus compliance_governance_strategy.md + governance_usage.md)
├── audit.md (Audit & PKI, von compliance_audit.md)
├── integration.md (Integration Guide, von compliance_integration.md)
└── extended_features.md (von EXTENDED_COMPLIANCE_FEATURES.md)
```

**Migrationsschritte:**

### Schritt 2.1: Verzeichnis erstellen
```bash
mkdir -p docs/compliance
```

### Schritt 2.2: Dateien erstellen und konsolidieren

**2.2.1: docs/compliance/index.md**
- Quelle: `docs/compliance.md`
- Aktion: Kopieren und erweitern mit Links zu Unterseiten
- Cross-References zu anderen Compliance-Dokumenten

**2.2.2: docs/compliance/governance.md**
- Quellen: 
  - `docs/compliance_governance_strategy.md` (Hauptteil)
  - `docs/governance_usage.md` (Usage-Sektion hinzufügen)
- Aktion: Zusammenführen, Duplikate entfernen
- Struktur:
  - Einführung
  - Klassifizierungsstufen (detailliert)
  - Policy-Engine-Architektur
  - Konfiguration (YAML-Format)
  - Usage Examples (von governance_usage.md)
  - Best Practices

**2.2.3: docs/compliance/audit.md**
- Quelle: `docs/compliance_audit.md`
- Aktion: Minimal editieren, verschieben
- Aktualisieren: PKI-Status auf echte OpenSSL-Implementierung

**2.2.4: docs/compliance/integration.md**
- Quelle: `docs/compliance_integration.md`
- Aktion: Minimal editieren, verschieben

**2.2.5: docs/compliance/extended_features.md**
- Quelle: `docs/EXTENDED_COMPLIANCE_FEATURES.md`
- Aktion: Verschieben und umbenennen

### Schritt 2.3: Redirects erstellen

Alte Dateien mit Redirect-Hinweisen versehen:

**docs/compliance.md:**
```markdown
# Compliance

**Diese Seite wurde verschoben.**  
Bitte nutze die neue Struktur: [docs/compliance/index.md](compliance/index.md)
```

Ähnlich für alle anderen verschobenen Dateien.

### Schritt 2.4: mkdocs.yml aktualisieren

```yaml
  - Sicherheit & Governance:
      - Überblick: compliance/index.md
      - Governance-Strategie: compliance/governance.md
      - Audit & PKI: compliance/audit.md
      - Integration Guide: compliance/integration.md
      - Erweiterte Features: compliance/extended_features.md
      # Alte Security-Docs (siehe Schritt 4)
```

### Schritt 2.5: Validierung

- [ ] Alle Links in den neuen Dateien prüfen
- [ ] mkdocs build erfolgreich
- [ ] Cross-References aktualisieren

---

## 3. Encryption - STRUKTURIERUNG ERFORDERLICH

**Aktuelle Dateien:**
1. `docs/encryption_strategy.md` - Gesamtstrategie
2. `docs/encryption_deployment.md` - Deployment
3. `docs/column_encryption.md` - Feature-spezifisch

**Analyse:**
- Dateien sind gut abgegrenzt
- Könnten unter encryption/ Unterordner organisiert werden
- Aktuell funktional, aber nicht optimal strukturiert

**Empfohlene Struktur:**

```
docs/encryption/
├── index.md (Überblick, Verlinkung zu Unterseiten)
├── strategy.md (von encryption_strategy.md)
├── deployment.md (von encryption_deployment.md)
└── column_level.md (von column_encryption.md)
```

**Alternative (einfacher):**
- Dateien im Root belassen
- Nur index.md als Overview hinzufügen
- Klare Cross-References zwischen Dateien

**Empfehlung:** Alternative (einfacher), da nur 3 Dateien und gute Abgrenzung

**Aktionen:**
1. Cross-References zwischen den 3 Dateien ergänzen
2. Jede Datei mit "Related Docs" Sektion versehen
3. Optional: `docs/encryption/index.md` als Overview erstellen

---

## 4. Security - REORGANISATION ERFORDERLICH

**Problem:** 
- `docs/security/` Verzeichnis existiert, ist aber leer
- Viele Security-Docs sind in `docs/` Root

**Aktuelle Dateien im Root:**
1. `docs/security_hardening_guide.md`
2. `docs/security_audit_checklist.md`
3. `docs/security_audit_report.md`
4. `docs/security_encryption_gap_analysis.md`
5. `docs/rbac_authorization.md`
6. `docs/pii_detection_engines.md`
7. `docs/pii_engine_signing.md`
8. `docs/pii_api.md`
9. Encryption-Docs (siehe Schritt 3)
10. Compliance-Docs (siehe Schritt 2)

**Empfohlene Struktur:**

```
docs/security/
├── index.md (Überblick, aktuell docs/security/overview.md?)
├── hardening.md (von security_hardening_guide.md)
├── audit_checklist.md (von security_audit_checklist.md)
├── audit_report.md (von security_audit_report.md)
├── encryption_gap_analysis.md (von security_encryption_gap_analysis.md)
├── rbac.md (von rbac_authorization.md)
├── pii/
│   ├── overview.md (von pii_detection.md)
│   ├── engines.md (von pii_detection_engines.md)
│   ├── signing.md (von pii_engine_signing.md)
│   └── api.md (von pii_api.md)
├── key_management.md (existiert bereits)
├── policies.md (existiert bereits)
└── threat_model.md (existiert bereits)
```

**Migrationsschritte:**

### Schritt 4.1: PII-Unterordner erstellen
```bash
mkdir -p docs/security/pii
```

### Schritt 4.2: Dateien verschieben und umbenennen

**PII-Docs:**
```bash
# Konzeptuell (Git-Commands folgen später):
mv docs/pii_detection_engines.md docs/security/pii/engines.md
mv docs/pii_engine_signing.md docs/security/pii/signing.md
mv docs/pii_api.md docs/security/pii/api.md
# Neu erstellen: docs/security/pii/overview.md (von pii_detection.md)
```

**Security-Docs:**
```bash
mv docs/security_hardening_guide.md docs/security/hardening.md
mv docs/security_audit_checklist.md docs/security/audit_checklist.md
mv docs/security_audit_report.md docs/security/audit_report.md
mv docs/security_encryption_gap_analysis.md docs/security/encryption_gap_analysis.md
mv docs/rbac_authorization.md docs/security/rbac.md
```

### Schritt 4.3: index.md erstellen

**docs/security/index.md:**
```markdown
# Security Overview

Themis bietet umfassende Sicherheitsfeatures für Enterprise-Anwendungen.

## Bereiche

- [Hardening Guide](hardening.md) - Sicherheitshärtung
- [RBAC](rbac.md) - Zugriffskontrolle
- [PII Detection](pii/overview.md) - Personenbezogene Daten
- [Key Management](key_management.md) - Schlüsselverwaltung
- [Policies](policies.md) - Sicherheitsrichtlinien
- [Threat Model](threat_model.md) - Bedrohungsmodell
- [Audit](audit_checklist.md) - Audit & Compliance

## Implementation Status

⚠️ **Wichtig:** Die meisten Security-Features sind für Post-Release geplant (Phase 7).
Aktuell implementiert:
- ✅ Field-Level Encryption (Column Encryption)
- ✅ PII Detection APIs
- ⏳ RBAC - GEPLANT
- ⏳ Audit Logging - TEILWEISE
```

### Schritt 4.4: Redirects erstellen

Alte Dateien mit Redirect-Hinweisen versehen.

### Schritt 4.5: mkdocs.yml aktualisieren

```yaml
  - Sicherheit & Governance:
      - Security Overview: security/index.md
      - Hardening Guide: security/hardening.md
      - RBAC: security/rbac.md
      - PII Detection:
          - Überblick: security/pii/overview.md
          - Engines: security/pii/engines.md
          - Signing: security/pii/signing.md
          - API: security/pii/api.md
      - Key Management: security/key_management.md
      - Policies: security/policies.md
      - Threat Model: security/threat_model.md
      - Audit:
          - Checklist: security/audit_checklist.md
          - Report: security/audit_report.md
      - Gap Analysis: security/encryption_gap_analysis.md
```

---

## 5. Observability - NEUE STRUKTUR VORGESCHLAGEN

**Aktuelle Situation:**
- `docs/tracing.md` - OpenTelemetry
- Prometheus-Metriken nicht umfassend dokumentiert
- Operations-Runbook existiert

**Empfohlene Struktur:**

```
docs/observability/
├── index.md (Überblick)
├── prometheus_metrics.md (NEU - siehe DOCUMENTATION_TODO.md)
├── tracing.md (verschieben von docs/tracing.md)
└── operations_runbook.md (verschieben von docs/operations_runbook.md)
```

**Aktionen:**
1. [ ] Verzeichnis erstellen
2. [ ] prometheus_metrics.md erstellen (siehe Gap Analysis)
3. [ ] Dateien verschieben
4. [ ] index.md erstellen
5. [ ] mkdocs.yml aktualisieren

---

## 6. APIs - KONSOLIDIERUNG

**Aktuelle Dateien:**
- `docs/apis/openapi.md` - OpenAPI-Dokumentation
- `docs/openapi.yaml` - OpenAPI-Spezifikation (Root)
- Verschiedene API-Docs verstreut (pii_api.md, etc.)

**Empfohlene Struktur:**

```
docs/apis/
├── index.md (Überblick über alle APIs)
├── openapi.md (existiert bereits)
├── rest_api.md (HTTP REST Endpoints)
├── timeseries_api.md (NEU - TSStore API)
└── graphql_api.md (wenn geplant)
```

**Aktionen:**
1. [ ] index.md erstellen mit Übersicht
2. [ ] timeseries_api.md erstellen (siehe Gap Analysis A6)
3. [ ] openapi.yaml erweitern (Backup/Restore, Vector endpoints)

---

## Prioritäten

### Phase 1: Kritische Fixes (Diese Woche)
1. ✅ Inkonsistenzen in todo.md und implementation_status.md beheben
2. [ ] Compliance-Docs konsolidieren (Schritt 2)
3. [ ] Security-Docs reorganisieren (Schritt 4)

### Phase 2: Neue Dokumentation (Nächste 2 Wochen)
4. [ ] Observability-Struktur aufbauen (Schritt 5)
5. [ ] APIs konsolidieren (Schritt 6)
6. [ ] Prometheus Metrics Reference erstellen
7. [ ] TimeSeries API dokumentieren

### Phase 3: Validierung (Nächste 4 Wochen)
8. [ ] Alle Links validieren
9. [ ] mkdocs build testen
10. [ ] README.md aktualisieren
11. [ ] architecture.md aktualisieren

---

## Git-Workflow

### Für jeden Konsolidierungsschritt:

1. **Neue Dateien erstellen**
   ```bash
   mkdir -p docs/new_directory
   # Dateien erstellen/kopieren
   ```

2. **Redirects in alten Dateien**
   ```markdown
   # Alte Datei
   **Diese Seite wurde verschoben.**  
   Siehe: [Neue Seite](new_location.md)
   ```

3. **mkdocs.yml aktualisieren**

4. **Build testen**
   ```bash
   mkdocs build
   ```

5. **Commit**
   ```bash
   git add docs/
   git commit -m "Reorganize [topic] documentation"
   ```

6. **Report Progress**
   - Fortschritt dokumentieren
   - Checklist aktualisieren

---

## Tracking

### Abgeschlossen
- [x] Plan erstellt
- [x] Inkonsistenzen behoben (todo.md, implementation_status.md)

### In Bearbeitung
- [ ] Compliance-Konsolidierung

### Ausstehend
- [ ] Security-Reorganisation
- [ ] Observability-Struktur
- [ ] APIs-Konsolidierung
- [ ] Encryption-Strukturierung
- [ ] Validierung

---

**Letzte Aktualisierung:** 17. November 2025  
**Nächstes Review:** Nach Phase 1
