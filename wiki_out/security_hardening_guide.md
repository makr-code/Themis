# Themis – Security Hardening Guide

Praxisleitfaden zur Härtung von Themis-Server und Admin-Tools.

## Server-Härtung
- Reverse Proxy vor Themis (TLS, Rate Limiting, Auth): Nginx/Traefik empfohlen
- TLS: TLS 1.2+, HSTS, sichere Cipher Suites, OCSP Stapling
- Accounts: Least-Privilege Service User, kein Admin-Kontext
- Firewall: Nur benötigte Ports (8765) freigeben, IP-Restriktionen erwägen
- Logging: Security-Events zentralisieren; Log Rotation, WORM/ELK/Graylog
- Ressourcen: Request-Timeouts, Body-Size-Limits, Parallelitätsgrenzen
- Build: Reproducible, vcpkg Baseline fixiert; ASAN/UBSAN im Testlauf

## Admin-Tools (WPF)
- Code Signing der EXEs und Installer (MSIX/WiX)
- Updates: Signierte Updates; Hash-Validierung bei Verteilung
- Netzwerk: Nur HTTPS-Endpoints verwenden; Zertifikatsvalidierung aktiv
- Konfiguration: Keine Secrets in Klartextdateien; Windows Credential Locker/DPAPI
- Telemetrie/Logs: Keine PII im Klartext; Minimalprinzip

## Secrets-Management
- Keinerlei Secrets im Repo halten; .gitignore beachten
- Nutzung von Secret Stores (Windows, Azure Key Vault, HashiCorp Vault)
- Rotationspläne definieren (LEK/KEK/DEK + App-Secrets)

## Compliance-Aspekte
- DSGVO: Recht auf Löschung, Auskunft, Pseudonymisierung
- Auditierbarkeit: Export-/Löschaktionen protokollieren
- Aufbewahrung: Retention-Policies technisch durchsetzen

## Checklisten & Gates
- Vor Release: `docs/security_audit_checklist.md` durchgehen
- Vulnerability-Scans ohne kritische Funde
- Signierte, versionierte Artefakte im `dist/`-Pfad
