# Security & Compliance Review

Dieses Dokument fasst den Sicherheits- und Compliance-Status von ThemisDB zusammen, verlinkt die relevanten Detaildokumente und enthält eine überprüfbare Checkliste für Audits.

## Geltungsbereich

- Daten-at-Rest und Daten-in-Transit
- Schlüsselverwaltung und Kryptokonfiguration
- PII-Erkennung, Audit/Retention
- Härtung, Bedrohungsmodell, Betriebsprozesse
- Compliance-Mappings (GDPR/DSGVO, ISO 27001-nahe Praktiken)

## Referenz-Dokumente

- Überblick: [Security Overview](overview.md)
- Schlüsselverwaltung: [Key Management](key_management.md)
- Verschlüsselung: [Encryption Strategy](../encryption_strategy.md), [Encryption Deployment](../encryption_deployment.md), [Column Encryption](../column_encryption.md)
- PII: [PII Detection (Overview)](pii_detection.md), [PII Engines](../pii_detection_engines.md), [PII Engine Signing](../pii_engine_signing.md)
- Audit & Retention: [Audit & Retention](audit_and_retention.md)
- Threat Modeling: [Threat Model](threat_model.md)
- Hardening: [Security Hardening Guide](../security_hardening_guide.md)
- Compliance: [Compliance Audit](../compliance_audit.md), [Governance-Strategie](../compliance_governance_strategy.md), [Compliance-Integration](../compliance_integration.md), [Governance Usage](../governance_usage.md)
- Operations: [Deployment & Betrieb](../deployment.md), [Operations Runbook](../operations_runbook.md), [Tracing & Observability](../tracing.md)

## Audit-Checkliste (Kernpunkte)

- Kryptographie
  - Transportverschlüsselung (TLS/Reverse Proxy) konfiguriert
  - At-Rest: Komponentenspezifische Verschlüsselung (SST/Blob/Spalten) bewertet und konfiguriert
  - Schlüsselrotation, KMS-Integration (Konzept & Schnittstellen) dokumentiert
- Zugriff & AuthZ
  - Admin-Endpoints abgesichert, sensible Operationen geloggt
  - Optionales RBAC/Scopes (Roadmap) definiert
- PII & Datenschutz
  - PII-Detection Flows und Ausnahmenprozess dokumentiert
  - Retention-Policies (TTL/Archivierung) technisch verankert
- Auditierbarkeit
  - Audit-Events definiert (Create/Update/Delete, Indexops, Admin)
  - Retention & Export der Auditdaten beschrieben
- Härtung
  - Container/K8s Best Practices, minimaler OS-Footprint, Secrets-Handling
  - Angriffspunkte aus Threat Model gemappt auf Mitigations
- Observability & Incident Response
  - Prometheus-/Tracing-Integration, Alarme (SLOs) vorhanden
  - Runbook: Playbooks für Ausfälle, Rebuilds, Backups

## Verifikation (Stichproben)

- /metrics enthält sicherheitsrelevante Zähler (z. B. Fehler, Auth-Fehlschläge sofern implementiert)
- Konfiguration (Secrets, Ports, CORS) in `deployment.md`/`docker-compose.yml` nachvollziehbar
- CDC-/SSE-Endpunkte: Hinweis auf Reverse-Proxy-Konfiguration (TLS, Timeouts)

## Offene Punkte / Empfehlungen

- RBAC/Policies: Ausarbeitung und Implementierungsfahrplan (siehe Roadmap)
- Keys at rest: Optionale Integration externer KMS (HashiCorp Vault, AWS KMS)
- Secrets-Scanning in CI (gitleaks) und SBOM/Signaturen (Syft/Cosign)
- Penetrationstest-Checkliste ergänzen; Fuzzing-Pfade (Parser) prüfen

## Änderungsverlauf

- 2025-11-02: Erstveröffentlichung der konsolidierten Review-Seite
